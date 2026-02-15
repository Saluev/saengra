#include "graph.h"
#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

using namespace saengra;
using testing::UnorderedElementsAre;

class GraphTest : public ::testing::Test {
protected:
    Graph graph;

    // Helper to get non-const vertices container for testing
    MutableVerticesContainer& get_vertices_mut() {
        return const_cast<MutableVerticesContainer&>(graph.get_vertices());
    }

    MutableEdgesContainer& get_edges_mut() {
        return const_cast<MutableEdgesContainer&>(graph.get_edges());
    }

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

    Update make_remove_vertex(const std::string& type_name, const std::string& value) {
        return Update{
            ProtoUpdateKind::RemoveVertex,
            UpdateVertex{type_name, value},
            std::nullopt,
            std::nullopt
        };
    }

    Update make_remove_edge(const std::string& from_type, const std::string& from_value,
                            const std::string& label,
                            const std::string& to_type, const std::string& to_value) {
        return Update{
            ProtoUpdateKind::RemoveEdge,
            UpdateVertex{from_type, from_value},
            label,
            UpdateVertex{to_type, to_value}
        };
    }

    Update make_remove_edges_to_all(const std::string& from_type, const std::string& from_value,
                                     const std::string& label) {
        return Update{
            ProtoUpdateKind::RemoveEdgesToAll,
            UpdateVertex{from_type, from_value},
            label,
            std::nullopt
        };
    }

    Observations normalize(Observations obs) {
        std::sort(obs.begin(), obs.end());
        for (auto& o : obs) {
            std::sort(o.added_vertex_ids.begin(), o.added_vertex_ids.end());
            std::sort(o.added_edges.begin(), o.added_edges.end());
        }
        return obs;
    }
};

