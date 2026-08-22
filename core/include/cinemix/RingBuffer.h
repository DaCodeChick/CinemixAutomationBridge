// SpScRingBuffer — bounded single-producer/single-consumer ring buffer.
// Used for:
//   * transport callback → bridge worker (raw MIDI bytes)
//   * audio/host thread → bridge worker (parameter writes)
//
// The producer side is lock-free, allocation-free, and real-time safe; the
// consumer runs on the bridge worker thread. Overflow drops the newest byte
// (or event) and increments a counter the worker can read for diagnostics.
//
// API note: `bool push(const T&)` / `bool pop(T&)` / `popBulk(T*, n)` are
// deliberate — they express bounded producer/consumer semantics without
// allocating result containers, and stay allocation-free on the hot path.
// Do not "modernize" them into value-returning containers.
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
    explicit SpScRingBuffer(std::size_t capacity) {
        std::size_t rounded = 1;
        while (rounded < capacity) rounded <<= 1;
        rounded = rounded < kMinimumCapacity ? kMinimumCapacity : rounded;
        buffer_.resize(rounded);
        mask_ = rounded - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        overflows_.store(0, std::memory_order_relaxed);
    }

    // Producer: returns false if the ring is full (element dropped).
    bool push(const T& value) {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (((tail + 1) & mask_) == head) {
            overflows_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buffer_[tail] = value;
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Consumer: returns false if empty.
    bool pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail) return false;
        out = buffer_[head];
        head_.store((head + 1) & mask_, std::memory_order_release);
        return true;
    }

    std::size_t overflowCount() const noexcept {
        return overflows_.load(std::memory_order_relaxed);
    }

    // Drain a range of bytes in bulk (for the byte ring).
    std::size_t popBulk(T* out, std::size_t maxCount) {
        std::size_t count = 0;
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        std::size_t head = head_.load(std::memory_order_relaxed);
        while (count < maxCount && head != tail) {
            out[count++] = buffer_[head];
            head = (head + 1) & mask_;
        }
        head_.store(head, std::memory_order_release);
        return count;
    }

    std::size_t available() const noexcept {
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t head = head_.load(std::memory_order_relaxed);
        return (tail - head) & mask_;
    }

    std::size_t capacity() const noexcept { return buffer_.size(); }

private:
    static constexpr std::size_t kMinimumCapacity = 8;

    std::vector<T> buffer_;
    std::size_t mask_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
    std::atomic<std::size_t> overflows_;
};

} // namespace cinemix

#endif // CINEMIX_SPSC_RING_H
