#pragma once
#include <vector>

namespace tbb_shim {
class filter {
public:
  enum mode { serial_in_order, serial_out_of_order, parallel };
  filter(bool /*is_serial*/) {}
  filter(mode /*m*/) {}
  virtual ~filter() {}
  virtual void *operator()(void *item) = 0;
};

class pipeline {
public:
  pipeline() {}
  ~pipeline() {}
  void add_filter(filter &f) { filters.push_back(&f); }
  void run(size_t) {
    if (filters.empty())
      return;
    while (true) {
      // Input filter
      void *item = (*filters[0])(nullptr);
      if (item == nullptr)
        break;

      // Subsequent filters
      for (size_t i = 1; i < filters.size(); ++i) {
        item = (*filters[i])(item);
        if (item == nullptr)
          break; // Filter consumed/discarded item
      }
    }
  }
  void clear() { filters.clear(); }

private:
  std::vector<filter *> filters;
};
} // namespace tbb_shim
