#include "observer_container.h"
#include "graph.h"
#include "debug.h"
#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>
#include <google/protobuf/util/message_differencer.h>

using namespace saengra;
using testing::UnorderedElementsAre;

MATCHER_P(EqualsProto, expected, "") {
  return google::protobuf::util::MessageDifferencer::Equals(arg, expected);
}

class ObserversTest : public ::testing::Test {
protected:
    Graph graph;
    ObserverContainer observer_container{graph};

    // Helper to create a basic Observe_Observer request
    void register_observer(
        const std::string& id,
        const std::string& expression,
        bool on_create,
        bool on_change,
        bool on_delete
    ) {
        saengra_api::Observe_Observer request;
        request.set_id(id);
        request.set_expression(expression);
        request.set_on_create(on_create);
        request.set_on_change(on_change);
        request.set_on_delete(on_delete);
        observer_container.register_(request);
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

    saengra_api::Vertex make_proto_vertex(const std::string& type_name, const std::string& value) {
        saengra_api::Vertex vertex;
        vertex.set_type_name(type_name);
        vertex.set_value(value);
        return vertex;
    }

    saengra_api::CommitResponse_Observation make_observation(
        const std::string& id, saengra_api::CommitResponse_Observation_ObservationType type, const std::string& k1, const std::string& t1, const std::string& v1) {
        saengra_api::CommitResponse_Observation obs;
        obs.set_observer_id(id);
        obs.set_type(type);
        (*obs.mutable_variables())[k1] = make_proto_vertex(t1, v1);
        return obs;
    }

    saengra_api::CommitResponse_Observation make_create_observation(
        const std::string& id, const std::string& k1, const std::string& t1, const std::string& v1) {
        return make_observation(id, saengra_api::CommitResponse_Observation::ON_CREATE, k1, t1, v1);
    }

    saengra_api::CommitResponse_Observation make_change_observation(
        const std::string& id, const std::string& k1, const std::string& t1, const std::string& v1) {
        return make_observation(id, saengra_api::CommitResponse_Observation::ON_CHANGE, k1, t1, v1);
    }

    saengra_api::CommitResponse_Observation make_delete_observation(
        const std::string& id, const std::string& k1, const std::string& t1, const std::string& v1) {
        return make_observation(id, saengra_api::CommitResponse_Observation::ON_DELETE, k1, t1, v1);
    }
};

TEST_F(ObserversTest, ObserveNewVertex) {
    graph.update({
        make_add_vertex("person", "Alice"),
        make_add_vertex("person", "Bob"),
    });
    graph.apply();

    register_observer("any_vertex.on_create", "* as p", /*on_create=*/true, false, false);

    graph.update({
        make_add_vertex("person", "Bob"),
        make_add_vertex("person", "Sidney"),
    });

    auto observations = observer_container.observe_proto(graph.apply());
    ASSERT_THAT(observations, UnorderedElementsAre(
        EqualsProto(make_create_observation("any_vertex.on_create", "p", "person", "Sidney"))
    ));
}

TEST_F(ObserversTest, ObserveRemovedVertex) {
    graph.update({
        make_add_vertex("person", "Alice"),
        make_add_vertex("person", "Bob"),
    });
    graph.apply();

    register_observer("any_vertex.on_delete", "* as p", false, false, /*on_delete=*/true);

    graph.update({
        make_remove_vertex("person", "Bob"),
        make_add_vertex("person", "Sidney"),
    });

    auto observations = observer_container.observe_proto(graph.apply());
    ASSERT_THAT(observations, UnorderedElementsAre(
        EqualsProto(make_delete_observation("any_vertex.on_delete", "p", "person", "Bob"))
    ));
}

TEST_F(ObserversTest, ObserveChangedVertexEdges) {
    graph.update({
        make_add_vertex("person", "Alice"),
        make_add_vertex("person", "Bob"),
        make_add_vertex("person", "Sidney"),
        make_add_edge("person", "Bob", "knows", "person", "Alice"),
    });
    graph.apply();

    register_observer("any_vertex.on_change", "* as p (all: -knows>)", false, /*on_change=*/true, false);

    graph.update({
        make_add_edge("person", "Bob", "knows", "person", "Sidney"),
    });

    auto observations = observer_container.observe_proto(graph.apply());
    ASSERT_THAT(observations, UnorderedElementsAre(
        EqualsProto(make_change_observation("any_vertex.on_change", "p", "person", "Bob"))
    ));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
