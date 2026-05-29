cmake_minimum_required( VERSION 3.15)

message(STATUS "CMAKE version ${CMAKE_VERSION}")

if (POLICY CMP0074)
    message(STATUS "Set CMP0074 state to NEW")
    cmake_policy(SET CMP0074 NEW)
endif()

if (POLICY CMP0086)
    message(STATUS "Set CMP0086 state to NEW")
    cmake_policy(SET CMP0086 NEW)
endif()

if (POLICY CMP0078)
    message(STATUS "Set CMP0078 state to NEW")
    cmake_policy(SET CMP0078 NEW)
endif()

# Prevent in source build
set(CMAKE_DISABLE_SOURCE_CHANGES  OFF)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(fastmm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")

# Use -O3 for release builds (Linux/macOS toolchains only; Windows is unsupported)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Boost 1.82's qvm headers (pulled in by boost::geometry) use the `.template`
# disambiguator in a way that Clang 18 / Apple Clang 17 reject by default.
# Suppress just that diagnostic on Clang; GCC does not have the flag.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-missing-template-arg-list-after-template-kw)
endif()
set(CMAKE_CXX_STANDARD 17)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(Conda)
if(CONDA_PREFIX)
  message(STATUS "Set CONDA_PREFIX ${CONDA_PREFIX}")
  set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CONDA_PREFIX})
else()
  message(STATUS "Non conda exist, search library in default path")
endif()

find_package(Boost 1.56.0 REQUIRED serialization)
if (Boost_FOUND)
  message(STATUS "Boost headers found at ${Boost_INCLUDE_DIR}")
  message(STATUS "Boost library found at ${Boost_LIBRARIES}")
  message(STATUS "Boost library version ${Boost_LIB_VERSION}")
else()
  message(FATAL_ERROR "Boost Not Found!")
endif (Boost_FOUND)


find_package(OpenMP REQUIRED)
if(OPENMP_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
  message(STATUS "OpenMP_HEADERS found at ${OpenMP_CXX_INCLUDE_DIRS}")
  message(STATUS "OpenMP_CXX_LIBRARIES found at ${OpenMP_CXX_LIBRARIES}")
  if(OpenMP_CXX_INCLUDE_DIRS)
    include_directories(${OpenMP_CXX_INCLUDE_DIRS})
  endif()
endif()

### Set RPATH properties

set(CMAKE_SKIP_BUILD_RPATH FALSE)

set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Creation of FASTMMLIB

set(FASTMM_INCLUDE_PATH
${CMAKE_CURRENT_SOURCE_DIR}/src
${CMAKE_CURRENT_SOURCE_DIR}/third_party)

include_directories(${FASTMM_INCLUDE_PATH})

file(GLOB CoreGlob CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB AlgorithmGlob CONFIGURE_DEPENDS src/algorithm/*.cpp)
file(GLOB NetworkGlob CONFIGURE_DEPENDS src/network/*.cpp)
file(GLOB UtilGlob CONFIGURE_DEPENDS src/util/*.cpp)
file(GLOB MMGlob CONFIGURE_DEPENDS src/mm/*.cpp)
file(GLOB FMMGlob CONFIGURE_DEPENDS src/mm/fmm/*.cpp)

add_library(CORE OBJECT ${CoreGlob})
add_library(ALGORITHM OBJECT ${AlgorithmGlob})
add_library(UTIL OBJECT ${UtilGlob})
add_library(NETWORK OBJECT ${NetworkGlob})
add_library(MM_OBJ OBJECT ${MMGlob})
add_library(FMM_OBJ OBJECT ${FMMGlob})

add_library(FASTMMLIB SHARED
  $<TARGET_OBJECTS:MM_OBJ>
  $<TARGET_OBJECTS:FMM_OBJ>
  $<TARGET_OBJECTS:CORE>
  $<TARGET_OBJECTS:ALGORITHM>
  $<TARGET_OBJECTS:UTIL>
  $<TARGET_OBJECTS:NETWORK>)

# Link Boost and OpenMP into FASTMMLIB only (these were previously linked
# globally via link_libraries(), which leaked them into routingkit_ch_native
# which uses neither). OpenMP is used only in src/mm/fmm/ubodt_gen_algorithm.cpp.
target_link_libraries(FASTMMLIB PUBLIC ${Boost_LIBRARIES} ${OpenMP_CXX_LIBRARIES})

# Set the output name to lowercase for consistency
set_target_properties(FASTMMLIB PROPERTIES OUTPUT_NAME "fastmmlib")

message(STATUS "Installation folder ${CMAKE_INSTALL_PREFIX}")
message(STATUS "FASTMMLIB target type: $<TARGET_PROPERTY:FASTMMLIB,TYPE>")

# FASTMMLIB installation is handled in python/pybind11/CMakeLists.txt with the python component

add_subdirectory(python/pybind11)
