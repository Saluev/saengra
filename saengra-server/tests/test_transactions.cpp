#include "graph.h"
#include <gtest/gtest.h>
#include <random>
#include <set>
#include <string>
#include <tuple>

using namespace saengra;

namespace {

using ModelEdge = std::tuple<std::string, std::string, std::string>;

/* Transactions in terms of sets: present state, state at the last apply, committed state. */
struct ReferenceGraph {
    std::set<std::string> vertices, applied_vertices, committed_vertices;
    std::set<ModelEdge> edges, applied_edges, committed_edges;

    void add_vertex(const std::string& v) { vertices.insert(v); }
    void remove_vertex(const std::string& v) {
        vertices.erase(v);
        std::erase_if(edges, [&v](const ModelEdge& e) { return std::get<0>(e) == v || std::get<2>(e) == v; });
    }
    void add_edge(const std::string& a, const std::string& label, const std::string& b) {
        if (vertices.contains(a) && vertices.contains(b)) edges.emplace(a, label, b);
    }
    void remove_edge(const std::string& a, const std::string& label, const std::string& b) { edges.erase({a, label, b}); }
    void remove_edges_to_all(const std::string& a, const std::string& label) {
        std::erase_if(edges, [&](const ModelEdge& e) { return std::get<0>(e) == a && std::get<1>(e) == label; });
    }
    void apply() { applied_vertices = vertices; applied_edges = edges; }
    void commit() { committed_vertices = applied_vertices; committed_edges = applied_edges; }
    void rollback() {
        vertices = applied_vertices = committed_vertices;
        edges = applied_edges = committed_edges;
    }
};

template <typename T>
std::set<T> difference(const std::set<T>& a, const std::set<T>& b) {
    std::set<T> result;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::inserter(result, result.end()));
    return result;
}

NativeUpdate make_update(NativeUpdateKind kind, const std::string& from, const std::string& label = "",
                         const std::string& to = "") {
    return NativeUpdate{kind, {"v", from}, label, {"v", to}};
}

std::set<std::string> present_vertices(const Graph& graph) {
    std::set<std::string> result;
    for (VertexID v : graph.get_vertices().iter_present()) result.insert(v->value);
    return result;
}

std::set<ModelEdge> present_edges(const Graph& graph) {
    std::set<ModelEdge> result;
    for (const Edge& e : graph.get_edges().iter_present()) result.emplace(e.from->value, *e.label.text, e.to->value);
    return result;
}

void check_observations(const Observations& observations, const ReferenceGraph& ref, const std::string& context) {
    std::set<std::string> added_vertices;
    std::set<ModelEdge> added_edges;
    for (const auto& o : observations) {
        if (boost::get<NewVertex>(&o.observable)) {
            for (VertexID v : o.added_vertex_ids) EXPECT_TRUE(added_vertices.insert(v->value).second) << context;
        }
        if (boost::get<NewEdge>(&o.observable)) {
            for (const Edge& e : o.added_edges) {
                EXPECT_TRUE(added_edges.emplace(e.from->value, *e.label.text, e.to->value).second) << context;
            }
        }
    }
    EXPECT_EQ(added_vertices, difference(ref.vertices, ref.applied_vertices)) << context;
    EXPECT_EQ(added_edges, difference(ref.edges, ref.applied_edges)) << context;
}

void run_random_sequence(uint64_t seed, size_t steps) {
    std::mt19937_64 rng(seed);
    Graph graph;
    ReferenceGraph ref;
    const std::string labels[] = {"x", "y"};

    for (size_t step = 0; step < steps; ++step) {
        const std::string a = std::to_string(rng() % 8), b = std::to_string(rng() % 8);
        const std::string& label = labels[rng() % 2];
        const auto op = rng() % 100;
        const std::string context = "seed " + std::to_string(seed) + ", step " + std::to_string(step);
        if (op < 25) {
            graph.update(NativeUpdates{make_update(NativeUpdateKind::AddVertex, a)});
            ref.add_vertex(a);
        } else if (op < 33) {
            graph.update(NativeUpdates{make_update(NativeUpdateKind::RemoveVertex, a)});
            ref.remove_vertex(a);
        } else if (op < 63) {
            graph.update(NativeUpdates{make_update(NativeUpdateKind::AddEdge, a, label, b)});
            ref.add_edge(a, label, b);
        } else if (op < 75) {
            graph.update(NativeUpdates{make_update(NativeUpdateKind::RemoveEdge, a, label, b)});
            ref.remove_edge(a, label, b);
        } else if (op < 78) {
            graph.update(NativeUpdates{make_update(NativeUpdateKind::RemoveEdgesToAll, a, label)});
            ref.remove_edges_to_all(a, label);
        } else if (op < 88) {
            check_observations(graph.apply(), ref, context + ", apply");
            ref.apply();
        } else if (op < 97) {
            check_observations(graph.apply(), ref, context + ", apply+commit");
            ref.apply();
            graph.commit();
            ref.commit();
        } else {
            graph.rollback();
            ref.rollback();
        }
        ASSERT_EQ(present_vertices(graph), ref.vertices) << context;
        ASSERT_EQ(present_edges(graph), ref.edges) << context;
    }
}

}  // namespace

TEST(TransactionsTest, RandomSequencesMatchReference) {
    for (uint64_t seed = 0; seed < 300; ++seed) {
        run_random_sequence(seed, 300);
        if (HasFailure()) return;
    }
}

TEST(TransactionsTest, LargeTransactionThenSmallOnes) {
    // A large transaction grows the undo log beyond the retained capacity; later ones must still work.
    Graph graph;
    NativeUpdates load;
    for (int i = 0; i < 10000; ++i) load.push_back(make_update(NativeUpdateKind::AddVertex, std::to_string(i)));
    for (int i = 1; i < 10000; ++i) load.push_back(make_update(NativeUpdateKind::AddEdge, std::to_string(i - 1), "next", std::to_string(i)));
    graph.update(load);
    graph.apply();
    graph.commit();

    graph.update(NativeUpdates{make_update(NativeUpdateKind::RemoveVertex, "5000")});
    graph.rollback();
    EXPECT_EQ(present_vertices(graph).size(), 10000);
    EXPECT_EQ(present_edges(graph).size(), 9999);

    graph.update(NativeUpdates{make_update(NativeUpdateKind::RemoveVertex, "5000")});
    graph.apply();
    graph.commit();
    EXPECT_EQ(present_vertices(graph).size(), 9999);
    EXPECT_EQ(present_edges(graph).size(), 9997);
}
