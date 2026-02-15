#pragma once

#include "observable.h"
#include "graph.h"
#include "parser.h"
#include "matcher.h"
#include "incremental_matcher.h"
#include "messages.pb.h"

namespace saengra {

using ObserverID = std::string;

enum class NativeObservationType { ON_CREATE, ON_CHANGE, ON_DELETE };

struct NativeObservation {
    std::string observer_id;
    NativeObservationType type;
    Refs variables;
};

struct SubscriptionChanges {
    Observables added_observables;
    Observables removed_observables;
};

class Observer {
public:
    inline ObserverID get_id() const { return id_; }
    Observer(ObserverID id, Graph& graph, const saengra_api::Observe_Observer& request);
    Observer(ObserverID id, Graph& graph, const std::string& expression,
             const PlaceholderValues& placeholder_values,
             bool on_create, bool on_change, bool on_delete);
    inline SubscriptionChanges reinitialize() { return {matcher_.reinitialize(), {}}; }
    SubscriptionChanges observe_proto(const Observations& observations, std::vector<saengra_api::CommitResponse_Observation>& dest);
    SubscriptionChanges observe_native(const Observations& observations, std::vector<NativeObservation>& dest);

private:
    ObserverID id_;
    const Graph& graph_;
    IncrementalMatcher matcher_;
    bool on_create_;
    bool on_change_;
    bool on_delete_;

    saengra_api::CommitResponse_Observation convert_to_proto_observation(const Subgraph&, saengra_api::CommitResponse_Observation_ObservationType) const;
    void convert_to_proto_observations(const Subgraphs&, saengra_api::CommitResponse_Observation_ObservationType, std::vector<saengra_api::CommitResponse_Observation>&) const;
    void convert_to_proto_observations(const ChangedSubgraphs&, saengra_api::CommitResponse_Observation_ObservationType, std::vector<saengra_api::CommitResponse_Observation>&) const;

    NativeObservation convert_to_native_observation(const Subgraph&, NativeObservationType) const;
    void convert_to_native_observations(const Subgraphs&, NativeObservationType, std::vector<NativeObservation>&) const;
    void convert_to_native_observations(const ChangedSubgraphs&, NativeObservationType, std::vector<NativeObservation>&) const;
};

using Observers = std::set<Observer*>;

class ObserverContainer {
public:
    ObserverContainer(Graph& graph);
    void register_(const saengra_api::Observe_Observer& request);
    void register_native(const std::string& id, const std::string& expression,
                         const PlaceholderValues& placeholder_values,
                         bool on_create, bool on_change, bool on_delete);
    std::vector<saengra_api::CommitResponse_Observation> observe_proto(const Observations& observations);
    std::vector<NativeObservation> observe_native(const Observations& observations);
    void reinitialize_all();
private:
    Graph& graph_;

    std::unordered_map<ObserverID, std::unique_ptr<Observer>> observers_;
    std::unordered_map<Observable, Observers> subscriptions_;

    void apply_subscription_changes(ObserverID observer_id, const SubscriptionChanges& changes);
};

}
