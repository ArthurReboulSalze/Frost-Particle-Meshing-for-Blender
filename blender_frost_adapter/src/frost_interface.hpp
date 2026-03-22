#pragma once

#include <vector>
#include <string>
#include <tuple>
#include <memory>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

class FrostInterface {
public:
    FrostInterface();
    ~FrostInterface();

    // Set particle data from NumPy arrays
    void set_particles(
        pybind11::array_t<float> positions,
        pybind11::array_t<float> radii,
        pybind11::object velocities = pybind11::none()
    );

    // Set a single parameter
    void set_parameter(const std::string& name, pybind11::object value);
    
    // Set multiple parameters from a dictionary
    void set_parameters(pybind11::dict params);

    // Generate mesh and return (vertices, faces) tuple
    std::tuple<pybind11::array_t<float>, pybind11::array_t<int>> generate_mesh();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
