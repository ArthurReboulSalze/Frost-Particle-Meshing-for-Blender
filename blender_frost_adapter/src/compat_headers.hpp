#pragma once
#include <Imath/half.h>
using Imath::half;
#include <iostream>
// Assuming Imath::half is defined in a header like <Imath/half.h> or similar,
// which should be included for this operator to compile.
// For example: #include <Imath/half.h>

// Shim for io_service (deprecated in Boost 1.70+)
#define io_service io_context
#include <boost/asio/io_context.hpp>

// Ensure half is streamed as float
// operator<< for half is provided by half.h

// Simply include the boost header where enable_if_c is defined
// If it's missing in newer boost, then we might need the struct hack,
// but the error "template defined" suggests it exists.
#include <boost/core/enable_if.hpp>
#include <boost/utility/enable_if.hpp>
