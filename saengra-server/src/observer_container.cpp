//#include "debug.h"
#include <stdexcept>
#include "logging.h"
#include "observer_container.h"

namespace saengra {

PlaceholderValues convert_placeholder_values(Graph& graph, const saengra_api::Observe_Observer& request) {
    PlaceholderValues result;
    result.reserve(request.placeholder_values_size());
    for(size_t i = 0; i < request.placeholder_values_size(); ++i) {
        const auto& placeholder_value = request.placeholder_values(i);
        const auto type_name = graph.internalize_type_name(placeholder_value.type_name());
        result.emplace_back(type_name, placeholder_value.value());
    }
    return result;
}

Observer::Observer(ObserverID id, Graph& graph, const saengra_api::Observe_Observer& request):
    id_(id),
    graph_(graph),
    matcher_(
        Matcher(),
        make_query(
            graph,
            Parser(graph).parse(request.expression()),
            convert_placeholder_values(graph, request)
        )
    ),
    on_create_(request.on_create()),
    on_change_(request.on_change()),
    on_delete_(request.on_delete()),
    with_subgraphs_(request.with_subgraphs()) {
}

Observer::Observer(ObserverID id, Graph& graph, const std::string& expression,
                   const PlaceholderValues& placeholder_values,
                   bool on_create, bool on_change, bool on_delete, bool with_subgraphs):
    id_(id),
    graph_(graph),
    matcher_(
        Matcher(),
        make_query(
            graph,
            Parser(graph).parse(expression),
            placeholder_values
        )
    ),
    on_create_(on_create),
    on_change_(on_change),
    on_delete_(on_delete),
    with_subgraphs_(with_subgraphs) {
}

static void convert_to_proto_vertex(const StoredVertex& vertex, saengra_api::Vertex& proto) {
    proto.set_type_name(*vertex.type_name.text);
    proto.set_value(vertex.value);
}

static void convert_to_proto_position(const Position& position, saengra_api::Position& proto) {
    convert_to_proto_vertex(*position.vertex_id, *proto.mutable_vertex());
    proto.set_kind(position.kind == PositionKind::CORE
        ? saengra_api::Position_PositionKind_CORE
        : saengra_api::Position_PositionKind_ORBIT);
}

void convert_to_proto_subgraph(const Subgraph& subgraph, saengra_api::Subgraph& proto) {
    convert_to_proto_position(subgraph.start_position, *proto.mutable_start_position());
    for (const Position& end_pos : subgraph.end_positions) {
        convert_to_proto_position(end_pos, *proto.add_end_positions());
    }
    for (VertexID vid : subgraph.vertices) {
        convert_to_proto_vertex(*vid, *proto.add_vertices());
    }
    for (const Edge& edge : subgraph.edges) {
        auto* proto_edge = proto.add_edges();
        convert_to_proto_vertex(*edge.from, *proto_edge->mutable_from());
        proto_edge->set_label(*edge.label.text);
        convert_to_proto_vertex(*edge.to, *proto_edge->mutable_to());
    }
    auto& proto_refs = *proto.mutable_refs();
    for (const auto& [name, vid] : subgraph.refs.positives()) {
        convert_to_proto_vertex(*vid, proto_refs[name]);
    }
}

saengra_api::CommitResponse_Observation Observer::convert_to_proto_observation(const Subgraph& subgraph, saengra_api::CommitResponse_Observation_ObservationType type) const {
    saengra_api::CommitResponse_Observation obs;
    obs.set_observer_id(id_);
    obs.set_type(type);
    for (const auto& [ref_name, vertex_id] : subgraph.refs.positives()) {
        convert_to_proto_vertex(*vertex_id, (*obs.mutable_variables())[ref_name]);
    }
    if (with_subgraphs_) {
        convert_to_proto_subgraph(subgraph, *obs.mutable_subgraph());
    }
    return obs;
}

void Observer::convert_to_proto_observations(const Subgraphs& subgraphs, saengra_api::CommitResponse_Observation_ObservationType type, std::vector<saengra_api::CommitResponse_Observation>& dest) const {
    for (const auto& subgraph : subgraphs) {
        dest.push_back(convert_to_proto_observation(subgraph, type));
    }
}

void Observer::convert_to_proto_observations(const ChangedSubgraphs& changed_subgraphs, saengra_api::CommitResponse_Observation_ObservationType type, std::vector<saengra_api::CommitResponse_Observation>& dest) const {
    for (const auto& changed_subgraph : changed_subgraphs) {
        dest.push_back(convert_to_proto_observation(changed_subgraph.after, type));
    }
}

SubscriptionChanges Observer::observe_proto(const Observations& observations, std::vector<saengra_api::CommitResponse_Observation>& dest) {
    auto update = matcher_.match_incrementally(observations);

    spdlog::debug("Observer '{}'\n    observed {} added subgraphs, {} changed subgraphs, {} removed subgraphs",
        BRIGHT_WHITE(id_),
        update.added_subgraphs.size() > 0 ? BRIGHT_GREEN(update.added_subgraphs.size()) : BRIGHT_WHITE(update.added_subgraphs.size()),
        update.changed_subgraphs.size() > 0 ? YELLOW(update.changed_subgraphs.size()) : BRIGHT_WHITE(update.changed_subgraphs.size()),
        update.removed_subgraphs.size() > 0 ? RED(update.removed_subgraphs.size()) : BRIGHT_WHITE(update.removed_subgraphs.size())
    );

    if (on_create_) {
        convert_to_proto_observations(update.added_subgraphs, saengra_api::CommitResponse_Observation::ON_CREATE, dest);
    }

    if (on_change_) {
        convert_to_proto_observations(update.changed_subgraphs, saengra_api::CommitResponse_Observation::ON_CHANGE, dest);
    }

    if (on_delete_) {
        convert_to_proto_observations(update.removed_subgraphs, saengra_api::CommitResponse_Observation::ON_DELETE, dest);
    }

    return SubscriptionChanges{std::move(update.added_deps), std::move(update.removed_deps)};
}

NativeObservation Observer::convert_to_native_observation(const Subgraph& subgraph, NativeObservationType type) const {
    NativeObservation obs;
    obs.observer_id = id_;
    obs.type = type;
    obs.variables = subgraph.refs;
    if (with_subgraphs_) {
        obs.subgraph = subgraph;
    }
    return obs;
}

void Observer::convert_to_native_observations(const Subgraphs& subgraphs, NativeObservationType type, std::vector<NativeObservation>& dest) const {
    for (const auto& subgraph : subgraphs) {
        dest.push_back(convert_to_native_observation(subgraph, type));
    }
}

void Observer::convert_to_native_observations(const ChangedSubgraphs& changed_subgraphs, NativeObservationType type, std::vector<NativeObservation>& dest) const {
    for (const auto& changed_subgraph : changed_subgraphs) {
        dest.push_back(convert_to_native_observation(changed_subgraph.after, type));
    }
}

SubscriptionChanges Observer::observe_native(const Observations& observations, std::vector<NativeObservation>& dest) {
    auto update = matcher_.match_incrementally(observations);

    spdlog::debug("Observer '{}'\n    observed {} added subgraphs, {} changed subgraphs, {} removed subgraphs",
        BRIGHT_WHITE(id_),
        update.added_subgraphs.size() > 0 ? BRIGHT_GREEN(update.added_subgraphs.size()) : BRIGHT_WHITE(update.added_subgraphs.size()),
        update.changed_subgraphs.size() > 0 ? YELLOW(update.changed_subgraphs.size()) : BRIGHT_WHITE(update.changed_subgraphs.size()),
        update.removed_subgraphs.size() > 0 ? RED(update.removed_subgraphs.size()) : BRIGHT_WHITE(update.removed_subgraphs.size())
    );

    if (on_create_) {
        convert_to_native_observations(update.added_subgraphs, NativeObservationType::ON_CREATE, dest);
    }

    if (on_change_) {
        convert_to_native_observations(update.changed_subgraphs, NativeObservationType::ON_CHANGE, dest);
    }

    if (on_delete_) {
        convert_to_native_observations(update.removed_subgraphs, NativeObservationType::ON_DELETE, dest);
    }

    return SubscriptionChanges{std::move(update.added_deps), std::move(update.removed_deps)};
}

ObserverContainer::ObserverContainer(Graph& graph):
    graph_(graph) {
}

void ObserverContainer::register_(const saengra_api::Observe_Observer& request) {
    ObserverID observer_id = request.id();
    if (observers_.find(observer_id) != observers_.end()) {
        throw std::runtime_error("Observer with ID '" + observer_id + "' is already registered");
    }

    auto observer = std::make_unique<Observer>(observer_id, graph_, request);
    auto initial_subscriptions = observer->reinitialize();
    observers_[observer_id] = std::move(observer);
    apply_subscription_changes(observer_id, initial_subscriptions);
}

void ObserverContainer::register_native(const std::string& id, const std::string& expression,
                                         const PlaceholderValues& placeholder_values,
                                         bool on_create, bool on_change, bool on_delete, bool with_subgraphs) {
    ObserverID observer_id = id;
    if (observers_.find(observer_id) != observers_.end()) {
        throw std::runtime_error("Observer with ID '" + observer_id + "' is already registered");
    }

    auto observer = std::make_unique<Observer>(observer_id, graph_, expression, placeholder_values, on_create, on_change, on_delete, with_subgraphs);
    auto initial_subscriptions = observer->reinitialize();
    observers_[observer_id] = std::move(observer);
    apply_subscription_changes(observer_id, initial_subscriptions);
}

struct ObserverWithObservations {
    ObserverWithObservations(Observer* observer) : observer(observer) {}

