#include "start_positions.h"
#include "graph.h"
#include "parser.h"
#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

using namespace saengra;
using testing::UnorderedElementsAre;
using testing::Contains;
using testing::SizeIs;

class StartPositionFinderTest : public ::testing::Test {
protected:
    Graph graph_;
    Parser parser_{graph_};
    StartPositionFinder finder_;

    // Helper to create updates
    Update make_add_vertex(const std::string& type_name, const std::string& value) {
        return Update{
            ProtoUpdateKind::AddVertex,
            UpdateVertex{type_name, value},
            std::nullopt,
            std::nullopt
        };
    }

    Update make_add_edge(const std::string& from_type, const std::string& from_value,
                         const std::string& label,
                         const std::string& to_type, const std::string& to_value) {
        return Update{
            ProtoUpdateKind::AddEdge,
            UpdateVertex{from_type, from_value},
            label,
            UpdateVertex{to_type, to_value}
        };
    }

    // Helper to get vertex ID
    VertexID get_vertex_id(const std::string& type_name, const std::string& value) {
        VertexTypeName tn = graph_.internalize_type_name(type_name);
        VertexData v(tn, value);
        auto id = graph_.get_vertices().get_vertex_id(v);
        EXPECT_TRUE(id.has_value()) << "VertexData not found: " << type_name << "(" << value << ")";
        return id.value_or(0);
    }

    // Helper to check if position is in results
    bool has_position(const StartPositions& result, VertexID vertex_id, PositionKind kind) {
        for (const auto& pos : result.positions) {
            if (pos.vertex_id == vertex_id && pos.kind == kind) {
                return true;
            }
        }
        return false;
    }

    // Helper to check dependencies
    template<typename T>
    bool has_dep(const StartPositions& result, const T& observable) {
        for (const auto& dep : result.deps) {
            if (auto* obs = boost::get<T>(&dep.observable)) {
                if (*obs == observable) {
                    return true;
                }
            }
        }
        return false;
    }
};

TEST_F(StartPositionFinderTest, VertexExprWithTypeName) {
    // Add some vertices to the graph
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("City", "NYC")
    });

    // Parse expression for Person vertices
    auto expr = parser_.parse("Person");
    Query query{graph_, expr, {}};

    // Find start positions
    auto result = finder_.find_start_positions(query);

    // Should have positions for both Person vertices
    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID bob_id = get_vertex_id("Person", "Bob");
    VertexID nyc_id = get_vertex_id("City", "NYC");

    EXPECT_TRUE(has_position(result, alice_id, PositionKind::CORE));
    EXPECT_TRUE(has_position(result, bob_id, PositionKind::CORE));
    EXPECT_FALSE(has_position(result, nyc_id, PositionKind::CORE));

    // Should have dependency for new Person vertices
    VertexTypeName person_type = graph_.internalize_type_name("Person");
    EXPECT_TRUE(has_dep(result, NewVertexOfType{person_type}));
}

TEST_F(StartPositionFinderTest, VertexExprWildcard) {
    // Add some vertices
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("City", "NYC")
    });

    auto expr = parser_.parse("*");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Should have positions for all vertices
    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID nyc_id = get_vertex_id("City", "NYC");

    EXPECT_TRUE(has_position(result, alice_id, PositionKind::CORE));
    EXPECT_TRUE(has_position(result, nyc_id, PositionKind::CORE));

    // Should have dependency for any new vertex
    EXPECT_TRUE(has_dep(result, NewVertex{}));
}

TEST_F(StartPositionFinderTest, EdgeExprWithLabelsForward) {
    // Add vertices and edges
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("City", "NYC"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_add_edge("Person", "Bob", "lives_in", "City", "NYC")
    });

    auto expr = parser_.parse("-knows->");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Should have ORBIT position for Alice (from vertex of knows edge)
    VertexID alice_id = get_vertex_id("Person", "Alice");
    EXPECT_TRUE(has_position(result, alice_id, PositionKind::ORBIT));

    // Should have dependency for new edges with label "knows"
    EdgeLabel knows_label = graph_.internalize_label("knows");
    EXPECT_TRUE(has_dep(result, NewEdgeWithLabel{knows_label}));
}

