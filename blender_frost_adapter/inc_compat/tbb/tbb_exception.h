#pragma once
#include <stdexcept>
namespace tbb {
class tbb_exception : public std::exception {
public:
  virtual const char *what() const throw() { return "tbb_exception"; }
};
using captured_exception = std::exception; // Minimal shim
} // namespace tbb
