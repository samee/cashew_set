// Testing C++17 aligned_new. Right now, on g++ 7.2, this needs
// -faligned-new flag.

#include<new>
#include<memory>
#include<iostream>
using namespace std;

constexpr int goodAlign = 64;

template <class T>
struct alignas(goodAlign) MyStuff {
  unique_ptr<int> non_trivial_dtor;
  char blah;
  T foo[13];
};

static_assert(sizeof(MyStuff<int>) <= goodAlign, "Make struct MyStuff smaller");

int main() {
  auto p = make_unique<MyStuff<int>[]>(10);
  bool aligned = ((reinterpret_cast<ptrdiff_t>(p.get()) & (goodAlign-1)) == 0);
  cout << (aligned?"Aligned":"Not aligned") << endl;
  cout << "alignof(MyStuff<int>) = " << alignof(MyStuff<int>) << endl;
}
