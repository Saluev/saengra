# Tests for saengra

# Find or fetch Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing with gtest
enable_testing()

add_executable(test_vertices_container
    test_vertices_container.cpp
    ${CMAKE_SOURCE_DIR}/src/vertex.cpp
    ${CMAKE_SOURCE_DIR}/src/vertices_container.cpp
    ${CMAKE_SOURCE_DIR}/src/edge.cpp
    ${CMAKE_SOURCE_DIR}/src/edges_container.cpp
    ${CMAKE_SOURCE_DIR}/src/observable.cpp
    ${CMAKE_SOURCE_DIR}/src/graph.cpp
)

add_executable(test_edges_container
    test_edges_container.cpp
    ${CMAKE_SOURCE_DIR}/src/vertex.cpp
    ${CMAKE_SOURCE_DIR}/src/vertices_container.cpp
    ${CMAKE_SOURCE_DIR}/src/edge.cpp
    ${CMAKE_SOURCE_DIR}/src/edges_container.cpp
    ${CMAKE_SOURCE_DIR}/src/observable.cpp
    ${CMAKE_SOURCE_DIR}/src/graph.cpp
)

add_executable(test_parser
    test_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/expression.cpp
    ${CMAKE_SOURCE_DIR}/src/graph.cpp
    ${CMAKE_SOURCE_DIR}/src/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/vertex.cpp
    ${CMAKE_SOURCE_DIR}/src/vertices_container.cpp
    ${CMAKE_SOURCE_DIR}/src/edge.cpp
    ${CMAKE_SOURCE_DIR}/src/edges_container.cpp
    ${BISON_ExprParser_OUTPUTS}
    ${FLEX_ExprLexer_OUTPUTS}
)

add_executable(test_observables
    test_observables.cpp
    ${CMAKE_SOURCE_DIR}/src/vertex.cpp
    ${CMAKE_SOURCE_DIR}/src/edge.cpp
    ${CMAKE_SOURCE_DIR}/src/observable.cpp
)

add_executable(test_graph
    test_graph.cpp
    ${CMAKE_SOURCE_DIR}/src/vertex.cpp
    ${CMAKE_SOURCE_DIR}/src/vertices_container.cpp
    ${CMAKE_SOURCE_DIR}/src/edge.cpp
    ${CMAKE_SOURCE_DIR}/src/edges_container.cpp
    ${CMAKE_SOURCE_DIR}/src/observable.cpp
    ${CMAKE_SOURCE_DIR}/src/graph.cpp
)

add_executable(test_start_positions
    test_start_positions.cpp
    ${CMAKE_SOURCE_DIR}/src/start_positions.cpp
    ${CMAKE_SOURCE_DIR}/src/expression.cpp
    ${CMAKE_SOURCE_DIR}/src/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/vertex.cpp
    ${CMAKE_SOURCE_DIR}/src/vertices_container.cpp
    ${CMAKE_SOURCE_DIR}/src/edge.cpp
    ${CMAKE_SOURCE_DIR}/src/edges_container.cpp
    ${CMAKE_SOURCE_DIR}/src/observable.cpp
    ${CMAKE_SOURCE_DIR}/src/graph.cpp
    ${BISON_ExprParser_OUTPUTS}
    ${FLEX_ExprLexer_OUTPUTS}
)

add_executable(test_matcher
    test_matcher.cpp
    ${CMAKE_SOURCE_DIR}/src/matcher.cpp
    ${CMAKE_SOURCE_DIR}/src/refs.cpp
    ${CMAKE_SOURCE_DIR}/src/start_positions.cpp
    ${CMAKE_SOURCE_DIR}/src/expression.cpp
    ${CMAKE_SOURCE_DIR}/src/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/vertex.cpp
    ${CMAKE_SOURCE_DIR}/src/vertices_container.cpp
    ${CMAKE_SOURCE_DIR}/src/edge.cpp
    ${CMAKE_SOURCE_DIR}/src/edges_container.cpp
    ${CMAKE_SOURCE_DIR}/src/observable.cpp
    ${CMAKE_SOURCE_DIR}/src/graph.cpp
    ${BISON_ExprParser_OUTPUTS}
    ${FLEX_ExprLexer_OUTPUTS}
)

add_executable(test_observers
    test_observers.cpp
    ${CMAKE_SOURCE_DIR}/src/observer_container.cpp
    ${CMAKE_SOURCE_DIR}/src/matcher.cpp
    ${CMAKE_SOURCE_DIR}/src/incremental_matcher.cpp
    ${CMAKE_SOURCE_DIR}/src/start_positions.cpp
    ${CMAKE_SOURCE_DIR}/src/expression.cpp
    ${CMAKE_SOURCE_DIR}/src/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/vertex.cpp
    ${CMAKE_SOURCE_DIR}/src/vertices_container.cpp
    ${CMAKE_SOURCE_DIR}/src/edge.cpp
    ${CMAKE_SOURCE_DIR}/src/edges_container.cpp
    ${CMAKE_SOURCE_DIR}/src/observable.cpp
    ${CMAKE_SOURCE_DIR}/src/graph.cpp
    ${CMAKE_SOURCE_DIR}/src/refs.cpp
    ${PROTO_SRCS}
    ${BISON_ExprParser_OUTPUTS}
    ${FLEX_ExprLexer_OUTPUTS}
)

target_include_directories(test_vertices_container PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_include_directories(test_edges_container PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_include_directories(test_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

target_include_directories(test_observables PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_include_directories(test_graph PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_include_directories(test_start_positions PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

target_include_directories(test_matcher PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

target_include_directories(test_observers PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

# Link against gtest and absl
find_package(absl REQUIRED)
target_link_libraries(test_vertices_container
    GTest::gtest
    absl::hash
)
target_link_libraries(test_edges_container
    GTest::gtest
    absl::hash
)
target_link_libraries(test_parser
    GTest::gtest
    GTest::gmock
    absl::hash
)
target_link_libraries(test_observables
    GTest::gtest
    absl::hash
)
target_link_libraries(test_graph
    GTest::gtest
    GTest::gmock
    absl::hash
)
target_link_libraries(test_start_positions
    GTest::gtest
    GTest::gmock
    absl::hash
)
target_link_libraries(test_matcher
    GTest::gtest
    GTest::gmock
    absl::hash
)
target_link_libraries(test_observers
    GTest::gtest
    GTest::gmock
    protobuf::libprotobuf
    absl::hash
    absl::log_internal_check_op
    absl::log_internal_message
    spdlog::spdlog
)

# Add test to CTest
include(GoogleTest)
gtest_discover_tests(test_vertices_container)
gtest_discover_tests(test_edges_container)
gtest_discover_tests(test_parser)
gtest_discover_tests(test_observables)
gtest_discover_tests(test_graph)
gtest_discover_tests(test_start_positions)
gtest_discover_tests(test_matcher)
gtest_discover_tests(test_observers)
