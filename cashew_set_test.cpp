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

// Make sure we get enough inserts to produce a 3-deep tree.
template <class X> int smallInsertCount() {
  int x = CashewSetTraits<X>::elt_count_max;
  x=1+x+x*(x+1);
  return x>100?x:100;
}

template <class X> void testSmallInserts() {
  int ic = smallInsertCount<X>();
  cashew_set<X> s;
  // Check if it's empty.
  assert(s.empty());
  assert(s.count(X(1))==0);

  // Start running forward.
  int prev_size = s.size();
  for(int i=1;i<=ic;++i) {
    int j = 2*i;
    assert(s.insert(X(j)));
    assert(!s.empty());
    assert(s.count(X(j))==1);
    assert(s.count(X(j+2))==0);
    assert(s.size()==++prev_size);
  }
  // Go backwards, with odd numbers.
  for(int i=ic;i>=1;--i) {
    int j = 2*i-1;
    assert(s.insert(X(j)));
    assert(!s.empty());
    assert(s.count(X(j))==1);
    assert(s.count(X(j-2))==0);
    assert(s.size()==++prev_size);
  }

  // Insert duplicates.
  assert(!s.insert(X(1)));
  assert(!s.insert(X(10)));
  assert(!s.insert(X(100)));
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

struct IntLifeCount {
  static int born;
  static int died;
  int32_t x;
  IntLifeCount() = delete;
  IntLifeCount(const IntLifeCount& that) : x(that.x) { born++;}
  IntLifeCount(IntLifeCount&& that) : x(that.x) { born++; }
  explicit IntLifeCount(int32_t x) : x(x) { born++; }
  ~IntLifeCount() { died++; }
  IntLifeCount& operator=(const IntLifeCount&) = default;
};
int IntLifeCount::born = 0;
int IntLifeCount::died = 0;
bool operator<(const IntLifeCount& a,const IntLifeCount& b) {
  return a.x<b.x;
}
bool operator==(const IntLifeCount& a,const IntLifeCount& b) {
  return a.x==b.x;
}
// Test if custom types get destructors invoked.
void testDtorInvocation() {
  assert(IntLifeCount::born == 0);
  assert(IntLifeCount::died == 0);
  testSmallInserts<IntLifeCount>();
  assert(IntLifeCount::born == IntLifeCount::died);
  {
    cashew_set<IntLifeCount> s;
    s.insert(IntLifeCount(5));
    s.clear();
  }
  assert(IntLifeCount::born == IntLifeCount::died);
}

int main() {
  testNodeAlignment();
  testSmallInserts<uint32_t>();
  testNoDefaultConstructor();
  testDtorInvocation();
}
