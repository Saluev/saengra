cmake_minimum_required(VERSION 3.15)
project(saengra VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add sanitizers for debugging
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g")

# Find dependencies
find_package(Protobuf REQUIRED)
find_package(absl REQUIRED)
find_package(Boost REQUIRED)
find_package(spdlog REQUIRED)

# Use Homebrew Bison instead of system Bison (unless building in Docker)
if(NOT DEFINED ENV{AVOID_HOMEBREW})
    set(BISON_EXECUTABLE "/opt/homebrew/opt/bison/bin/bison")
endif()
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# Generate protobuf sources
set(PROTO_FILES
    proto/messages.proto
)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Generate parser sources
BISON_TARGET(ExprParser parser/parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.cpp
    DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.h)
FLEX_TARGET(ExprLexer parser/lexer.l ${CMAKE_CURRENT_BINARY_DIR}/lexer.yy.cpp)
ADD_FLEX_BISON_DEPENDENCY(ExprLexer ExprParser)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated protobuf headers
    ${PROTOBUF_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

# Core static library (everything except main.cpp and unix_socket_server.cpp)
add_library(saengra_core STATIC
    src/worker.cpp
    src/vertex.cpp
    src/vertices_container.cpp
    src/edge.cpp
    src/edges_container.cpp
    src/expression.cpp
    src/parser.cpp
    src/observable.cpp
    src/observer_container.cpp
    src/graph.cpp
    src/start_positions.cpp
    src/refs.cpp
    src/matcher.cpp
    src/incremental_matcher.cpp
    ${PROTO_SRCS}
    ${BISON_ExprParser_OUTPUTS}
    ${FLEX_ExprLexer_OUTPUTS}
)

set_target_properties(saengra_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(saengra_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(saengra_core PUBLIC
    protobuf::libprotobuf
    absl::log_internal_check_op
    absl::log_internal_message
    spdlog::spdlog
)

# Server executable
add_executable(saengra_server
    src/main.cpp
    src/unix_socket_server.cpp
)

target_link_libraries(saengra_server
    saengra_core
)

# Enable testing
enable_testing()
# add_subdirectory(tests)
