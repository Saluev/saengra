#include <algorithm>
#include <map>
#include "nested_registry.h"

namespace saengra {

namespace {

void collect_placeholders(const MatchVertex& vertex, std::vector<int>& placeholders) {
    if (vertex.placeholder_idx.has_value()) {
        placeholders.push_back(vertex.placeholder_idx.value());
    }
}

// Placeholders the plan uses, in order.
void collect_placeholders(const Plan& plan, std::vector<int>& placeholders) {
    if (const auto* vertex = boost::get<MatchVertex>(&plan)) {
        collect_placeholders(*vertex, placeholders);
    } else if (const auto* path = boost::get<MatchPath>(&plan)) {
        for (const auto& v : path->vertices) {
            collect_placeholders(v, placeholders);
        }
    } else if (const auto* path_to_vertex = boost::get<MatchPathToVertex>(&plan)) {
        for (const auto& v : path_to_vertex->path.vertices) {
            collect_placeholders(v, placeholders);
        }
        collect_placeholders(path_to_vertex->vertex, placeholders);
    }
    for (const Plan& child : plan.iter_nested_plans()) {
        collect_placeholders(child, placeholders);
    }
}

// Sorted and deduplicated deps of a query set.
Observables sorted_deps(QuerySetObservables&& qsos) {
    Observables deps;
    deps.reserve(qsos.size());
    for (auto& d : qsos) {
        deps.push_back(std::move(d.observable));
    }
    std::sort(deps.begin(), deps.end());
    deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
    return deps;
}

NestedExpressionStart to_start(const NestedExpressionSubgraph& observable) {
    return NestedExpressionStart{
        observable.expression_id,
        Position(observable.vertex_id, observable.orbit ? PositionKind::ORBIT : PositionKind::CORE),
        observable.refs,
    };
}

}

NestedExpressionRegistry::NestedExpressionRegistry(const Graph& graph) : graph_(graph) {}

void NestedExpressionRegistry::register_plan(Plan& plan, const PlaceholderValues& placeholder_values) {
    register_nested(plan, placeholder_values);
}

size_t NestedExpressionRegistry::register_nested(Plan& plan, const PlaceholderValues& placeholder_values) {
    size_t height = 0;
    for (Plan& child : plan.iter_nested_plans()) {
        height = std::max(height, register_nested(child, placeholder_values));
    }
    auto* nested = boost::get<MatchNestedExpression>(&plan);
    if (nested == nullptr) {
        return height;
    }

    // Nested expressions inside are registered by now, so the text identifies the expression,
    // together with the values of the placeholders it uses.
    std::string key = nested->plan.to_string();
    std::vector<int> placeholders;
    collect_placeholders(nested->plan, placeholders);
    for (int idx : placeholders) {
        const VertexData& value = placeholder_values[idx];
        key += "\n" + *value.type_name.text + ":" + value.value;
    }

    auto [it, inserted] = expression_ids_.try_emplace(key, expressions_.size());
    nested->id = it->second;
    if (inserted) {
        expressions_.push_back(std::make_unique<NestedExpression>(NestedExpression{
            Query{graph_, Expression{}, placeholder_values, Plan{*nested}},
            height + 1,
        }));
    }
    return height + 1;
}

const MatchNestedExpression& NestedExpressionRegistry::nested_plan(size_t id) const {
    return boost::get<MatchNestedExpression>(expressions_[id]->query.plan);
}

QuerySet NestedExpressionRegistry::match(const NestedExpressionStart& start) {
    const auto& expression = *expressions_[start.expression_id];
    return matcher_.match_nested(expression.query, nested_plan(start.expression_id), start.position, start.refs, this);
}

void NestedExpressionRegistry::subscribe(const NestedExpressionStart& start, const Observable& dep) {
    dep_to_starts_[dep].insert(start);
}

void NestedExpressionRegistry::unsubscribe(const NestedExpressionStart& start, const Observable& dep) {
    auto it = dep_to_starts_.find(dep);
    if (it == dep_to_starts_.end()) {
        return;
    }
    it->second.erase(start);
    if (it->second.empty()) {
        dep_to_starts_.erase(it);
        if (boost::get<NestedExpressionSubgraph>(&dep) != nullptr) {
            maybe_unused_.insert(dep);
        }
    }
}

const std::vector<Subgraph>& NestedExpressionRegistry::get(const MatchNestedExpression& plan, const Position& position, const Refs& refs) {
    NestedExpressionStart start{plan.id, position, refs};
    auto it = subgraphs_.find(start);
    if (it != subgraphs_.end()) {
        return it->second;
    }
    QuerySet qs = match(start);
    Observables deps = sorted_deps(std::move(qs.deps));
    for (const Observable& dep : deps) {
        subscribe(start, dep);
    }
    deps_[start] = std::move(deps);
    maybe_unused_.insert(start.observable());
    return subgraphs_.emplace(std::move(start), std::move(qs.subgraphs)).first->second;
}

bool NestedExpressionRegistry::depends_on(const Observable& observable) const {
    return dep_to_starts_.contains(observable);
}

Observations NestedExpressionRegistry::update(const Observations& observations) {
    // The most nested expressions first, so that the ones consisting of them see their new subgraphs.
    std::map<size_t, std::vector<NestedExpressionStart>> by_height;
    absl::flat_hash_set<NestedExpressionStart, std::hash<NestedExpressionStart>> scheduled;
    auto schedule = [&](const Observable& observable) {
        auto it = dep_to_starts_.find(observable);
        if (it == dep_to_starts_.end()) {
            return;
        }
        for (const NestedExpressionStart& start : it->second) {
            if (scheduled.insert(start).second) {
                by_height[expressions_[start.expression_id]->height].push_back(start);
            }
        }
    };
    for (const Observation& o : observations) {
        schedule(o.observable);
    }

    Observations result;
    while (!by_height.empty()) {
        auto starts = std::move(by_height.begin()->second);
        by_height.erase(by_height.begin());
        for (const NestedExpressionStart& start : starts) {
            auto it = subgraphs_.find(start);
            if (it == subgraphs_.end()) {
                continue;
            }
            // Matching may cache other subgraphs, which invalidates iterators, but not references.
            std::vector<Subgraph>& cached = it->second;
            QuerySet qs = match(start);

            // Only deps that changed are updated.
            Observables& old_deps = deps_[start];
            Observables new_deps = sorted_deps(std::move(qs.deps));
            auto old_it = old_deps.begin();
            auto new_it = new_deps.begin();
            while (old_it != old_deps.end() || new_it != new_deps.end()) {
                if (new_it == new_deps.end() || (old_it != old_deps.end() && *old_it < *new_it)) {
                    unsubscribe(start, *old_it++);
                } else if (old_it == old_deps.end() || *new_it < *old_it) {
                    subscribe(start, *new_it++);
                } else {
                    ++old_it;
                    ++new_it;
                }
            }
            old_deps = std::move(new_deps);

            if (qs.subgraphs == cached) {
                // Whatever consists of these subgraphs doesn't need to be rematched.
                continue;
            }
            cached = std::move(qs.subgraphs);
            Observable observable = start.observable();
            result.emplace_back(observable);
            schedule(observable);
        }
    }
    return result;
}

void NestedExpressionRegistry::mark_maybe_unused(const Observable& observable) {
    if (boost::get<NestedExpressionSubgraph>(&observable) != nullptr) {
        maybe_unused_.insert(observable);
    }
}

void NestedExpressionRegistry::evict_unused(const std::function<bool(const Observable&)>& is_observed) {
    while (!maybe_unused_.empty()) {
        const std::vector<Observable> candidates(maybe_unused_.begin(), maybe_unused_.end());
        maybe_unused_.clear();
        for (const Observable& observable : candidates) {
            if (dep_to_starts_.contains(observable) || is_observed(observable)) {
                continue;
            }
            const NestedExpressionStart start = to_start(boost::get<NestedExpressionSubgraph>(observable));
            if (subgraphs_.erase(start) == 0) {
                continue;
            }
            auto deps_it = deps_.find(start);
            if (deps_it != deps_.end()) {
                for (const Observable& dep : deps_it->second) {
                    unsubscribe(start, dep);
                }
                deps_.erase(deps_it);
            }
        }
    }
}

void NestedExpressionRegistry::clear() {
    subgraphs_.clear();
    deps_.clear();
    dep_to_starts_.clear();
    maybe_unused_.clear();
}

}
