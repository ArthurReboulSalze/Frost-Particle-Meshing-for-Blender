#include "frost_interface.hpp"
#include <pybind11/pybind11.h>
#include <tbb/global_control.h>
#include <thread>

namespace py = pybind11;

// Force TBB to use all available hardware threads
// This persistent object ensures TBB uses all 24 threads for the entire module
// lifetime
static std::unique_ptr<tbb::global_control> g_tbb_thread_control;

PYBIND11_MODULE(blender_frost_adapter, m) {
  m.doc() = "Frost Blender Adapter";

  // Initialize TBB to use ALL available hardware threads
  // Without this, TBB defaults to a conservative thread count
  unsigned int num_threads = std::thread::hardware_concurrency();
  if (num_threads == 0)
    num_threads = 24; // Fallback for user's 12c/24t CPU

  g_tbb_thread_control = std::make_unique<tbb::global_control>(
      tbb::global_control::max_allowed_parallelism, num_threads);

  m.attr("tbb_thread_count") = num_threads; // Expose for debugging

  py::class_<FrostInterface>(m, "FrostInterface")
      .def(py::init<>())
      .def("set_particles", &FrostInterface::set_particles, "Set particle data",
           py::arg("positions"), py::arg("radii"),
           py::arg("velocities") = py::none())
      .def("set_parameter", &FrostInterface::set_parameter,
           "Set a single parameter")
      .def("set_parameters", &FrostInterface::set_parameters,
           "Set multiple parameters")
      .def("generate_mesh", &FrostInterface::generate_mesh,
           "Generate mesh from particles");
}
