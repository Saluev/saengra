cmake_minimum_required(VERSION 3.15)
project(saengra VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find Python
find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)

# Find dependencies (needed to resolve transitive deps when linking the shared module)
find_package(absl REQUIRED)
find_package(Protobuf REQUIRED)

# Build saengra_core from saengra-server (exclude saengra_server executable)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/saengra-server ${CMAKE_CURRENT_BINARY_DIR}/saengra-server-build EXCLUDE_FROM_ALL)

# Create the Python extension module
Python_add_library(c_extension MODULE
    src/adapter.cpp
)

option(SAENGRA_STATIC_DEPS "Statically link all deps into the extension (for wheel builds)" OFF)

if(SAENGRA_STATIC_DEPS)
    # For manylinux wheel builds: bundle all symbols from protobuf/abseil
    # into the .so so it has no runtime dependencies on them.
    file(GLOB ABSL_STATIC_LIBS "/usr/local/lib/libabsl_*.a" "/usr/local/lib64/libabsl_*.a")
    find_library(PROTOBUF_STATIC_LIB libprotobuf.a REQUIRED)
    find_library(UTF8_VALIDITY_LIB libutf8_validity.a)
    find_library(UTF8_RANGE_LIB libutf8_range.a)
    target_link_libraries(c_extension PRIVATE
        -Wl,--whole-archive
        saengra_core
        ${PROTOBUF_STATIC_LIB}
        $<$<BOOL:${UTF8_VALIDITY_LIB}>:${UTF8_VALIDITY_LIB}>
        $<$<BOOL:${UTF8_RANGE_LIB}>:${UTF8_RANGE_LIB}>
        ${ABSL_STATIC_LIBS}
        -Wl,--no-whole-archive
        -lz
    )
else()
    target_link_libraries(c_extension PRIVATE
        saengra_core
        absl::raw_hash_set
    )
endif()

# Set output name and location
set_target_properties(c_extension PROPERTIES
    PREFIX ""
    OUTPUT_NAME "c_extension"
)

if(DEFINED CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set_target_properties(c_extension PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
else()
    set_target_properties(c_extension PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/saengra"
    )
endif()

install(TARGETS c_extension
    LIBRARY DESTINATION saengra
)
