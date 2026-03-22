#pragma once
#include <thread>
namespace tbb {
using tbb_thread = std::thread;
namespace this_tbb_thread {
using std::this_thread::get_id;
using std::this_thread::sleep_for;
using std::this_thread::yield;
} // namespace this_tbb_thread
} // namespace tbb
