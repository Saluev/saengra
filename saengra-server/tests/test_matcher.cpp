#include "matcher.h"
#include "start_positions.h"
#include "graph.h"
#include "parser.h"
#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>
#include <set>

using namespace saengra;
using testing::UnorderedElementsAre;
using testing::Contains;
using testing::SizeIs;

class MatcherTest : public ::testing::Test {
protected:
    Graph graph_;
    Parser parser_{graph_};
    StartPositionFinder finder_;
    Matcher matcher_;

    // Helper to create updates
    Update make_add_vertex(const std::string& type_name, const std::string& value) {
        return Update{
            ProtoUpdateKind::AddVertex,
            UpdateVertex{type_name, value},
            std::nullopt,
            std::nullopt
        };
    }

    Update make_remove_vertex(const std::string& type_name, const std::string& value) {
        return Update{
            ProtoUpdateKind::RemoveVertex,
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

    // Helper to check if a subgraph contains a vertex
    bool has_vertex(const Subgraph& sg, VertexID vid) {
        for (VertexID v : sg.vertices) {
            if (v == vid) return true;
        }
        return false;
    }

    // Helper to check if a subgraph contains an edge
    bool has_edge(const Subgraph& sg, VertexID from, const std::string& label, VertexID to) {
        EdgeLabel lbl = graph_.internalize_label(label);
        for (const Edge& e : sg.edges) {
            if (e.from == from && e.label == lbl && e.to == to) return true;
        }
        return false;
    }

    // Helper to count subgraphs
    size_t count_subgraphs(const QuerySet& qs) {
        return qs.subgraphs.size();
    }
};

TEST_F(MatcherTest, MatchSimpleVertex) {
    // Add vertices
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("City", "NYC")
    });

    auto expr = parser_.parse("Person");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should have 2 subgraphs (one for each Person)
    EXPECT_EQ(count_subgraphs(result), 2);

    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID bob_id = get_vertex_id("Person", "Bob");

    // Each subgraph should contain one Person vertex
    for (const Subgraph& sg : result.subgraphs) {
        EXPECT_EQ(sg.vertices.size(), 1);
        bool is_alice = has_vertex(sg, alice_id);
        bool is_bob = has_vertex(sg, bob_id);
        EXPECT_TRUE(is_alice || is_bob);
    }
}

TEST_F(MatcherTest, MatchDeletedVertex) {
    auto expr = parser_.parse("*");
    Query query{graph_, expr, {}};

    graph_.update({make_add_vertex("Person", "Alice")});
    graph_.apply();
    auto start_positions = finder_.find_start_positions(query);
    EXPECT_EQ(start_positions.positions.size(), 1);

    graph_.update({make_remove_vertex("Person", "Alice")});

    {
        auto result = matcher_.match(query, start_positions);
        EXPECT_EQ(count_subgraphs(result), 0);
    }

    graph_.apply();

    {
        auto result = matcher_.match(query, start_positions);
        EXPECT_EQ(count_subgraphs(result), 0);
    }
}

TEST_F(MatcherTest, MatchWildcard) {
    // Add vertices of different types
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("City", "NYC"),
        make_add_vertex("Country", "USA")
    });

    auto expr = parser_.parse("*");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should match all 3 vertices
    EXPECT_EQ(count_subgraphs(result), 3);
}

TEST_F(MatcherTest, MatchEdgeForward) {
    // Create graph: Alice -knows-> Bob
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob")
    });

    auto expr = parser_.parse("-knows->");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should have 1 subgraph
    EXPECT_EQ(count_subgraphs(result), 1);

    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID bob_id = get_vertex_id("Person", "Bob");

    const Subgraph& sg = result.subgraphs[0];
    EXPECT_TRUE(has_edge(sg, alice_id, "knows", bob_id));
    // Edge subgraphs may have empty vertices vector - vertices are implicit from edges
}

TEST_F(MatcherTest, MatchEdgeBackward) {
    // Create graph: Alice -knows-> Bob
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob")
    });

    auto expr = parser_.parse("<-knows-");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should have 1 subgraph (matching from Bob's perspective)
    EXPECT_EQ(count_subgraphs(result), 1);

    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID bob_id = get_vertex_id("Person", "Bob");

    const Subgraph& sg = result.subgraphs[0];
    EXPECT_TRUE(has_edge(sg, alice_id, "knows", bob_id));
}

