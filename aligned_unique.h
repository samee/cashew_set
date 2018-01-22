// Provides two main utilities:
//   * make_aligned_unique<T,align>: Like std::make_unique<T>, but constructs
//       the objects at a given alignment. Requires sizeof(T) to be a multiple
//       of `align`, and `align` to be a power of 2.
//   * aligned_unique_ptr<T>: Type alias of std::unique_ptr with a deleter that
//       can free aligned memory allocated with make_aligned_unique.
#pragma once

#include <cstdlib>
#include <memory>

namespace cashew {

template <class T>
class free_deleter {
 public:
  void operator()(void* p) { static_cast<T*>(p)->~T(); std::free(p); }
};

template <class T>
class free_deleter<T[]> {
  size_t n_;
 public:
  explicit free_deleter(size_t n) : n_(n) {}
  void operator()(void* vp) {
    T* p = static_cast<T*>(vp);
    for(size_t i=0;i<n_;++i) p[i].~T();
    std::free(p);
  }
};


template <class T>
using aligned_unique_ptr = std::unique_ptr<T, free_deleter<T>>;

// Helpers to get around the rule of "no partial specialization of function
// template". Takes functions out of overload resolution based on template
// parameter.
template <class T> struct make_aligned_unique_result {
  using single_object = aligned_unique_ptr<T>;
};

template <class T> struct make_aligned_unique_result<T[]> {
  using array = aligned_unique_ptr<T[]>;
};

template <class T, size_t n> struct make_aligned_unique_result<T[n]> {
  using no_result = aligned_unique_ptr<T[]>;
};

class uninit_unique_ptr : public std::unique_ptr<void, void(*)(void*)> {
 public:
   uninit_unique_ptr(void* p) noexcept
     : std::unique_ptr<void, void(*)(void*)>(p, std::free) {}
};

template <class T, size_t align, class... Args>
typename make_aligned_unique_result<T>::single_object
make_aligned_unique(Args&&... args) {
  uninit_unique_ptr uninit(aligned_alloc(align, sizeof(T)));
  if (uninit==nullptr) throw std::bad_alloc();
  aligned_unique_ptr<T> rv(new (uninit.get()) T(std::forward<Args>(args)...));
  uninit.release();  // release *after* we know T() didn't throw.
  return std::move(rv);
}

template <class ArrayT,size_t align>
typename make_aligned_unique_result<ArrayT>::array
make_aligned_unique(size_t n) {
  using T = typename std::remove_extent<ArrayT>::type;
  static_assert(sizeof(T)%align == 0,
      "Object size needs to be a multiple of alignment");
  
  uninit_unique_ptr uninit(aligned_alloc(align, sizeof(T)*n));
  if (uninit==nullptr) throw std::bad_alloc();
  aligned_unique_ptr<T[]> rv(new (uninit.get()) T[n], free_deleter<T[]>(n));
  uninit.release();  // release *after* we know ctors didn't throw.
  return std::move(rv);
}

template <class BoundedArrayT, size_t align>
typename make_aligned_unique_result<BoundedArrayT>::no_result
make_aligned_unique(...) = delete;

}  // namespace cashew
