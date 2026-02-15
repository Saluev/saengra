#include "vertices_container.h"
#include <gtest/gtest.h>
#include <string>

using namespace saengra;

class VerticesContainerTest : public ::testing::Test {
protected:
    MutableVerticesContainer container;
};

TEST_F(VerticesContainerTest, InternalizeTypeName) {
    auto t1 = container.internalize_type_name("Foo");
    EXPECT_EQ(container.num_internalized_type_names(), 1);
    EXPECT_EQ(*t1.text, "Foo");

    auto t2 = container.internalize_type_name("Bar");
    EXPECT_EQ(container.num_internalized_type_names(), 2);
    EXPECT_NE(t1.text, t2.text);
    EXPECT_EQ(*t2.text, "Bar");

    auto t3 = container.internalize_type_name("Foo");
    EXPECT_EQ(container.num_internalized_type_names(), 2);
    EXPECT_EQ(t1.text, t3.text);
}

TEST_F(VerticesContainerTest, AddAndDiscardVertex) {
    // Internalize type names
    VertexTypeName person_type = container.internalize_type_name("Person");

    // Create vertices
    std::string name1 = "Alice";
    std::string name2 = "Bob";
    VertexData v1{person_type, name1};
    VertexData v2{person_type, name2};

    // Add vertices
    container.add_vertex(v1);
    container.add_vertex(v2);

    ASSERT_NE(container.get_vertex_id(v1), std::nullopt);
    ASSERT_NE(container.get_vertex_id(v2), std::nullopt);;

    // Check they're in just_added
    auto just_added = container.iter_just_added();
    EXPECT_EQ(just_added.size(), 2);

    // Apply changes
    container.apply();
    EXPECT_EQ(container.iter_just_added().size(), 0);

    // Discard one vertex
    container.discard_vertex(v1);
    auto just_removed = container.iter_just_removed();
    EXPECT_EQ(just_removed.size(), 1);

    // Try adding the same vertex again (should re-add it, canceling removal)
    container.add_vertex(v1);
    EXPECT_EQ(container.iter_just_added().size(), 0); // Cancels out
    EXPECT_EQ(container.iter_just_removed().size(), 0);
}

TEST_F(VerticesContainerTest, ApplyAndCommit) {
    VertexTypeName city_type = container.internalize_type_name("City");
    std::string city1 = "NYC";
    std::string city2 = "LA";
    VertexData v1{city_type, city1};
    VertexData v2{city_type, city2};

    // Add vertices
    container.add_vertex(v1);
    container.add_vertex(v2);
    EXPECT_EQ(container.iter_just_added().size(), 2);

    // Apply
    container.apply();
    EXPECT_EQ(container.iter_just_added().size(), 0);

    // Commit
    container.commit();

    // Now discard a vertex and commit
    container.discard_vertex(v1);
    container.apply();
    container.commit();
}

TEST_F(VerticesContainerTest, Rollback) {
    VertexTypeName animal_type = container.internalize_type_name("Animal");
    std::string animal1 = "Cat";
    std::string animal2 = "Dog";
    VertexData v1{animal_type, animal1};
    VertexData v2{animal_type, animal2};

    // Add and commit first vertex
    container.add_vertex(v1);
    container.apply();
    container.commit();

    // Add second vertex but don't commit
    container.add_vertex(v2);
    container.apply();

    // Discard first vertex but don't commit
    container.discard_vertex(v1);

    // Rollback should restore state
    container.rollback();

    // After rollback:
    // - v1 should still be present (discard was rolled back)
    // - v2 should not be present (add was rolled back)
    EXPECT_EQ(container.iter_just_added().size(), 0);
    EXPECT_EQ(container.iter_just_removed().size(), 0);
}

TEST_F(VerticesContainerTest, IterByTypeName) {
    VertexTypeName person_type = container.internalize_type_name("Person");
    VertexTypeName city_type = container.internalize_type_name("City");

    std::string name1 = "Alice";
    std::string name2 = "Bob";
    std::string city1 = "NYC";

    VertexData v1{person_type, name1};
    VertexData v2{person_type, name2};
    VertexData v3{city_type, city1};

    container.add_vertex(v1);
    container.add_vertex(v2);
    container.add_vertex(v3);

    auto persons = container.iter_present_by_type_name(person_type);
    auto cities = container.iter_present_by_type_name(city_type);

    EXPECT_EQ(persons.size(), 2);
    EXPECT_EQ(cities.size(), 1);

    // Test iteration
    int person_count = 0;
    for (auto vertex_id : persons) {
        person_count++;
        EXPECT_NE(vertex_id, nullptr); // IDs should be positive
    }
    EXPECT_EQ(person_count, 2);
}

TEST_F(VerticesContainerTest, DuplicateAdd) {
    VertexTypeName item_type = container.internalize_type_name("Item");
    std::string item_name = "Widget";
    VertexData v{item_type, item_name};

    container.add_vertex(v);
    EXPECT_EQ(container.iter_just_added().size(), 1);

    // Try adding the same vertex again
    container.add_vertex(v);
    EXPECT_EQ(container.iter_just_added().size(), 1); // Should still be 1
}

TEST_F(VerticesContainerTest, DiscardNonExistentVertex) {
    VertexTypeName type = container.internalize_type_name("Test");
    std::string value = "NonExistent";
    VertexData v{type, value};

    // Discard a vertex that was never added
    container.discard_vertex(v);

    // Should have no effect
    EXPECT_EQ(container.iter_just_removed().size(), 0);
}

TEST_F(VerticesContainerTest, MultipleApplyCommitCycles) {
    VertexTypeName type = container.internalize_type_name("Number");

    std::string val1 = "1";
    std::string val2 = "2";
    std::string val3 = "3";
    VertexData v1{type, val1};
    VertexData v2{type, val2};
    VertexData v3{type, val3};

    // First cycle
    container.add_vertex(v1);
    container.apply();
    container.commit();

    // Second cycle
    container.add_vertex(v2);
    container.apply();
    container.commit();

    // Third cycle
    container.add_vertex(v3);
    container.discard_vertex(v1);
    container.apply();
    container.commit();

    auto numbers = container.iter_present_by_type_name(type);
    EXPECT_EQ(numbers.size(), 2); // v2 and v3 remain
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
