#include <iostream>
#include <vector>

#include <boost/pool/pool_alloc.hpp>

using namespace boost;
using namespace std;

int main() {
  {
    vector<int, pool_allocator<int>> v;
    for(int i=0;i<10000;++i) v.push_back(13);
    cout << v.size() << endl;
  }
}