TEST_F(GraphTest, AddVertices) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("City", "NYC")
    };

    graph.update(updates);

    auto& vertices_mut = get_vertices_mut();
    const auto& vertices = graph.get_vertices();
    auto just_added = vertices.iter_just_added();
    EXPECT_EQ(just_added.size(), 3);

    // Verify observations from apply()
    auto observations = graph.apply();

    // Get vertex IDs and type names for expectations
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto city_type = vertices_mut.internalize_type_name("City");
    auto alice = VertexData(person_type, "Alice");
    auto bob = VertexData(person_type, "Bob");
    auto nyc = VertexData(city_type, "NYC");
    auto alice_id = vertices.get_vertex_id(alice).value();
    auto bob_id = vertices.get_vertex_id(bob).value();
    auto nyc_id = vertices.get_vertex_id(nyc).value();

    Observations expected = {
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(NewVertexOfType{city_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{bob}),
        Observation(ParticularVertex{nyc})
    };
    expected[0].added_vertex_ids = {nyc_id, bob_id, alice_id};  // Reverse order from actual output
    expected[1].added_vertex_ids = {bob_id, alice_id};
    expected[2].added_vertex_ids = {nyc_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, AddEdge) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("City", "NYC"),
        make_add_edge("Person", "Alice", "livesIn", "City", "NYC")
    };

    graph.update(updates);

    const auto& edges = graph.get_edges();
    auto just_added_edges = edges.iter_just_added();
    ASSERT_EQ(just_added_edges.size(), 1);

    // Verify the edge exists
    EXPECT_EQ(*just_added_edges[0].label.text, "livesIn");

    // Verify observations from apply()
    auto observations = graph.apply();

    auto& vertices_mut = get_vertices_mut();
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto city_type = vertices_mut.internalize_type_name("City");
    auto livesIn_label = get_edges_mut().internalize_label("livesIn");
    auto alice = VertexData(person_type, "Alice");
    auto nyc = VertexData(city_type, "NYC");
    auto alice_id = graph.get_vertices().get_vertex_id(alice).value();
    auto nyc_id = graph.get_vertices().get_vertex_id(nyc).value();
    auto edge = Edge{alice_id, livesIn_label, nyc_id};

    Observations expected = {
        Observation(NewEdge()),
        Observation(NewEdgeWithLabel{livesIn_label}),
        Observation(EdgeFrom{alice_id}),
        Observation(EdgeFromWithLabel{alice_id, livesIn_label}),
        Observation(EdgeTo{nyc_id}),
        Observation(EdgeToWithLabel{nyc_id, livesIn_label}),
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(NewVertexOfType{city_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{nyc})
    };
    expected[0].added_edges = {edge};
    expected[1].added_edges = {edge};
    expected[6].added_vertex_ids = {nyc_id, alice_id};
    expected[7].added_vertex_ids = {alice_id};
    expected[8].added_vertex_ids = {nyc_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, AddEdgeWithForwardAndInverse) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob")
    };

    graph.update(updates);

    const auto& vertices = graph.get_vertices();
    const auto& edges = graph.get_edges();

    // Get vertex IDs (need mutable access to internalize type names)
    auto alice_type = get_vertices_mut().internalize_type_name("Person");
    auto alice = VertexData(alice_type, "Alice");
    auto bob = VertexData(alice_type, "Bob");

    auto alice_id = vertices.get_vertex_id(alice);
    auto bob_id = vertices.get_vertex_id(bob);

    ASSERT_TRUE(alice_id.has_value());
    ASSERT_TRUE(bob_id.has_value());

    // Check forward edge: Alice -> Bob
    auto forward_edges = edges.iter_present_from_vertex(alice_id.value());
    EXPECT_EQ(forward_edges.size(), 1);
    EXPECT_EQ(forward_edges[0].to, bob_id.value());

    // Check inverse edge: Bob <- Alice
    auto inverse_edges = edges.iter_present_to_vertex(bob_id.value());
    EXPECT_EQ(inverse_edges.size(), 1);
    EXPECT_EQ(inverse_edges[0].from, alice_id.value());

    // Verify observations from apply()
    auto observations = graph.apply();

    auto person_type = get_vertices_mut().internalize_type_name("Person");
    auto knows_label = get_edges_mut().internalize_label("knows");
    auto edge = Edge{alice_id.value(), knows_label, bob_id.value()};

    Observations expected = {
        Observation(NewEdge()),
        Observation(NewEdgeWithLabel{knows_label}),
        Observation(EdgeFrom{alice_id.value()}),
        Observation(EdgeFromWithLabel{alice_id.value(), knows_label}),
        Observation(EdgeTo{bob_id.value()}),
        Observation(EdgeToWithLabel{bob_id.value(), knows_label}),
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{bob})
    };
    expected[0].added_edges = {edge};
    expected[1].added_edges = {edge};
    expected[6].added_vertex_ids = {bob_id.value(), alice_id.value()};
    expected[7].added_vertex_ids = {bob_id.value(), alice_id.value()};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, AddEdgeToNonExistentVertex) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        // Try to add edge to non-existent vertex
        make_add_edge("Person", "Alice", "knows", "Person", "Bob")
    };

    graph.update(updates);

    const auto& edges = graph.get_edges();
    auto just_added_edges = edges.iter_just_added();

    // Edge should not be added since Bob doesn't exist
    EXPECT_EQ(just_added_edges.size(), 0);

    // Verify observations from apply() - should only have vertex observations
    auto observations = graph.apply();

    auto& vertices_mut = get_vertices_mut();
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto alice = VertexData(person_type, "Alice");
    auto alice_id = graph.get_vertices().get_vertex_id(alice).value();

    Observations expected = {
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(ParticularVertex{alice})
    };
    expected[0].added_vertex_ids = {alice_id};
    expected[1].added_vertex_ids = {alice_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, RemoveVertex) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("Person", "Charlie"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_add_edge("Person", "Bob", "knows", "Person", "Charlie"),
        make_remove_vertex("Person", "Bob")
    };

    graph.update(updates);

    const auto& vertices = graph.get_vertices();
    const auto& edges = graph.get_edges();

    // Bob was added then removed in same batch - should cancel out
    auto just_added = vertices.iter_just_added();
    auto just_removed = vertices.iter_just_removed();
    EXPECT_EQ(just_added.size(), 2);  // Alice and Charlie remain
    EXPECT_EQ(just_removed.size(), 0);  // Bob cancelled out

    // Both edges involving Bob should be cancelled out
    auto added_edges = edges.iter_just_added();
    auto removed_edges = edges.iter_just_removed();
    EXPECT_EQ(added_edges.size(), 0);  // All edges cancelled
    EXPECT_EQ(removed_edges.size(), 0);  // All edges cancelled

    // Verify observations from apply()
    auto observations = graph.apply();

    auto& vertices_mut = get_vertices_mut();
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto alice = VertexData(person_type, "Alice");
    auto charlie = VertexData(person_type, "Charlie");
    auto alice_id = graph.get_vertices().get_vertex_id(alice).value();
    auto charlie_id = graph.get_vertices().get_vertex_id(charlie).value();

    Observations expected = {
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{charlie})
    };
    expected[0].added_vertex_ids = {charlie_id, alice_id};
    expected[1].added_vertex_ids = {charlie_id, alice_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, RemoveVertexWithMultipleEdges) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("Person", "Charlie"),
        make_add_vertex("City", "NYC"),
        make_add_edge("Person", "Bob", "knows", "Person", "Alice"),
        make_add_edge("Person", "Bob", "knows", "Person", "Charlie"),
        make_add_edge("Person", "Bob", "livesIn", "City", "NYC"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_remove_vertex("Person", "Bob")
    };

    graph.update(updates);

    const auto& edges = graph.get_edges();

    // Bob and all his edges were added then removed - should cancel out
    auto added_edges = edges.iter_just_added();
    auto removed_edges = edges.iter_just_removed();
    EXPECT_EQ(added_edges.size(), 0);  // All edges cancelled
    EXPECT_EQ(removed_edges.size(), 0);  // All edges cancelled

    // Verify observations from apply()
    auto observations = graph.apply();

    auto& vertices_mut = get_vertices_mut();
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto city_type = vertices_mut.internalize_type_name("City");
    auto alice = VertexData(person_type, "Alice");
    auto charlie = VertexData(person_type, "Charlie");
    auto nyc = VertexData(city_type, "NYC");
    auto alice_id = graph.get_vertices().get_vertex_id(alice).value();
    auto charlie_id = graph.get_vertices().get_vertex_id(charlie).value();
    auto nyc_id = graph.get_vertices().get_vertex_id(nyc).value();

    Observations expected = {
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(NewVertexOfType{city_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{charlie}),
        Observation(ParticularVertex{nyc})
    };
    expected[0].added_vertex_ids = {nyc_id, charlie_id, alice_id};
    expected[1].added_vertex_ids = {charlie_id, alice_id};
    expected[2].added_vertex_ids = {nyc_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, RemoveNonExistentVertex) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_remove_vertex("Person", "Bob")  // Bob doesn't exist
    };

    graph.update(updates);

    const auto& vertices = graph.get_vertices();
    auto just_removed = vertices.iter_just_removed();

    // Should have no effect
    EXPECT_EQ(just_removed.size(), 0);

    // Verify observations from apply()
    auto observations = graph.apply();

    auto& vertices_mut = get_vertices_mut();
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto alice = VertexData(person_type, "Alice");
    auto alice_id = graph.get_vertices().get_vertex_id(alice).value();

    Observations expected = {
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(ParticularVertex{alice})
    };
    expected[0].added_vertex_ids = {alice_id};
    expected[1].added_vertex_ids = {alice_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, RemoveEdge) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_remove_edge("Person", "Alice", "knows", "Person", "Bob")
    };

    graph.update(updates);

    const auto& edges = graph.get_edges();

    // Edge should be added then removed
    auto just_added = edges.iter_just_added();
    auto just_removed = edges.iter_just_removed();

    EXPECT_EQ(just_added.size(), 0);  // Cancelled out
    EXPECT_EQ(just_removed.size(), 0);  // Cancelled out

    // Verify observations from apply()
    auto observations = graph.apply();

    auto& vertices_mut = get_vertices_mut();
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto alice = VertexData(person_type, "Alice");
    auto bob = VertexData(person_type, "Bob");
    auto alice_id = graph.get_vertices().get_vertex_id(alice).value();
    auto bob_id = graph.get_vertices().get_vertex_id(bob).value();

    Observations expected = {
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{bob})
    };
    expected[0].added_vertex_ids = {bob_id, alice_id};
    expected[1].added_vertex_ids = {bob_id, alice_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, RemoveNonExistentEdge) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_remove_edge("Person", "Alice", "knows", "Person", "Bob")  // Edge doesn't exist
    };

    graph.update(updates);

    const auto& edges = graph.get_edges();
    auto just_removed = edges.iter_just_removed();

    // Should have no effect
    EXPECT_EQ(just_removed.size(), 0);

    // Verify observations from apply()
    auto observations = graph.apply();

    auto& vertices_mut = get_vertices_mut();
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto alice = VertexData(person_type, "Alice");
    auto bob = VertexData(person_type, "Bob");
    auto alice_id = graph.get_vertices().get_vertex_id(alice).value();
    auto bob_id = graph.get_vertices().get_vertex_id(bob).value();

    Observations expected = {
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{bob})
    };
    expected[0].added_vertex_ids = {bob_id, alice_id};
    expected[1].added_vertex_ids = {bob_id, alice_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, RemoveEdgesToAll) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("Person", "Charlie"),
        make_add_vertex("City", "NYC"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Charlie"),
        make_add_edge("Person", "Alice", "livesIn", "City", "NYC"),
        make_remove_edges_to_all("Person", "Alice", "knows")
    };

    graph.update(updates);

    const auto& vertices = graph.get_vertices();
    const auto& edges = graph.get_edges();

    auto alice_type = get_vertices_mut().internalize_type_name("Person");
    auto alice = VertexData(alice_type, "Alice");
    auto alice_id = vertices.get_vertex_id(alice);
    ASSERT_TRUE(alice_id.has_value());

    // Check that only "livesIn" edge remains
    auto forward_edges = edges.iter_present_from_vertex(alice_id.value());
    EXPECT_EQ(forward_edges.size(), 1);
    EXPECT_EQ(*forward_edges[0].label.text, "livesIn");

    // Verify observations from apply()
    auto observations = graph.apply();

    auto person_type = get_vertices_mut().internalize_type_name("Person");
    auto city_type = get_vertices_mut().internalize_type_name("City");
    auto livesIn_label = get_edges_mut().internalize_label("livesIn");
    auto bob = VertexData(person_type, "Bob");
    auto charlie = VertexData(person_type, "Charlie");
    auto nyc = VertexData(city_type, "NYC");
    auto bob_id = graph.get_vertices().get_vertex_id(bob).value();
    auto charlie_id = graph.get_vertices().get_vertex_id(charlie).value();
    auto nyc_id = graph.get_vertices().get_vertex_id(nyc).value();
    auto edge = Edge{alice_id.value(), livesIn_label, nyc_id};

    Observations expected = {
        Observation(NewEdge()),
        Observation(NewEdgeWithLabel{livesIn_label}),
        Observation(EdgeFrom{alice_id.value()}),
        Observation(EdgeFromWithLabel{alice_id.value(), livesIn_label}),
        Observation(EdgeTo{nyc_id}),
        Observation(EdgeToWithLabel{nyc_id, livesIn_label}),
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(NewVertexOfType{city_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{bob}),
        Observation(ParticularVertex{charlie}),
        Observation(ParticularVertex{nyc})
    };
    expected[0].added_edges = {edge};
    expected[1].added_edges = {edge};
    expected[6].added_vertex_ids = {nyc_id, charlie_id, bob_id, alice_id.value()};
    expected[7].added_vertex_ids = {charlie_id, bob_id, alice_id.value()};
    expected[8].added_vertex_ids = {nyc_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, RemoveEdgesToAllNonExistentLabel) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_remove_edges_to_all("Person", "Alice", "likes")  // "likes" doesn't exist
    };

    graph.update(updates);

    const auto& edges = graph.get_edges();

    // Should have no effect on existing edges
    auto just_added = edges.iter_just_added();
    EXPECT_EQ(just_added.size(), 1);  // "knows" edge still there

    // Verify observations from apply()
    auto observations = graph.apply();

    auto& vertices_mut = get_vertices_mut();
    auto person_type = vertices_mut.internalize_type_name("Person");
    auto knows_label = get_edges_mut().internalize_label("knows");
    auto alice = VertexData(person_type, "Alice");
    auto bob = VertexData(person_type, "Bob");
    auto alice_id = graph.get_vertices().get_vertex_id(alice).value();
    auto bob_id = graph.get_vertices().get_vertex_id(bob).value();
    auto edge = Edge{alice_id, knows_label, bob_id};

    Observations expected = {
        Observation(NewEdge()),
        Observation(NewEdgeWithLabel{knows_label}),
        Observation(EdgeFrom{alice_id}),
        Observation(EdgeFromWithLabel{alice_id, knows_label}),
        Observation(EdgeTo{bob_id}),
        Observation(EdgeToWithLabel{bob_id, knows_label}),
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{bob})
    };
    expected[0].added_edges = {edge};
    expected[1].added_edges = {edge};
    expected[6].added_vertex_ids = {bob_id, alice_id};
    expected[7].added_vertex_ids = {bob_id, alice_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, ComplexScenario) {
    std::vector<Update> updates = {
        // Create a social network
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("Person", "Charlie"),
        make_add_vertex("Person", "Diana"),
        make_add_vertex("City", "NYC"),
        make_add_vertex("City", "LA"),

        // Add friendships
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Charlie"),
        make_add_edge("Person", "Bob", "knows", "Person", "Charlie"),
        make_add_edge("Person", "Charlie", "knows", "Person", "Diana"),

        // Add locations
        make_add_edge("Person", "Alice", "livesIn", "City", "NYC"),
        make_add_edge("Person", "Bob", "livesIn", "City", "NYC"),
        make_add_edge("Person", "Charlie", "livesIn", "City", "LA"),

        // Remove Bob (should remove all edges involving Bob)
        make_remove_vertex("Person", "Bob"),

        // Remove Alice's friendship with Charlie
        make_remove_edge("Person", "Alice", "knows", "Person", "Charlie")
    };

    graph.update(updates);

    const auto& vertices = graph.get_vertices();
    const auto& edges = graph.get_edges();

    // Verify vertices: Alice, Charlie, Diana, NYC, LA (Bob cancelled out)
    auto just_added_vertices = vertices.iter_just_added();
    EXPECT_EQ(just_added_vertices.size(), 5);  // Bob was added then removed, so cancelled

    auto just_removed_vertices = vertices.iter_just_removed();
    EXPECT_EQ(just_removed_vertices.size(), 0);  // Bob cancelled out

    // Verify Alice's edges
    auto alice_type = get_vertices_mut().internalize_type_name("Person");
    auto alice = VertexData(alice_type, "Alice");
    auto alice_id = vertices.get_vertex_id(alice);
    ASSERT_TRUE(alice_id.has_value());

    auto alice_edges = edges.iter_present_from_vertex(alice_id.value());
    EXPECT_EQ(alice_edges.size(), 1);  // Only livesIn NYC remains
    EXPECT_EQ(*alice_edges[0].label.text, "livesIn");

    // Verify observations from apply()
    auto observations = graph.apply();

    auto person_type = alice_type;  // already created above
    auto city_type = get_vertices_mut().internalize_type_name("City");
    auto livesIn_label = get_edges_mut().internalize_label("livesIn");
    auto knows_label = get_edges_mut().internalize_label("knows");
    auto charlie = VertexData(person_type, "Charlie");
    auto diana = VertexData(person_type, "Diana");
    auto nyc = VertexData(city_type, "NYC");
    auto la = VertexData(city_type, "LA");
    auto charlie_id = vertices.get_vertex_id(charlie).value();
    auto diana_id = vertices.get_vertex_id(diana).value();
    auto nyc_id = vertices.get_vertex_id(nyc).value();
    auto la_id = vertices.get_vertex_id(la).value();

    auto edge1 = Edge{alice_id.value(), livesIn_label, nyc_id};
    auto edge2 = Edge{charlie_id, livesIn_label, la_id};
    auto edge3 = Edge{charlie_id, knows_label, diana_id};

    Observations expected = {
        Observation(NewEdge()),
        Observation(NewEdgeWithLabel{livesIn_label}),
        Observation(NewEdgeWithLabel{knows_label}),
        Observation(EdgeFrom{alice_id.value()}),
        Observation(EdgeFrom{charlie_id}),
        Observation(EdgeFromWithLabel{alice_id.value(), livesIn_label}),
        Observation(EdgeFromWithLabel{charlie_id, livesIn_label}),
        Observation(EdgeFromWithLabel{charlie_id, knows_label}),
        Observation(EdgeTo{nyc_id}),
        Observation(EdgeTo{la_id}),
        Observation(EdgeTo{diana_id}),
        Observation(EdgeToWithLabel{nyc_id, livesIn_label}),
        Observation(EdgeToWithLabel{la_id, livesIn_label}),
        Observation(EdgeToWithLabel{diana_id, knows_label}),
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(NewVertexOfType{city_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{charlie}),
        Observation(ParticularVertex{diana}),
        Observation(ParticularVertex{nyc}),
        Observation(ParticularVertex{la})
    };
    expected[0].added_edges = {edge2, edge3, edge1};  // Order may vary, match actual
    expected[1].added_edges = {edge2, edge1};  // Order may vary
    expected[2].added_edges = {edge3};
    expected[14].added_vertex_ids = {la_id, nyc_id, diana_id, charlie_id, alice_id.value()};
    expected[15].added_vertex_ids = {diana_id, charlie_id, alice_id.value()};
    expected[16].added_vertex_ids = {la_id, nyc_id};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, EmptyUpdates) {
    std::vector<Update> updates = {};

    graph.update(updates);

    const auto& vertices = graph.get_vertices();
    const auto& edges = graph.get_edges();

    // Should have no effect
    EXPECT_EQ(vertices.iter_just_added().size(), 0);
    EXPECT_EQ(edges.iter_just_added().size(), 0);

    // Verify observations from apply()
    auto observations = graph.apply();
    EXPECT_EQ(observations.size(), 0);  // No updates means no observations
}

TEST_F(GraphTest, MultipleEdgesBetweenSameVertices) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_add_edge("Person", "Alice", "likes", "Person", "Bob"),
        make_add_edge("Person", "Alice", "worksWith", "Person", "Bob")
    };

    graph.update(updates);

    const auto& vertices = graph.get_vertices();
    const auto& edges = graph.get_edges();

    auto alice_type = get_vertices_mut().internalize_type_name("Person");
    auto alice = VertexData(alice_type, "Alice");
    auto alice_id = vertices.get_vertex_id(alice);
    ASSERT_TRUE(alice_id.has_value());

    auto alice_edges = edges.iter_present_from_vertex(alice_id.value());
    EXPECT_EQ(alice_edges.size(), 3);

    // Verify observations from apply()
    auto observations = graph.apply();

    auto person_type = alice_type;  // already created above
    auto knows_label = get_edges_mut().internalize_label("knows");
    auto likes_label = get_edges_mut().internalize_label("likes");
    auto worksWith_label = get_edges_mut().internalize_label("worksWith");
    auto bob = VertexData(person_type, "Bob");
    auto bob_id = vertices.get_vertex_id(bob).value();

    auto edge1 = Edge{alice_id.value(), knows_label, bob_id};
    auto edge2 = Edge{alice_id.value(), likes_label, bob_id};
    auto edge3 = Edge{alice_id.value(), worksWith_label, bob_id};

    Observations expected = {
        Observation(NewEdge()).with_added_edges({edge1, edge2, edge3}),
        Observation(NewEdgeWithLabel{knows_label}).with_added_edges({edge1}),
        Observation(NewEdgeWithLabel{likes_label}).with_added_edges({edge2}),
        Observation(NewEdgeWithLabel{worksWith_label}).with_added_edges({edge3}),
        Observation(EdgeFrom{alice_id.value()}),
        Observation(EdgeFromWithLabel{alice_id.value(), knows_label}),
        Observation(EdgeFromWithLabel{alice_id.value(), likes_label}),
        Observation(EdgeFromWithLabel{alice_id.value(), worksWith_label}),
        Observation(EdgeTo{bob_id}),
        Observation(EdgeToWithLabel{bob_id, knows_label}),
        Observation(EdgeToWithLabel{bob_id, likes_label}),
        Observation(EdgeToWithLabel{bob_id, worksWith_label}),
        Observation(NewVertex()).with_added_vertex_ids({bob_id, alice_id.value()}),
        Observation(NewVertexOfType{person_type}).with_added_vertex_ids({bob_id, alice_id.value()}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{bob})
    };

    EXPECT_EQ(normalize(observations), normalize(expected));
}

