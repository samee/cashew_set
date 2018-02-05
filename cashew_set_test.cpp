#include "aligned_unique.h"
#include "cashew_set.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
using namespace cashew;
using namespace std;

using intSet = cashew_set<int32_t>;

void testNodeAlignment() {
  using traits_type = CashewSetTraits<int>;
  using node_type = CashewSetNode<int,traits_type>;
  auto p = make_aligned_unique<node_type[],traits_type::cache_line_nbytes>(10);
  assert((ptrdiff_t(p.get()) & (CashewSetTraits<int>::cache_line_nbytes-1))
      == 0);
}

void testSmallInserts() {
  intSet s;
  // Check if it's empty.
  assert(s.empty());
  assert(s.count(1)==0);

  // Start running.
  for(int i=1;i<=100;++i) {
    assert(s.insert(i));
    assert(!s.empty());
    assert(s.count(i)==1);
    assert(s.count(i+1)==0);
    assert(s.size()==i);
  }

  // Insert duplicates.
  assert(!s.insert(1));
  assert(!s.insert(10));
  assert(!s.insert(100));
}

void testRandomInserts() {
  vector<int> v(100000);
  for(int i=0;i<v.size();++i) v[i]=i;
  random_shuffle(v.begin(),v.end());

  intSet s;
  for(int x:v) {
    assert(s.count(x)==0);
    assert(s.insert(x));
    assert(s.count(x)==1);
  }
  reverse(v.begin(),v.end());
  for(int x:v) assert(s.count(x)==1);
  assert(s.count(200000)==0);
}

struct IntNoDefaultCtor {
  int32_t x;
  IntNoDefaultCtor() = delete;
  explicit IntNoDefaultCtor(int32_t x) : x(x) {}
};
bool operator<(const IntNoDefaultCtor& a,const IntNoDefaultCtor& b) {
  return a.x<b.x;
}
bool operator==(const IntNoDefaultCtor& a,const IntNoDefaultCtor& b) {
  return a.x==b.x;
}
// This test checks compile-time properties.
void testNoDefaultConstructor() {
  cashew_set<IntNoDefaultCtor> s;
  s.insert(IntNoDefaultCtor(4));
  assert(s.count(IntNoDefaultCtor(4))==1);
  assert(s.count(IntNoDefaultCtor(5))==0);
}

int main() {
  testNodeAlignment();
  testSmallInserts();
  testNoDefaultConstructor();
}
