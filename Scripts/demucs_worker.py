#!/usr/bin/env python
"""Stem separation worker for MashupDaw.

Reads one JSON job from stdin (or --job file) and prints JSON lines on stdout:
  {"type":"info", "device":"cuda", "model":"htdemucs"}
  {"type":"progress", "value":0.42, "stage":"separating"}
  {"type":"done", "files":{"vocals":"/path/vocals.wav", ...}, "sample_rate":44100}
  {"type":"error", "message":"..."}
Job: {"input": path, "output_dir": path, "model": "htdemucs", "device": "auto|cuda|cpu",
      "two_stems": null | "vocals", "shifts": 1, "overlap": 0.25, "format": "wav", "name": "song"}
Cancellation: the parent simply terminates this process.
"""
import json, os, sys, time

def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()

def main():
    job_text = None
    if len(sys.argv) > 2 and sys.argv[1] == "--job":
        job_text = open(sys.argv[2], "r", encoding="utf-8").read()
    else:
        job_text = sys.stdin.readline()
    try:
        job = json.loads(job_text)
    except Exception as e:
        emit({"type": "error", "message": f"bad job json: {e}"}); return 2

    try:
        emit({"type": "progress", "value": 0.0, "stage": "loading torch"})
        import torch
        import torchaudio
        from demucs.pretrained import get_model
        from demucs.apply import apply_model
        from demucs.audio import convert_audio
    except Exception as e:
        emit({"type": "error", "message": f"python environment missing dependencies: {e}"}); return 3

    device = job.get("device", "auto")
    if device == "auto":
        device = "cuda" if torch.cuda.is_available() else "cpu"
    if device == "cuda" and not torch.cuda.is_available():
        emit({"type": "info", "message": "CUDA requested but unavailable, falling back to CPU"})
        device = "cpu"
    model_name = job.get("model", "htdemucs")
    try:
        emit({"type": "progress", "value": 0.02, "stage": f"loading model {model_name}"})
        model = get_model(model_name)
        model.to(device)
        model.eval()
    except Exception as e:
        emit({"type": "error", "message": f"could not load model {model_name}: {e}"}); return 4
    emit({"type": "info", "device": device, "model": model_name, "sources": list(model.sources),
          "gpu": torch.cuda.get_device_name(0) if device == "cuda" else ""})

    inp = job["input"]
    try:
        emit({"type": "progress", "value": 0.05, "stage": "reading audio"})
        wav, sr = _load_audio(inp, torchaudio)
        wav = convert_audio(wav, sr, model.samplerate, model.audio_channels)
    except Exception as e:
        emit({"type": "error", "message": f"could not read {inp}: {e}"}); return 5

    ref = wav.mean(0)
    mean, std = ref.mean(), ref.std() + 1e-8
    mix = ((wav - mean) / std).unsqueeze(0).to(device)

    total = wav.shape[-1]
    shifts = int(job.get("shifts", 1))
    overlap = float(job.get("overlap", 0.25))
    state = {"last": time.time()}

    def callback(info):
        # demucs>=4.1 passes dicts with segment_offset / audio_length / state
        try:
            off = float(info.get("segment_offset", 0)); length = float(info.get("audio_length", total) or total)
            frac = min(1.0, max(0.0, off / length)) if length > 0 else 0.0
            shift_idx = int(info.get("shift_idx", 0)); models = max(1, int(info.get("models", 1))); model_idx = int(info.get("model_idx_in_bag", 0))
            frac = (model_idx + (shift_idx + frac) / max(1, shifts)) / models
            now = time.time()
            if now - state["last"] > 0.1 or info.get("state") == "end":
                state["last"] = now
                emit({"type": "progress", "value": 0.08 + 0.84 * frac, "stage": "separating"})
        except Exception:
            pass

    try:
        emit({"type": "progress", "value": 0.08, "stage": "separating"})
        with torch.no_grad():
            try:
                out = apply_model(model, mix, shifts=shifts, split=True, overlap=overlap, progress=False,
                                  device=device, callback=callback)
            except TypeError:
                out = apply_model(model, mix, shifts=shifts, split=True, overlap=overlap, progress=False, device=device)
        out = out * std + mean
        out = out[0].cpu()
    except Exception as e:
        emit({"type": "error", "message": f"separation failed: {e}"}); return 6

    sources = list(model.sources)
    stems = {name: out[i] for i, name in enumerate(sources)}
    two = job.get("two_stems")
    if two and two in stems:
        other = sum(t for n, t in stems.items() if n != two)
        stems = {two: stems[two], f"no_{two}": other}

    out_dir = job["output_dir"]
    os.makedirs(out_dir, exist_ok=True)
    base = job.get("name") or os.path.splitext(os.path.basename(inp))[0]
    files = {}
    emit({"type": "progress", "value": 0.94, "stage": "writing stems"})
    for name, tensor in stems.items():
        path = os.path.join(out_dir, f"{base} - {name}.wav")
        _save_wav(path, tensor.clamp(-1, 1), model.samplerate, torchaudio)
        files[name] = path
    emit({"type": "progress", "value": 1.0, "stage": "done"})
    emit({"type": "done", "files": files, "sample_rate": model.samplerate})
    return 0

def _load_audio(path, torchaudio):
    try:
        wav, sr = torchaudio.load(path)
        return wav, sr
    except Exception:
        import soundfile as sf, numpy as np, torch
        data, sr = sf.read(path, dtype="float32", always_2d=True)
        return torch.from_numpy(np.ascontiguousarray(data.T)), sr

def _save_wav(path, tensor, sr, torchaudio):
    try:
        torchaudio.save(path, tensor, sr, encoding="PCM_F", bits_per_sample=32)
    except Exception:
        import soundfile as sf
        sf.write(path, tensor.numpy().T, sr, subtype="FLOAT")

if __name__ == "__main__":
    sys.exit(main())
