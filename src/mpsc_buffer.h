#pragma once

#include <array>
#include <thread>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include "internal.h"

namespace mesh {

constexpr bool is_power_two(int N) {
  return ((N & (N - 1)) == 0);
}

static constexpr std::size_t g_ring_buffer_size = 1024;

template <typename Ptr>
class alignas(64) ring_buffer {
public:
  static_assert(std::is_pointer<Ptr>::value, "buffer item should be a pointer");

  using index_type = std::int64_t;
  using value_type = std::atomic<Ptr>;
  using cursor_type = std::atomic<index_type>;
  using buffer_type = internal::vector<value_type>;

  ring_buffer(index_type arity = g_ring_buffer_size)
      : m_pptr(0), m_gptr(0), m_arity(arity), m_mask(arity - 1), m_buffer(arity) {
    for (value_type &v : m_buffer)
      v.store(nullptr, std::memory_order_release);
  }

public:
  inline void push(Ptr p) {
    index_type inx = m_pptr.fetch_add(1);
    while ((inx - m_arity) >= m_gptr.load(std::memory_order_acquire))
      std::this_thread::yield();
    get(inx).store(p, std::memory_order_release);
  }
  inline bool try_push(Ptr p) {
    index_type ginx = m_gptr.load(std::memory_order_acquire);
    index_type pinx = m_pptr.load(std::memory_order_acquire);
    if ((pinx - ginx) >= m_arity)
      return false;
    push(p);
    return true;
  }
  inline Ptr pop() {
    index_type ginx = m_gptr.load(std::memory_order_acquire);
    index_type pinx = m_pptr.load(std::memory_order_acquire);
    if (ginx >= pinx)
      return nullptr;
    value_type &v = get(ginx);
    Ptr vp;
    while (!(vp = v.exchange(nullptr)))
      ;
    m_gptr.store(ginx + 1, std::memory_order_release);
    return vp;
  }
  inline std::size_t pop(std::vector<Ptr> &op) {
    index_type ginx = m_gptr.load(std::memory_order_acquire);
    index_type pinx = m_pptr.load(std::memory_order_acquire);
    if (ginx >= pinx)
      return 0;
    if (pinx >= ginx + m_arity)
      pinx = ginx + m_arity;
    Ptr vp;
    for (index_type inx = ginx; inx < pinx; ++inx) {
      value_type &v = get(inx);
      while (!(vp = v.exchange(nullptr)))
        ;
      op.emplace_back(vp);
    }
    m_gptr.store(pinx, std::memory_order_release);
    return pinx - ginx;
  }

  inline std::size_t size() const {
    return m_buffer.size();
  }

private:
  inline value_type &get(index_type inx) {
    return m_buffer[inx & m_mask];
  }

  cursor_type m_pptr;
  char m_padding_1[64 - sizeof(cursor_type)];
  cursor_type m_gptr;
  char m_padding_2[64 - sizeof(cursor_type)];
  index_type m_arity;
  index_type m_mask;
  char m_padding_3[64 - sizeof(index_type) * 2];
  buffer_type m_buffer;
};

}  // namespace mesh