    Observer* observer;
    Observations observations;
};

std::vector<saengra_api::CommitResponse_Observation> ObserverContainer::observe_proto(const Observations& observations) {
//    std::cerr << "Observing " << observations.size() << " observations:" << std::endl;
//    for (const auto& observation : observations) {
//        std::cerr << "    " << observation << std::endl;
//    }
//    std::cerr << "Current subscriptions:" << std::endl;
//    for (const auto& [observable, observers] : subscriptions_) {
//        std::cerr << "    " << observable << ":";
//        for (Observer* observer : observers) {
//            std::cerr << " " << observer->get_id();
//        }
//        std::cerr << std::endl;
//    }

    std::vector<saengra_api::CommitResponse_Observation> result;

    std::unordered_map<ObserverID, ObserverWithObservations> id_to_obs;
    for (const Observation& observation : observations) {
        auto sub_it = subscriptions_.find(observation.observable);
        if (sub_it == subscriptions_.end()) {
            continue;
        }
        for (Observer* observer : sub_it->second) {
            auto [it, inserted] = id_to_obs.try_emplace(observer->get_id(), observer);
            it->second.observations.push_back(observation);
        }
    }

    for (auto [subscriber_id, sub_and_obs] : id_to_obs) {
//        std::cerr << "Observer " << subscriber_id << " is observing " << sub_and_obs.observations.size() << " observations" << std::endl;
        auto subscription_changes = sub_and_obs.observer->observe_proto(sub_and_obs.observations, result);
        apply_subscription_changes(subscriber_id, subscription_changes);
    }
    return result;
}

std::vector<NativeObservation> ObserverContainer::observe_native(const Observations& observations) {
    std::vector<NativeObservation> result;

    std::unordered_map<ObserverID, ObserverWithObservations> id_to_obs;
    for (const Observation& observation : observations) {
        auto sub_it = subscriptions_.find(observation.observable);
        if (sub_it == subscriptions_.end()) {
            continue;
        }
        for (Observer* observer : sub_it->second) {
            auto [it, inserted] = id_to_obs.try_emplace(observer->get_id(), observer);
            it->second.observations.push_back(observation);
        }
    }

    for (auto [subscriber_id, sub_and_obs] : id_to_obs) {
        auto subscription_changes = sub_and_obs.observer->observe_native(sub_and_obs.observations, result);
        apply_subscription_changes(subscriber_id, subscription_changes);
    }
    return result;
}

void ObserverContainer::apply_subscription_changes(ObserverID observer_id, const SubscriptionChanges& changes) {
    if (changes.added_observables.empty() && changes.removed_observables.empty()) {
        return;
    }
    auto& observer = *observers_.find(observer_id)->second;
    for (const auto& observable : changes.added_observables) {
        subscriptions_[observable].insert(&observer);
    }
    for (const auto& observable : changes.removed_observables) {
        auto obs_it = subscriptions_.find(observable);
        if (obs_it != subscriptions_.end()) {
            obs_it->second.erase(&observer);
            if (obs_it->second.empty()) {
                subscriptions_.erase(obs_it);
            }
        }
    }
}

void ObserverContainer::reinitialize_all() {
    subscriptions_.clear();
    for (auto& observer : observers_) {
        const auto changes = observer.second->reinitialize();
        apply_subscription_changes(observer.first, changes);
    }
}

}
