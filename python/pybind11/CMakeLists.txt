# pybind11 integration for FASTMM Python bindings

cmake_minimum_required(VERSION 3.15)

# Use modern Python discovery to match the interpreter cibuildwheel selected
# This avoids mismatches like including Python 3.8 headers but linking against 3.13 libs
# Use Development.Module instead of Development - only needs headers for extension modules
set(PYBIND11_FINDPYTHON ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)

# Add pybind11 (header-only)
include(FetchContent)
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG v2.12.0
)
FetchContent_MakeAvailable(pybind11)

# Include dirs provided by the Python3::Module / pybind11 targets

set(FASTMM_SRC ${PROJECT_SOURCE_DIR}/python/pybind11/fastmm_bindings.cpp)

# Build the Python extension with pybind11 helper to ensure correct Python linkage
pybind11_add_module(fastmm MODULE ${FASTMM_SRC})
set_target_properties(fastmm PROPERTIES PREFIX "" OUTPUT_NAME "fastmm")
if (WIN32)
  set_target_properties(fastmm PROPERTIES SUFFIX ".pyd")
endif()
target_link_libraries(fastmm PRIVATE pybind11::pybind11 Python3::Module)

# Link to core FASTMM library and Python
set(FASTMM_LINK_LIBS FASTMMLIB)
target_link_libraries(fastmm PRIVATE ${FASTMM_LINK_LIBS})

# Use C++17 (pybind11 3.0+ requires C++17)
set_target_properties(fastmm PROPERTIES CXX_STANDARD 17)

# Set RPATH so the extension can find libfastmmlib.so in the same directory
if(UNIX AND NOT APPLE)
    set_target_properties(fastmm PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
elseif(APPLE)
    set_target_properties(fastmm PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path"
    )
endif()

# Install Python extension (.pyd) - MODULE libraries use LIBRARY destination even on Windows
message(STATUS "Configuring install for fastmm.pyd")
install(TARGETS fastmm
    LIBRARY DESTINATION fastmm
    COMPONENT python)

# Install FASTMMLIB shared library alongside the fastmm extension
install(FILES $<TARGET_FILE:FASTMMLIB>
    DESTINATION fastmm
    COMPONENT python)

# Install Python source files
install(DIRECTORY ${PROJECT_SOURCE_DIR}/python/fastmm/
    DESTINATION fastmm
    COMPONENT python
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "*.pyi"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.egg-info" EXCLUDE)

# fastmm py.typed marker (lives at python/py.typed, ship as fastmm/py.typed)
install(FILES ${PROJECT_SOURCE_DIR}/python/py.typed
    DESTINATION fastmm
    COMPONENT python)

# --- routingkit_ch extension ---
file(GLOB RK_SRC ${PROJECT_SOURCE_DIR}/third_party/routingkit/src/*.cpp)

pybind11_add_module(routingkit_ch_native MODULE
  ${PROJECT_SOURCE_DIR}/src/python/routingkit_ch_bindings.cpp
  ${RK_SRC}
)
target_include_directories(routingkit_ch_native PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/routingkit/include
  ${PROJECT_SOURCE_DIR}/third_party/routingkit/src
)
set_target_properties(routingkit_ch_native PROPERTIES
  OUTPUT_NAME "_native"
  CXX_STANDARD 17
)
if(UNIX AND NOT APPLE)
    set_target_properties(routingkit_ch_native PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
elseif(APPLE)
    set_target_properties(routingkit_ch_native PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path"
    )
endif()
install(TARGETS routingkit_ch_native
    LIBRARY DESTINATION routingkit_ch
    COMPONENT python)

# Install routingkit_ch Python package source files
install(DIRECTORY ${PROJECT_SOURCE_DIR}/python/routingkit_ch/
    DESTINATION routingkit_ch
    COMPONENT python
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "*.pyi"
        PATTERN "py.typed"
    PATTERN "__pycache__" EXCLUDE)

# Install ch_router top-level package (thin re-export wrapper)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/python/ch_router/
    DESTINATION ch_router
    COMPONENT python
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "*.pyi"
        PATTERN "py.typed"
    PATTERN "__pycache__" EXCLUDE)