TEST_F(MatcherTest, MatchEdgeBidirectional) {
    // Create bidirectional edge: Alice <-friend-> Bob
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "friend", "Person", "Bob"),
        make_add_edge("Person", "Bob", "friend", "Person", "Alice")
    });

    auto expr = parser_.parse("<-friend->");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // With bidirectional edges between Alice and Bob, we get matches from both perspectives
    // The exact count depends on implementation details of how Both direction is handled
    EXPECT_GE(count_subgraphs(result), 2);

    // Each bidirectional match should have both forward and reverse edges
    bool found_dual_edge = false;
    for (const Subgraph& sg : result.subgraphs) {
        if (sg.edges.size() == 2) {
            found_dual_edge = true;
        }
    }
    EXPECT_TRUE(found_dual_edge);
}

TEST_F(MatcherTest, MatchConcatenation) {
    // Create chain: Alice -knows-> Bob -lives_in-> NYC
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("City", "NYC"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_add_edge("Person", "Bob", "lives_in", "City", "NYC")
    });

    auto expr = parser_.parse("Person -knows-> Person -lives_in-> City");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should have 1 subgraph (Alice -> Bob -> NYC)
    EXPECT_EQ(count_subgraphs(result), 1);

    VertexID alice_id = get_vertex_id("Person", "Alice");
    VertexID bob_id = get_vertex_id("Person", "Bob");
    VertexID nyc_id = get_vertex_id("City", "NYC");

    const Subgraph& sg = result.subgraphs[0];
    EXPECT_TRUE(has_vertex(sg, alice_id));
    EXPECT_TRUE(has_vertex(sg, bob_id));
    EXPECT_TRUE(has_vertex(sg, nyc_id));
    EXPECT_TRUE(has_edge(sg, alice_id, "knows", bob_id));
    EXPECT_TRUE(has_edge(sg, bob_id, "lives_in", nyc_id));
}

TEST_F(MatcherTest, MatchOr) {
    // Create graph with different vertex types
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("City", "NYC"),
        make_add_vertex("Country", "USA")
    });

    auto expr = parser_.parse("Person | City");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should match Person and City but not Country
    EXPECT_EQ(count_subgraphs(result), 2);
}

TEST_F(MatcherTest, MatchMultipleEdgesFromSameVertex) {
    // Create graph: Alice -knows-> Bob, Alice -knows-> Charlie
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("Person", "Charlie"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Charlie")
    });

    auto expr = parser_.parse("Person -knows-> Person");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should have 2 subgraphs (Alice->Bob and Alice->Charlie)
    EXPECT_EQ(count_subgraphs(result), 2);
}

TEST_F(MatcherTest, MatchEmptyGraph) {
    // No vertices or edges
    auto expr = parser_.parse("Person");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should have no subgraphs
    EXPECT_EQ(count_subgraphs(result), 0);
}

TEST_F(MatcherTest, MatchNoEdgesWithLabel) {
    // Add vertices but no edges with "knows" label
    graph_.update({
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob")
    });

    auto expr = parser_.parse("-knows->");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should have no subgraphs (no "knows" edges)
    EXPECT_EQ(count_subgraphs(result), 0);
}

TEST_F(MatcherTest, MatchLongerConcatenation) {
    // Create chain: A -e1-> B -e2-> C -e3-> D
    graph_.update({
        make_add_vertex("Node", "A"),
        make_add_vertex("Node", "B"),
        make_add_vertex("Node", "C"),
        make_add_vertex("Node", "D"),
        make_add_edge("Node", "A", "e1", "Node", "B"),
        make_add_edge("Node", "B", "e2", "Node", "C"),
        make_add_edge("Node", "C", "e3", "Node", "D")
    });

    auto expr = parser_.parse("Node -e1-> Node -e2-> Node -e3-> Node");
    Query query{graph_, expr, {}};
    auto start_positions = finder_.find_start_positions(query);
    auto result = matcher_.match(query, start_positions);

    // Should have 1 subgraph (A -> B -> C -> D)
    EXPECT_EQ(count_subgraphs(result), 1);

    const Subgraph& sg = result.subgraphs[0];
    EXPECT_EQ(sg.vertices.size(), 4);
    EXPECT_EQ(sg.edges.size(), 3);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
