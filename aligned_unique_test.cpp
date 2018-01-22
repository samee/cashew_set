// Testing C11 aligned_alloc. Seems to work well at g++ 7.2.0 and
// clang++ 4.0.1-6. Let's use this instead of aligned new for now, until those
// features stabilize.
// g++ aligned_alloc.cpp  # works with no special flags
// clang++ --std=c++1y aligned_alloc.cpp  # c++1z is still a bugfest

#include "aligned_unique.h"

#include<new>
#include<iostream>
#include<memory>
#include<type_traits>
using namespace cashew;
using namespace std;

constexpr int goodAlign = 64;

template <class T>
struct MyStuff {
  unique_ptr<int> non_trivial_dtor;
  char blah;
  T foo[13];
};

static_assert(sizeof(MyStuff<int>) <= goodAlign, "Make struct MyStuff smaller");

int main() {
  auto p = make_aligned_unique<MyStuff<int>,goodAlign>();
  bool aligned = (((reinterpret_cast<ptrdiff_t>(p.get())) & (goodAlign-1)) == 0);
  cout << (aligned?"Aligned":"Not aligned") << endl;
  cout << "sizeof(aligned_unique_ptr) = " << sizeof(p) << endl;
}