TEST_F(GraphTest, RemoveEdgesToAllWithMultipleTargets) {
    std::vector<Update> updates = {
        make_add_vertex("Person", "Alice"),
        make_add_vertex("Person", "Bob"),
        make_add_vertex("Person", "Charlie"),
        make_add_vertex("Person", "Diana"),
        make_add_edge("Person", "Alice", "knows", "Person", "Bob"),
        make_add_edge("Person", "Alice", "knows", "Person", "Charlie"),
        make_add_edge("Person", "Alice", "knows", "Person", "Diana"),
        make_add_edge("Person", "Alice", "likes", "Person", "Bob"),
        make_remove_edges_to_all("Person", "Alice", "knows")
    };

    graph.update(updates);

    const auto& vertices = graph.get_vertices();
    const auto& edges = graph.get_edges();

    auto alice_type = get_vertices_mut().internalize_type_name("Person");
    auto alice = VertexData(alice_type, "Alice");
    auto alice_id = vertices.get_vertex_id(alice);
    ASSERT_TRUE(alice_id.has_value());

    // Only "likes" edge should remain
    auto alice_edges = edges.iter_present_from_vertex(alice_id.value());
    EXPECT_EQ(alice_edges.size(), 1);
    EXPECT_EQ(*alice_edges[0].label.text, "likes");

    // Verify inverse edges are also removed
    auto bob_type = get_vertices_mut().internalize_type_name("Person");
    auto bob = VertexData(bob_type, "Bob");
    auto bob_id = vertices.get_vertex_id(bob);
    ASSERT_TRUE(bob_id.has_value());

    auto bob_incoming = edges.iter_present_to_vertex(bob_id.value());
    EXPECT_EQ(bob_incoming.size(), 1);  // Only "likes" edge
    EXPECT_EQ(*bob_incoming[0].label.text, "likes");

    // Verify observations from apply()
    auto observations = graph.apply();

    auto person_type = alice_type;  // already created above
    auto likes_label = get_edges_mut().internalize_label("likes");
    auto charlie = VertexData(person_type, "Charlie");
    auto diana = VertexData(person_type, "Diana");
    auto charlie_id = vertices.get_vertex_id(charlie).value();
    auto diana_id = vertices.get_vertex_id(diana).value();

    auto edge = Edge{alice_id.value(), likes_label, bob_id.value()};

    Observations expected = {
        Observation(NewEdge()),
        Observation(NewEdgeWithLabel{likes_label}),
        Observation(EdgeFrom{alice_id.value()}),
        Observation(EdgeFromWithLabel{alice_id.value(), likes_label}),
        Observation(EdgeTo{bob_id.value()}),
        Observation(EdgeToWithLabel{bob_id.value(), likes_label}),
        Observation(NewVertex()),
        Observation(NewVertexOfType{person_type}),
        Observation(ParticularVertex{alice}),
        Observation(ParticularVertex{bob}),
        Observation(ParticularVertex{charlie}),
        Observation(ParticularVertex{diana})
    };
    expected[0].added_edges = {edge};
    expected[1].added_edges = {edge};
    expected[6].added_vertex_ids = {diana_id, charlie_id, bob_id.value(), alice_id.value()};
    expected[7].added_vertex_ids = {diana_id, charlie_id, bob_id.value(), alice_id.value()};

    EXPECT_EQ(normalize(observations), normalize(expected));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
