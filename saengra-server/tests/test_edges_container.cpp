#include "edges_container.h"
#include "edge.h"
#include <gtest/gtest.h>
#include <string>

using namespace saengra;

class EdgesContainerTest : public ::testing::Test {
protected:
    MutableEdgesContainer container;
};

//TEST_F(EdgesContainerTest, AddAndQueryEdge) {
//    EdgeLabel knows = container.internalize_label("knows");
//
//    // Add edge: 1 -[knows]-> 2
//    container.add_edge(1, knows, 2);
//
//    // Query from vertex 1
//    auto edges_from_1 = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges_from_1.size(), 1);
//    EXPECT_EQ(edges_from_1[0].from, 1);
//    EXPECT_EQ(edges_from_1[0].label.text, "knows");
//    EXPECT_EQ(edges_from_1[0].to, 2);
//
//    // Query to vertex 2 (inverse)
//    auto edges_to_2 = container.iter_present_to_vertex(2);
//    EXPECT_EQ(edges_to_2.size(), 1);
//    EXPECT_EQ(edges_to_2[0].from, 1);
//    EXPECT_EQ(edges_to_2[0].to, 2);
//}

//TEST_F(EdgesContainerTest, AddMultipleEdges) {
//    EdgeLabel knows = container.internalize_label("knows");
//    EdgeLabel likes = container.internalize_label("likes");
//
//    // Add multiple edges from vertex 1
//    container.add_edge(1, knows, 2);
//    container.add_edge(1, knows, 3);
//    container.add_edge(1, likes, 4);
//
//    auto edges_from_1 = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges_from_1.size(), 3);
//
//    // Query via specific label
//    auto knows_edges = container.iter_present_from_vertex_via_label(1, knows);
//    EXPECT_EQ(knows_edges.size(), 2);
//
//    auto likes_edges = container.iter_present_from_vertex_via_label(1, likes);
//    EXPECT_EQ(likes_edges.size(), 1);
//    EXPECT_EQ(likes_edges[0], 4);
//}

//TEST_F(EdgesContainerTest, DiscardEdge) {
//    EdgeLabel knows = container.internalize_label("knows");
//
//    container.add_edge(1, knows, 2);
//    container.add_edge(1, knows, 3);
//
//    // Discard one edge
//    container.discard_edge(1, knows, 2);
//
//    auto edges_from_1 = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges_from_1.size(), 1);
//    EXPECT_EQ(edges_from_1[0].to, 3);
//
//    // Check inverse is also updated
//    auto edges_to_2 = container.iter_present_to_vertex(2);
//    EXPECT_EQ(edges_to_2.size(), 0);
//}

//TEST_F(EdgesContainerTest, ApplyAndCommit) {
//    EdgeLabel knows = container.internalize_label("knows");
//
//    container.add_edge(1, knows, 2);
//    container.apply();
//
//    // Edge should still be present after apply
//    auto edges = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges.size(), 1);
//
//    container.commit();
//
//    // Edge should still be present after commit
//    edges = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges.size(), 1);
//}

//TEST_F(EdgesContainerTest, Rollback) {
//    EdgeLabel knows = container.internalize_label("knows");
//
//    // Add and commit first edge
//    container.add_edge(1, knows, 2);
//    container.apply();
//    container.commit();
//
//    // Add second edge but don't commit
//    container.add_edge(1, knows, 3);
//    container.apply();
//
//    // Discard first edge but don't commit
//    container.discard_edge(1, knows, 2);
//
//    // Rollback
//    container.rollback();
//
//    // After rollback: only first edge should remain
//    auto edges = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges.size(), 1);
//    EXPECT_EQ(edges[0].to, 2);
//}

//TEST_F(EdgesContainerTest, RemoveAllEdgesFromVertex) {
//    EdgeLabel knows = container.internalize_label("knows");
//    EdgeLabel likes = container.internalize_label("likes");
//
//    container.add_edge(1, knows, 2);
//    container.add_edge(1, knows, 3);
//    container.add_edge(1, likes, 4);
//
//    // Remove all edges from vertex 1
//    container.remove_edges_from_vertex(1);
//
//    auto edges = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges.size(), 0);
//
//    // Check inverses are also removed
//    EXPECT_EQ(container.iter_present_to_vertex(2).size(), 0);
//    EXPECT_EQ(container.iter_present_to_vertex(3).size(), 0);
//    EXPECT_EQ(container.iter_present_to_vertex(4).size(), 0);
//}

//TEST_F(EdgesContainerTest, RemoveEdgesViaLabel) {
//    EdgeLabel knows = container.internalize_label("knows");
//    EdgeLabel likes = container.internalize_label("likes");
//
//    container.add_edge(1, knows, 2);
//    container.add_edge(1, knows, 3);
//    container.add_edge(1, likes, 4);
//
//    // Remove only 'knows' edges
//    container.remove_edges_from_vertex_via_label(1, knows);
//
//    auto edges = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges.size(), 1);
//    EXPECT_EQ(edges[0].label.text, "likes");
//    EXPECT_EQ(edges[0].to, 4);
//}

TEST_F(EdgesContainerTest, QueryNonExistentVertex) {
    auto edges = container.iter_present_from_vertex(VertexID(999));
    EXPECT_EQ(edges.size(), 0);

    edges = container.iter_present_to_vertex(VertexID(999));
    EXPECT_EQ(edges.size(), 0);

//    EdgeLabel knows = container.internalize_label("knows");
//    auto via_label = container.iter_present_from_vertex_via_label(999, knows);
//    EXPECT_EQ(via_label.size(), 0);
}

//TEST_F(EdgesContainerTest, DuplicateEdge) {
//    EdgeLabel knows = container.internalize_label("knows");
//
//    container.add_edge(1, knows, 2);
//    container.add_edge(1, knows, 2);  // Duplicate
//
//    auto edges = container.iter_present_from_vertex(1);
//    EXPECT_EQ(edges.size(), 1);  // Should still be 1
//}

//TEST_F(EdgesContainerTest, BidirectionalQuery) {
//    EdgeLabel knows = container.internalize_label("knows");
//
//    // Create a small graph: 1 -> 2, 1 -> 3, 2 -> 3
//    container.add_edge(1, knows, 2);
//    container.add_edge(1, knows, 3);
//    container.add_edge(2, knows, 3);
//
//    // Check outgoing edges
//    EXPECT_EQ(container.iter_present_from_vertex(1).size(), 2);
//    EXPECT_EQ(container.iter_present_from_vertex(2).size(), 1);
//    EXPECT_EQ(container.iter_present_from_vertex(3).size(), 0);
//
//    // Check incoming edges
//    EXPECT_EQ(container.iter_present_to_vertex(1).size(), 0);
//    EXPECT_EQ(container.iter_present_to_vertex(2).size(), 1);
//    EXPECT_EQ(container.iter_present_to_vertex(3).size(), 2);
//}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
