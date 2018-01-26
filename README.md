Cashew sets
===========

This is my first attempt at a cache-friendly STL set. On my CPU (Intel i5-6500),
here's a microbenchmark comparison on 30 million operations with `int32_t`
elements:

|               | `std::set` | `cashew_set` |
| ------------- | ---------: | -----------: |
| Insert asc.   |   13.2 s   |      2.4 s   |
| Insert desc.  |   12.4 s   |      2.4 s   |
| Insert random |   44.8 s   |     13.6 s   |
| Search random |   51.3 s   |     14.4 s   |

It turns out that many CPUs today go with a 64-byte cache line, so this should
provide benefits on any machine. This includes most of Intel, AMD, ARM, and IBM
Power processors. If your machine turns out to be different, there is a simple
`Traits` class you can inherit and override the value.


Usage
-----

Just `#include "cashew_set.h"` and start using like a set.  You can rerun these
microbenchmarks on your own machine by running `./cashew_set_bench.sh`. This
will give you more usage examples as well. You may need `--std=c++11` to compile
it, either on GCC or Clang. While I have not tested it on any other compiler,
would be curious to know the results.


Status
------

Just to be clear, this implementation is still very much a proof-of-concept, so
use at your own risk. For example, it only does insert and lookup, no delete.
Behavior around non-POD types and exception safety still needs to be worked out.
While I'll keep on working on it in my own free time, I'm uploading it in case
somebody finds it useful. Feel free to play around with it. If you have
questions, just use the issue tracker for now so everyone can see the answers.
