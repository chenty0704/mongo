cmake_minimum_required(VERSION 3.16)
project(mongo)

# Update CMake module path.
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(CMakePrintHelpers)
include(SConsHelpers)

# Set compiler options.
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS -w)
set(CMAKE_CXX_FLAGS -w)

# Set SCons options and variables.
set(SCONS_OPTIONS
        --build-dir=${PROJECT_BINARY_DIR}
        --cxx-std=${CMAKE_CXX_STANDARD}
        --disable-warnings-as-errors)
set(SCONS_VARIABLES
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CCFLAGS=${CMAKE_C_FLAGS}
        CXXFLAGS=${CMAKE_CXX_FLAGS}
        MONGO_VERSION=4.4.0-ec)
if (CMAKE_BUILD_TYPE STREQUAL Debug)
    list(APPEND SCONS_OPTIONS --dbg=on)
elseif (CMAKE_BUILD_TYPE STREQUAL Release)
    list(APPEND SCONS_OPTIONS --release)
endif ()

# Import top-level SCons targets.
import_scons_target(clean_all --clean)
import_scons_target(build_all install-all)
import_scons_target(compiledb compiledb)
import_scons_target(mongod install-mongod)
import_scons_target(mongos install-mongos)
import_scons_target(mongo install-mongo)
import_scons_target(unittests install-unittests)
import_scons_target(integration_tests install-integration-tests)

# Workaround for code analysis.
include_directories(src)
