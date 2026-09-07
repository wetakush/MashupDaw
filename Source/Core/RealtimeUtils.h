#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include <cstddef>
#include <juce_core/juce_core.h>

namespace mashup
{

/** Publishes an immutable object from the message thread to the audio thread without locks.
    The audio thread calls acquire()/release() around its use; retired objects are destroyed on
    the message thread by collectGarbage(). */
template <typename T>
class RealtimeSwap
{
public:
    RealtimeSwap() = default;
    ~RealtimeSwap() { delete current.load(); for (auto* p : retired) delete p; }

    /** Message thread. */
    void publish (std::unique_ptr<T> next)
    {
        T* old = current.exchange (next.release(), std::memory_order_acq_rel);
        if (old != nullptr) retired.push_back (old);
        collectGarbage();
    }

    /** Message thread: deletes objects no longer in use by the audio thread. */
    void collectGarbage()
    {
        // an object is safe to delete when no reader holds it and it is not current
        for (size_t i = 0; i < retired.size();)
        {
            if (retired[i]->rtUseCount.load (std::memory_order_acquire) == 0)
            {
                delete retired[i];
                retired[i] = retired.back();
                retired.pop_back();
            }
            else ++i;
        }
    }

    /** Audio thread scoped access. */
    struct Handle
    {
        Handle (std::atomic<T*>& c)
        {
            // spin-free: load, increment, re-validate
            for (;;)
            {
                ptr = c.load (std::memory_order_acquire);
                if (ptr == nullptr) return;
                ptr->rtUseCount.fetch_add (1, std::memory_order_acq_rel);
                if (c.load (std::memory_order_acquire) == ptr) return;
                ptr->rtUseCount.fetch_sub (1, std::memory_order_acq_rel);
            }
        }
        ~Handle() { if (ptr) ptr->rtUseCount.fetch_sub (1, std::memory_order_acq_rel); }
        T* get() const noexcept { return ptr; }
        T* operator->() const noexcept { return ptr; }
        explicit operator bool() const noexcept { return ptr != nullptr; }
    private:
        T* ptr = nullptr;
    };

    Handle acquire() noexcept { return Handle (current); }

    /** Message thread only. */
    T* getMessageThreadView() const noexcept { return current.load (std::memory_order_acquire); }

private:
    std::atomic<T*> current { nullptr };
    std::vector<T*> retired;   // message thread only
};

/** Mixin giving a class the use counter required by RealtimeSwap. */
struct RealtimeShared
{
    std::atomic<int> rtUseCount { 0 };
};

/** Single-producer single-consumer lock-free queue for fixed-size POD messages. */
template <typename T, int Capacity = 1024>
class SpscQueue
{
public:
    bool push (const T& item) noexcept
    {
        auto scope = fifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 < 1) return false;
        storage[(size_t) scope.startIndex1] = item;
        return true;
    }
    bool pop (T& out) noexcept
    {
        auto scope = fifo.read (1);
        if (scope.blockSize1 + scope.blockSize2 < 1) return false;
        out = storage[(size_t) scope.startIndex1];
        return true;
    }
    int getNumReady() const noexcept { return fifo.getNumReady(); }
private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> storage {};
};

/** Smoothed atomic parameter for realtime reads. */
class RealtimeParameter
{
public:
    explicit RealtimeParameter (float initial = 0.0f) : target (initial), current (initial) {}
    void set (float v) noexcept { target.store (v, std::memory_order_relaxed); }
    float getTarget() const noexcept { return target.load (std::memory_order_relaxed); }
    /** Audio thread: returns per-sample-smoothed value, `coeff` ~ 0.001..0.05 */
    float next (float coeff = 0.01f) noexcept
    {
        const float t = target.load (std::memory_order_relaxed);
        current += (t - current) * coeff;
        if (std::abs (t - current) < 1.0e-6f) current = t;
        return current;
    }
    float peek() const noexcept { return current; }
    void snap() noexcept { current = target.load (std::memory_order_relaxed); }
private:
    std::atomic<float> target;
    float current;
};

} // namespace mashup
