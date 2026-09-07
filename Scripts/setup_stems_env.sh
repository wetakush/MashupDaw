#!/usr/bin/env bash
# Creates the Python environment used by MashupDaw's stem separation (Demucs / PyTorch).
# Usage: Scripts/setup_stems_env.sh [--cpu]
# The environment lives in <repo>/.venv-stems (or $MASHUP_STEMS_VENV). GPU (CUDA) wheels are installed by default.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="${MASHUP_STEMS_VENV:-$HERE/../.venv-stems}"
PY_VERSION="3.12"
CPU_ONLY=0
[[ "${1:-}" == "--cpu" ]] && CPU_ONLY=1

if command -v uv >/dev/null 2>&1; then
  echo "[stems] creating venv with uv (python $PY_VERSION) at $VENV"
  uv venv --python "$PY_VERSION" "$VENV" --quiet --allow-existing
  PIP=(uv pip install --python "$VENV/bin/python" --quiet)
else
  echo "[stems] uv not found, falling back to python3 -m venv (torch may not support this interpreter version)"
  python3 -m venv "$VENV"
  PIP=("$VENV/bin/python" -m pip install --quiet)
fi

if [[ $CPU_ONLY == 1 ]]; then
  "${PIP[@]}" torch torchaudio --index-url https://download.pytorch.org/whl/cpu
else
  "${PIP[@]}" torch torchaudio            # default Linux wheels bundle CUDA
fi
"${PIP[@]}" "demucs>=4.0.1" soundfile numpy
"$VENV/bin/python" - <<'PY'
import torch, demucs
print(f"[stems] torch {torch.__version__}, cuda available: {torch.cuda.is_available()}, demucs {demucs.__version__}")
PY
echo "[stems] done. MashupDaw will use $VENV/bin/python"
