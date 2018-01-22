#!/bin/bash
set -x
clang++ -O3 --std=c++11 cashew_set_bench.cpp -DBENCH_STD  && ./a.out
clang++ -O3 --std=c++11 cashew_set_bench.cpp -DBENCH_CASHEW && ./a.out
