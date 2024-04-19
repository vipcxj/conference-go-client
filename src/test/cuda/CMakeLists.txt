project(cfgo-cuda C CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(cuda-api-wrappers CONFIG REQUIRED)

set(SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(cfgo-cuda "${SRC_PATH}/gst-cu.cu" "${SRC_PATH}/gst-cu.cuh")
target_include_directories(cfgo-cuda PRIVATE "${SRC_PATH}")
#set_target_properties(cfgo-cuda PROPERTIES CUDA_ARCHITECTURES "89")
target_link_libraries(cfgo-cuda PRIVATE CUDA::cudart)
target_link_libraries(cfgo-cuda PRIVATE cuda-api-wrappers::runtime-and-driver)