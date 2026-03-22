cmake_minimum_required(VERSION 3.15)
project(frost_cuda LANGUAGES CXX CUDA)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES "native")

# Minimal target - compiled as static library
add_library(frost_cuda STATIC frost_cuda.cu)

# Link CUDA Runtime (Static) and propagate to consumer
find_package(CUDAToolkit REQUIRED)
target_link_libraries(frost_cuda PUBLIC CUDA::cudart)

# Link cuNSearch for GPU neighbor search
target_include_directories(frost_cuda PUBLIC
    ${CMAKE_SOURCE_DIR}/../deps/cuNSearch/include
    ${CMAKE_SOURCE_DIR}/../deps/cuNSearch/Utils
)
# cuNSearch library will be linked at the adapter level since it's built there

# Define the macro expected by adapter
target_compile_definitions(frost_cuda PUBLIC FROST_ENABLE_CUDA)

