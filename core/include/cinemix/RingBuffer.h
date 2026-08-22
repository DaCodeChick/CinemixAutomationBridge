// SpScRingBuffer — bounded single-producer/single-consumer ring buffer.
// Used for:
//   * transport callback → bridge worker (raw MIDI bytes)
//   * audio/host thread → bridge worker (parameter writes)
//
// The producer side is lock-free, allocation-free, and real-time safe; the
// consumer runs on the bridge worker thread. Overflow drops the newest byte
// (or event) and increments a counter the worker can read for diagnostics.
#ifndef CINEMIX_SPSC_RING_H
#define CINEMIX_SPSC_RING_H

#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace cinemix {

template <typename T>
class SpScRingBuffer {
public:
    explicit SpScRingBuffer(size_t capacityPow2) {
        size_t cap = 1;
        while (cap < capacityPow2) cap <<= 1;
        cap = cap < 8 ? 8 : cap;
        buffer_.resize(cap);
        mask_ = cap - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        overflows_.store(0, std::memory_order_relaxed);
    }

    // Producer: returns false if the ring is full (element dropped).
    bool push(const T& value) {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (((t + 1) & mask_) == h) {
            overflows_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buffer_[t] = value;
        tail_.store((t + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Consumer: returns false if empty.
    bool pop(T& out) {
        const size_t t = tail_.load(std::memory_order_acquire);
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h == t) return false;
        out = buffer_[h];
        head_.store((h + 1) & mask_, std::memory_order_release);
        return true;
    }

    size_t overflowCount() const { return overflows_.load(std::memory_order_relaxed); }

    // Drain a range of bytes in bulk (for the byte ring).
    size_t popBulk(T* out, size_t maxCount) {
        size_t n = 0;
        const size_t t = tail_.load(std::memory_order_acquire);
        size_t h = head_.load(std::memory_order_relaxed);
        while (n < maxCount && h != t) {
            out[n++] = buffer_[h];
            h = (h + 1) & mask_;
        }
        head_.store(h, std::memory_order_release);
        return n;
    }

    size_t available() const {
        const size_t t = tail_.load(std::memory_order_acquire);
        const size_t h = head_.load(std::memory_order_relaxed);
        return (t - h) & mask_;
    }

    size_t capacity() const { return buffer_.size(); }

private:
    std::vector<T> buffer_;
    size_t mask_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
    std::atomic<size_t> overflows_;
};

} // namespace cinemix

#endif // CINEMIX_SPSC_RING_H
