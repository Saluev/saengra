#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "matcher.h"
#include "observable.h"
#include "query.h"

namespace saengra {

// Subgraphs of nested expressions, cached per start position and refs and shared by all observers.
// When cached subgraphs change, the registry issues NestedExpressionSubgraph observations.
class NestedExpressionRegistry : public NestedResults {
public:
    NestedExpressionRegistry(const Graph& graph);

    // Gives nested expressions of the plan their IDs in the registry, registering new ones.
    void register_plan(Plan& plan, const PlaceholderValues& placeholder_values);

    const std::vector<Subgraph>& get(const MatchNestedExpression& plan, const Position& position, const Refs& refs) override;

    // Whether some cached subgraphs depend on the observable.
    bool depends_on(const Observable& observable) const;

    // Rematches cached subgraphs affected by the observations, the most nested expressions first.
    // Returns observations of the ones that changed.
    Observations update(const Observations& observations);

    // Marks cached subgraphs that observers may no longer depend on.
    void mark_maybe_unused(const Observable& observable);

    // Drops cached subgraphs that nothing depends on anymore; `is_observed` tells whether observers do.
    void evict_unused(const std::function<bool(const Observable&)>& is_observed);

    // Drops all cached subgraphs, e.g. after the graph was rolled back.
    void clear();

private:
    struct NestedExpression {
        Query query;  // the plan of the query is the nested expression
        size_t height;  // how many nested expressions it consists of, including itself
    };

    const Graph& graph_;
    Matcher matcher_;
    std::vector<std::unique_ptr<NestedExpression>> expressions_;
    std::unordered_map<std::string, size_t> expression_ids_;

    // References to cached subgraphs stay valid.
    absl::node_hash_map<NestedExpressionStart, std::vector<Subgraph>, std::hash<NestedExpressionStart>> subgraphs_;
    absl::flat_hash_map<NestedExpressionStart, Observables, std::hash<NestedExpressionStart>> deps_;
    absl::flat_hash_map<Observable, absl::flat_hash_set<NestedExpressionStart, std::hash<NestedExpressionStart>>, std::hash<Observable>> dep_to_starts_;
    absl::flat_hash_set<Observable, std::hash<Observable>> maybe_unused_;

    // Returns the height of the highest nested expression in the plan.
    size_t register_nested(Plan& plan, const PlaceholderValues& placeholder_values);
    QuerySet match(const NestedExpressionStart& start);
    void subscribe(const NestedExpressionStart& start, const Observable& dep);
    void unsubscribe(const NestedExpressionStart& start, const Observable& dep);
    const MatchNestedExpression& nested_plan(size_t id) const;
};

}
