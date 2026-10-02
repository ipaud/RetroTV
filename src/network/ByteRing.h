#pragma once

// A byte ring with one producer (the network task) and one consumer (a MediaPlayer task), and
// the consumer's blocking read on top of it. Lock-free: each side only writes its own counter.
// Pure C++: tested on the host.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <atomic>

class ByteRing {
 public:
  // capacity: a power of two, so the uint32 counters stay right across their wrap (a stream
  // passes 4 GB in about 6 h). start: where both counters begin; tests use it to cross the wrap.
  void attach(uint8_t* buf, uint32_t capacity, uint32_t start = 0) {
    buf_ = buf;
    capacity_ = capacity;
    head_ = start;
    tail_ = start;
  }
  // Only while neither side is using the ring.
  void clear() { tail_.store(head_.load()); }

  uint32_t capacity() const { return capacity_; }
  uint32_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
  uint32_t space() const { return capacity_ - size(); }

  // Producer. Copies what fits; returns how much.
  uint32_t push(const uint8_t* src, uint32_t n) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t free = capacity_ - (head - tail_.load(std::memory_order_acquire));
    if (n > free) n = free;
    copyIn(head, src, n);
    head_.store(head + n, std::memory_order_release);
    return n;
  }

  // Consumer. Copies what is there, up to n; returns how much.
  uint32_t pop(uint8_t* dst, uint32_t n) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    const uint32_t used = head_.load(std::memory_order_acquire) - tail;
    if (n > used) n = used;
    copyOut(tail, dst, n);
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

 private:
  // Both copy in two parts when the span wraps the end of the buffer.
  void copyIn(uint32_t at, const uint8_t* src, uint32_t n) {
    if (n == 0) return;
    const uint32_t pos = at & (capacity_ - 1);
    const uint32_t first = n < capacity_ - pos ? n : capacity_ - pos;
    memcpy(buf_ + pos, src, first);
    memcpy(buf_, src + first, n - first);
  }
  void copyOut(uint32_t at, uint8_t* dst, uint32_t n) const {
    if (n == 0) return;
    const uint32_t pos = at & (capacity_ - 1);
    const uint32_t first = n < capacity_ - pos ? n : capacity_ - pos;
    memcpy(dst, buf_ + pos, first);
    memcpy(dst + first, buf_, n - first);
  }

  uint8_t* buf_ = nullptr;
  uint32_t capacity_ = 0;
  std::atomic<uint32_t> head_{0};  // bytes ever pushed
  std::atomic<uint32_t> tail_{0};  // bytes ever popped
};

enum class RingRead : uint8_t {
  Data,       // `got` bytes copied
  Ended,      // the body is over and everything was read
  Cancelled,  // the programme is being stopped
  Stalled,    // nothing arrived for stallMs: the connection is as good as lost
};

// The consumer's read: returns what the ring holds at once; with nothing there, waits (sleep()
// is one short slice) until data, the end of the body, a cancel, or stallMs without a byte.
// The producer sets `ended` after its last push. Reading it before the pop means that when it
// says "over", that pop already saw every byte: an empty ring then really is the end.
template <typename Now, typename Sleep>
RingRead readWaiting(ByteRing& ring, uint8_t* dst, uint32_t len, uint32_t& got, const std::atomic<bool>& ended,
                     const std::atomic<bool>& cancelled, uint32_t stallMs, Now now, Sleep sleep) {
  const uint32_t start = now();
  for (;;) {
    const bool over = ended.load();
    got = ring.pop(dst, len);
    if (got > 0) return RingRead::Data;
    if (over) return RingRead::Ended;
    if (cancelled.load()) return RingRead::Cancelled;
    if (now() - start >= stallMs) return RingRead::Stalled;
    sleep();
  }
}
