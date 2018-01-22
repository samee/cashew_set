// Compiler bugs require that we compile one or the other,
// not both.
//   * For std::set, we use:
//     clang++ -O3 --std=c++11 cashew_set_bench.cpp -DBENCH_STD
//   * For cashew_set, we use:
//     clang++ -O3 --std=c++1z cashew_set_bench.cpp -DBENCH_CASHEW
// Someday, if we see that defining both doesn't return an error,
// we can remove these preprocessor guards.

#ifdef BENCH_CASHEW
#include "cashew_set.h"
using namespace cashew;
#endif

#ifdef BENCH_STD
#include <set>
#endif

#ifdef BENCH_GNU_MT_ALLOC
#include <set>
#include <ext/mt_allocator.h>
using namespace __gnu_cxx;
#endif

#include <algorithm>
#include <ctime>
#include <iostream>
#include <vector>
using namespace std;

double wallClock() {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME,&t);
  return t.tv_sec+1e-9*t.tv_nsec;
}

template <class IntSet> void timeOps() {
  const int size=30000000;
  int i;
  IntSet s;
  double start = wallClock();
  for(i=0;i<size;++i) s.insert(i);
  cout<<"Inserted "<<size<<" elements in ascending order: "
      <<wallClock()-start<<" sec"<<endl;

  s.clear();
  start = wallClock();
  for(i=size;i>0;--i) s.insert(i);
  cout<<"Inserted "<<size<<" elements in descending order: "
      <<wallClock()-start<<" sec"<<endl;

  s.clear();
  vector<typename IntSet::value_type> v(size);
  for(i=0;i<v.size();++i) v[i]=i;
  random_shuffle(v.begin(),v.end());
  start = wallClock();
  for(i=0;i<size;++i) s.insert(v[i]*2); // insert even numbers.
  cout<<"Inserted "<<size<<" elements in random order: "
      <<wallClock()-start<<" sec"<<endl;

  int count=0;
  random_shuffle(v.begin(),v.end());
  start = wallClock();
  for(i=0;i<size;++i) count+=s.count(v[i]*2);
  cout<<"Searched "<<size<<" elements in random order, found " <<count<<": "
      <<wallClock()-start<<" sec"<<endl;

  count=0;
  random_shuffle(v.begin(),v.end());
  start = wallClock();
  for(i=0;i<size;++i) count+=s.count(v[i]*2+1); // So I won't find odd numbers.
  cout<<"Searched "<<size<<" elements in random order, found " <<count<<": "
      <<wallClock()-start<<" sec"<<endl;
}

int main() {
#ifdef BENCH_CASHEW
  timeOps<cashew_set<int32_t>>();
#endif
#ifdef BENCH_STD
  timeOps<set<int32_t>>();
#endif
#ifdef BENCH_GNU_MT_ALLOC
  timeOps<set<int32_t, less<int32_t>, __mt_alloc<int32_t>>>();
#endif
}
