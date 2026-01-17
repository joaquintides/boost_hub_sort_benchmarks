/* Benchmark of sort algorithms for boost::hub.
 * 
 * Copyright 2026 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>

std::chrono::high_resolution_clock::time_point measure_start, measure_pause;

template<typename F>
double measure(F f)
{
  using namespace std::chrono;

  static const int              num_trials = 10;
  static const milliseconds     min_time_per_trial(200);
  std::array<double,num_trials> trials;

  for(int i = 0; i < num_trials; ++i) {
    int                               runs = 0;
    high_resolution_clock::time_point t2;
    volatile decltype(f())            res; /* to avoid optimizing f() away */

    measure_start = high_resolution_clock::now();
    do{
      res = f();
      ++runs;
      t2 = high_resolution_clock::now();
    }while(t2 - measure_start<min_time_per_trial);
    trials[i] =
      duration_cast<duration<double>>(t2 - measure_start).count() / runs;
  }

  std::sort(trials.begin(), trials.end());
  return std::accumulate(
    trials.begin() + 2, trials.end() - 2, 0.0)/(trials.size() - 4);
}

void pause_timing()
{
  measure_pause = std::chrono::high_resolution_clock::now();
}

void resume_timing()
{
  measure_start += std::chrono::high_resolution_clock::now() - measure_pause;
}

#include <boost/core/detail/splitmix64.hpp>
#include <boost/hub.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <plf_hive.h>
#include <string>
#include <vector>

struct element
{
  element(int n_): n{n_} {}

#if defined(NONTRIVIAL_ELEMENT)
  element(element&& x): n{x.n}
  {
    std::memcpy(payload, x.payload, sizeof(payload));
    std::memset(x.payload, 0, sizeof(payload));
  }

  element& operator=(element&& x)
  {
    n = x.n;
    std::memcpy(payload, x.payload, sizeof(payload));
    std::memset(x.payload, 0, sizeof(payload));
    return *this;
  }
#endif

  operator int() const { return n; }

  int n;
  char payload[ELEMENT_SIZE - sizeof(int)];
};

template<typename Container>
Container make(std::size_t n, double erasure_rate)
{
  std::size_t   m = (std::size_t)((double)n / (1.0 - erasure_rate));
  std::uint64_t erasure_cut = 
    (std::uint64_t)(erasure_rate * (double)(std::uint64_t)(-1));

  Container                                 c;
  boost::detail::splitmix64                 rng;
  for(std::size_t i = 0; i < m; ++i) c.insert((int)rng());
  erase_if(c, [&] (const auto&){
    return rng() < erasure_cut;
  });
  return c;
}

void print_winner(double th, std::initializer_list<double> il)
{
  auto it_min = std::min_element(il.begin(), il.end());
  double next_min = std::numeric_limits<double>::max();
  for(auto it = il.begin(); it != il.end(); ++ it) {
    if(it == it_min) continue;
    next_min = (std::min)(next_min, *it);
  }
  std::cout << (it_min - il.begin()) + 1
            << std::fixed << std::setprecision(2)
            << " (" << next_min / *it_min << "x, "
            << th / *it_min << "x)  "
            << std::defaultfloat << std::setprecision(6);
}

int main()
{
  static constexpr std::size_t size_limit =
    sizeof(std::size_t) == 4?  800ull * 1024ull * 1024ull:
                              2048ull * 1024ull * 1024ull;

  using hub = boost::hub<element>;
  using hive = plf::hive<element>;

  std::cout << "sizeof(element): " << sizeof(element) << ", ";
#if defined(NONTRIVIAL_ELEMENT)
  std::cout  << "non-trivial movement\n";
#else
  std::cout << "trivial movement\n";
#endif
  std::cout << "n (ax, bx): alg #n wins, ax faster than alternatives, bx faster than plf::hive\n"
            << std::string(99, '-') << "\n"
            << std::left << std::setw(11) << "" << "container size\n" << std::right
            << std::left << std::setw(11) << "erase rate" << std::right;
  for(std::size_t i = 3; i <= 7; ++i)
  {
    std::cout << "1.E" << i << "              ";
  }
  std::cout << std::endl;
  for(double erasure_rate = 0.0; erasure_rate <= 0.9; erasure_rate += 0.1) {
    std::cout << std::left << std::setw(11) << erasure_rate << std::right << std::flush;
    for(std::size_t i = 3; i <= 7; ++i) {
      std::size_t n = (std::size_t)std::pow(10.0, (double)i);

      auto sort_hive = [&] {
        pause_timing();
        auto c = make<hive>(n, erasure_rate);
        resume_timing();
        c.sort();
        return c.size();
      };
      auto sort1 = [&] {
        pause_timing();
        auto c = make<hub>(n, erasure_rate);
        resume_timing();
        c.sort();
        return c.size();
      };
      auto sort2 = [&] {
        pause_timing();
        auto c = make<hub>(n, erasure_rate);
        resume_timing();
        c.sort2();
        return c.size();
      };
      auto sort3 = [&] {
        pause_timing();
        auto c = make<hub>(n, erasure_rate);
        resume_timing();
        c.sort3();
        return c.size();
      };
      auto sort4 = [&] {
        pause_timing();
        auto c = make<hub>(n, erasure_rate);
        resume_timing();
        c.sort4();
        return c.size();
      };

      if((double)n * (double)sizeof(element) / (1.0 - erasure_rate) > (double)size_limit) {
        std::cout << "too large         " << std::flush;
        continue;
      }

      try{
        auto th = measure(sort_hive);
        auto t1 = measure(sort1);
        auto t2 = measure(sort2);

        if constexpr(sizeof(element) <= 2 * sizeof(std::size_t)) {
          auto t3 = measure(sort3);
          auto t4 = measure(sort4);
          print_winner(th, {t1, t2, t3, t4});
        }
        else{
          (void)sort3;
          (void)sort4;
          print_winner(th, {t1, t2});
        }
        std::cout << std::flush;
      }
      catch(const std::bad_alloc&) {
        std::cout << "out of memory     " << std::flush;
      }
    }
    std::cout << std::endl;
  }
}
