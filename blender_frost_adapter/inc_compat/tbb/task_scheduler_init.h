#pragma once

namespace tbb {
class task_scheduler_init {
public:
  static const int automatic = -1;
  static int default_num_threads() { return -1; }
  task_scheduler_init(int = automatic) {}
  void initialize(int = automatic) {}
  void terminate() {}
  bool is_active() const { return true; }
};
} // namespace tbb
