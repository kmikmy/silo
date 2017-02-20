#include <cstdint>
#include <atomic>
#include "include/db.hpp"
#include "include/worker.hpp"
#include "include/rdtsc.hpp"

hpcs::DB hpcs::Worker::database;
std::atomic<uint64_t> hpcs::Worker::entireDurableTid;
std::atomic<uint64_t> hpcs::Worker::entireMaxLocalTid;
volatile bool hpcs::util::TS::start = false;
uint64_t hpcs::util::TS::diff_ts[hpcs::util::MAX_N_THREAD] = {0};
bool hpcs::Epoch::epoch_flag = false;
uint64_t hpcs::Epoch::epoch_us = 0;
std::atomic<uint32_t> hpcs::Epoch::globalEpoch = 0;
std::atomic<uint32_t> hpcs::Epoch::durableEpoch = 0;
