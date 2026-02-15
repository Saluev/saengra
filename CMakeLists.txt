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

if(UNIX AND NOT APPLE)
    target_link_libraries(c_extension PRIVATE
        -Wl,--start-group
        saengra_core
        protobuf::libprotobuf
        absl::base
        absl::cord
        absl::log_internal_check_op
        absl::log_internal_message
        absl::raw_hash_set
        absl::status
        absl::statusor
        absl::strings
        -Wl,--end-group
    )
else()
    target_link_libraries(c_extension PRIVATE
        saengra_core
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
