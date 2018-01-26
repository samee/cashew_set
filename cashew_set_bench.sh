#!/bin/bash
set -x
g++ -O3 --std=c++11 cashew_set_bench.cpp -DBENCH_CASHEW && ./a.out
g++ -O3 --std=c++11 cashew_set_bench.cpp -DBENCH_GNU_MT_ALLOC && ./a.out
g++ -O3 --std=c++11 cashew_set_bench.cpp -DBENCH_STD  && ./a.out