TEST_F(StartPositionFinderTest, EdgeExprWithLabelsBackward) {
    // Add vertices and edges
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob")
    });

    auto expr = parser_.parse("<-knows-");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Should have ORBIT position for Bob (to vertex of knows edge)
    VertexID bob_id = get_vertex_id("Person", "Bob");
    EXPECT_TRUE(has_position(result, bob_id, PositionKind::ORBIT));
}

TEST_F(StartPositionFinderTest, EdgeExprWithLabelsBoth) {
    // Add vertices and edges
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "friend", "Person", "Bob")
    });

    auto expr = parser_.parse("<-friend->");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Should have ORBIT position for both Alice and Bob
    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID bob_id = get_vertex_id("Person", "Bob");
    EXPECT_TRUE(has_position(result, alice_id, PositionKind::ORBIT));
    EXPECT_TRUE(has_position(result, bob_id, PositionKind::ORBIT));
}

TEST_F(StartPositionFinderTest, EdgeExprWithoutLabels) {
    // Add vertices and edges
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("City", "NYC"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob")
    });

    auto expr = parser_.parse("--*--");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Should have ORBIT positions for all vertices
    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID bob_id = get_vertex_id("Person", "Bob");
    VertexID nyc_id = get_vertex_id("City", "NYC");

    EXPECT_TRUE(has_position(result, alice_id, PositionKind::ORBIT));
    EXPECT_TRUE(has_position(result, bob_id, PositionKind::ORBIT));
    EXPECT_TRUE(has_position(result, nyc_id, PositionKind::ORBIT));

    // Should have dependency for any new edge
    EXPECT_TRUE(has_dep(result, NewEdge{}));
}

TEST_F(StartPositionFinderTest, OrExpr) {
    // Add vertices
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("City", "NYC")
    });

    auto expr = parser_.parse("Person | City");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Should have positions for both Person and City vertices
    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID nyc_id = get_vertex_id("City", "NYC");

    EXPECT_TRUE(has_position(result, alice_id, PositionKind::CORE));
    EXPECT_TRUE(has_position(result, nyc_id, PositionKind::CORE));

    // Should have dependencies for both types
    VertexTypeName person_type = graph_.internalize_type_name("Person");
    VertexTypeName city_type = graph_.internalize_type_name("City");
    EXPECT_TRUE(has_dep(result, NewVertexOfType{person_type}));
    EXPECT_TRUE(has_dep(result, NewVertexOfType{city_type}));
}

TEST_F(StartPositionFinderTest, ConcatenationExpr) {
    // Add vertices
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("City", "NYC")
    });

    auto expr = parser_.parse("Person -knows-> City");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Concatenation should use first operand (Person) for start positions
    VertexID alice_id = get_vertex_id("Person", "Alice");
    EXPECT_TRUE(has_position(result, alice_id, PositionKind::CORE));

    // Should not have City positions (that's for matching, not starting)
    VertexID nyc_id = get_vertex_id("City", "NYC");
    EXPECT_FALSE(has_position(result, nyc_id, PositionKind::CORE));
}

TEST_F(StartPositionFinderTest, OperationExprAll) {
    // Add vertices
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob")
    });

    auto expr = parser_.parse("(all: Person)");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Should have positions for Person vertices (operation wraps operand)
    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID bob_id = get_vertex_id("Person", "Bob");

    EXPECT_TRUE(has_position(result, alice_id, PositionKind::CORE));
    EXPECT_TRUE(has_position(result, bob_id, PositionKind::CORE));
}

TEST_F(StartPositionFinderTest, EmptyGraph) {
    // No vertices or edges in graph

    auto expr = parser_.parse("Person");
    Query query{graph_, expr, {}};

    auto result = finder_.find_start_positions(query);

    // Should have no positions
    EXPECT_THAT(result.positions, SizeIs(0));

    // But should still have dependency
    VertexTypeName person_type = graph_.internalize_type_name("Person");
    EXPECT_TRUE(has_dep(result, NewVertexOfType{person_type}));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
