#include "observable.h"
#include <gtest/gtest.h>
#include <string>

using namespace saengra;

class ObservablesTest : public ::testing::Test {
};

TEST_F(ObservablesTest, CreateEqualityCheck) {
    const Observable x = NewVertex{};
    const Observable y = NewEdge{};
    ASSERT_NE(x, y);
}

TEST_F(ObservablesTest, CreateUnorderedSet) {
    const Observable x = NewVertex{};
    const Observable y = NewEdge{};

    std::unordered_set<Observable> s;
    s.insert(x);
    s.insert(y);
    ASSERT_EQ(s.size(), 2);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
