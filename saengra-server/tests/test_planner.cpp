#include "parser.h"
#include "planner.h"
#include <gtest/gtest.h>

using namespace saengra;

// Plans are compared by their string representation, where `* as x` sets ref x and `* == x`
// only matches the vertex against its value.
class PlannerTest : public ::testing::Test {
protected:
    Graph graph_;
    Parser parser_{graph_};
    Planner planner_;

    std::string plan(const std::string& expression) {
        return planner_.plan(parser_.parse(expression)).to_string();
    }
};

// ============================================================================
// Setting and matching refs
// ============================================================================

TEST_F(PlannerTest, FirstOccurrenceSetsRefAndNextOnesMatchIt) {
    EXPECT_EQ(plan("user as u -friend> user as f -friend> u"),
              "Concatenation(Path(user as u -friend-> user as f), EdgeToVertex(-friend-> * == u))");
}

TEST_F(PlannerTest, LeadingReferenceSetsRef) {
    EXPECT_EQ(plan("x -c> * as x"), "Concatenation(Vertex(* as x), EdgeToVertex(-c-> * == x))");
}

TEST_F(PlannerTest, RefSetInAllAlternativesIsBoundAfterThem) {
    EXPECT_EQ(plan("? (-a> * as x | -b> * as x) -c> x"),
              "Concatenation(Vertex(?), Or(Path(-a-> * as x), Path(-b-> * as x)), EdgeToVertex(-c-> * == x))");
}

TEST_F(PlannerTest, RefSetInSomeAlternativesIsNotBoundAfterThem) {
    EXPECT_EQ(plan("? (-a> * as x | -b> *) -c> x"),
              "Concatenation(Vertex(?), Or(Path(-a-> * as x), Path(-b-> *)), Path(-c-> * as x))");
}

TEST_F(PlannerTest, AndOperandsDontSeeRefsOfEachOtherButRefsAreBoundAfterThem) {
    EXPECT_EQ(plan("? (-a> x & -b> * as x) -c> x"),
              "Concatenation(Vertex(?), And(Path(-a-> * as x), Path(-b-> * as x)), EdgeToVertex(-c-> * == x))");
}

TEST_F(PlannerTest, RefSetInOptionalBranchIsNotBoundAfterIt) {
    EXPECT_EQ(plan("? (maybe: -z> * as x) -a> x"),
              "Concatenation(Vertex(?), Maybe(Path(-z-> * as x)), Path(-a-> * as x))");
}

TEST_F(PlannerTest, RefExcludedByNegationIsNotBoundAfterIt) {
    EXPECT_EQ(plan("? (?! -a> * as x) -b> x"),
              "Concatenation(Vertex(?), Unless(Path(-a-> * as x)), Path(-b-> * as x))");
}

TEST_F(PlannerTest, RefSetInLookaheadIsBoundAfterIt) {
    EXPECT_EQ(plan("? (?= -a> * as x) -b> x"),
              "Concatenation(Vertex(?), If(Path(-a-> * as x)), EdgeToVertex(-b-> * == x))");
}

TEST_F(PlannerTest, RefSetInSkipIsBoundAfterIt) {
    EXPECT_EQ(plan("? (?: -a> * as x) -b> x"),
              "Concatenation(Vertex(?), Skip(Path(-a-> * as x)), EdgeToVertex(-b-> * == x))");
}

TEST_F(PlannerTest, RefSetInAllIsBoundAfterIt) {
    EXPECT_EQ(plan("? (all: -a> * as x) -b> x"),
              "Concatenation(Vertex(?), All(Path(-a-> * as x)), EdgeToVertex(-b-> * == x))");
}

// ============================================================================
// Paths and edges to known vertices
// ============================================================================

TEST_F(PlannerTest, PathStartingWithVertex) {
    EXPECT_EQ(plan("user -friend> user <friend- user"), "Path(user -friend-> user <-friend- user)");
}

TEST_F(PlannerTest, PathStartingWithEdge) {
    EXPECT_EQ(plan("? (?= -friend> user <friend- user)"),
              "Concatenation(Vertex(?), IfWithoutSetRef(Path(-friend-> user <-friend- user)))");
}

TEST_F(PlannerTest, NestedConcatenationsJoinPath) {
    EXPECT_EQ(plan("user (-friend> user) -friend> user"), "Path(user -friend-> user -friend-> user)");
}

TEST_F(PlannerTest, AdjacentEdgesBreakPath) {
    EXPECT_EQ(plan("user -friend> -friend> user"), "Concatenation(Path(user -friend->), Path(-friend-> user))");
}

TEST_F(PlannerTest, SingleVertexIsNotPath) {
    EXPECT_EQ(plan("user"), "Vertex(user)");
}

TEST_F(PlannerTest, EdgeToPlaceholder) {
    EXPECT_EQ(plan("unit -owned_by> ?"), "Concatenation(Vertex(unit), EdgeToVertex(-owned_by-> ?))");
}

TEST_F(PlannerTest, EdgeToMatchedRef) {
    EXPECT_EQ(plan("user as u -friend> user -friend> u -friend> user"),
              "Concatenation(Path(user as u -friend-> user), EdgeToVertex(-friend-> * == u), Path(-friend-> user))");
}

TEST_F(PlannerTest, EdgeToSetRefIsPath) {
    EXPECT_EQ(plan("user -friend> user as f"), "Path(user -friend-> user as f)");
}

// ============================================================================
// Operations without setting refs
// ============================================================================

TEST_F(PlannerTest, AndWithoutSetRef) {
    EXPECT_EQ(plan("? (-a> * & -b> *)"), "Concatenation(Vertex(?), AndWithoutSetRef(Path(-a-> *), Path(-b-> *)))");
}

TEST_F(PlannerTest, AndMatchingBoundRefsDoesntSetRefs) {
    EXPECT_EQ(plan("? as x (-a> x & -b> *)"),
              "Concatenation(Vertex(? as x), AndWithoutSetRef(EdgeToVertex(-a-> * == x), Path(-b-> *)))");
}

TEST_F(PlannerTest, AndSettingRef) {
    EXPECT_EQ(plan("? (-a> * as x & -b> *)"), "Concatenation(Vertex(?), And(Path(-a-> * as x), Path(-b-> *)))");
}

TEST_F(PlannerTest, AllWithoutSetRef) {
    EXPECT_EQ(plan("? (all: -a> * | -b> *)"),
              "Concatenation(Vertex(?), AllWithoutSetRef(Or(Path(-a-> *), Path(-b-> *))))");
}

TEST_F(PlannerTest, AllOfAndWithoutSetRef) {
    EXPECT_EQ(plan("? (all: -a> * & -b> *)"),
              "Concatenation(Vertex(?), AllWithoutSetRef(AndWithoutSetRef(Path(-a-> *), Path(-b-> *))))");
}

TEST_F(PlannerTest, AllOfAnd) {
    EXPECT_EQ(plan("? (all: -a> * as x & -b> x)"),
              "Concatenation(Vertex(?), AllAnd(Path(-a-> * as x), Path(-b-> * as x)))");
}

TEST_F(PlannerTest, AllSettingRef) {
    EXPECT_EQ(plan("? (all: -s> * | -e> * as z)"),
              "Concatenation(Vertex(?), All(Or(Path(-s-> *), Path(-e-> * as z))))");
}

TEST_F(PlannerTest, IfWithoutSetRef) {
    EXPECT_EQ(plan("? as x (?= -a> x)"), "Concatenation(Vertex(? as x), IfWithoutSetRef(EdgeToVertex(-a-> * == x)))");
}

TEST_F(PlannerTest, IfSettingRef) {
    EXPECT_EQ(plan("? (?= -a> * as x)"), "Concatenation(Vertex(?), If(Path(-a-> * as x)))");
}

TEST_F(PlannerTest, UnlessWithoutRefs) {
    EXPECT_EQ(plan("? (?! -battle>)"), "Concatenation(Vertex(?), UnlessWithoutSetNonLocalRef(Edge(-battle->)))");
}

TEST_F(PlannerTest, UnlessSettingLocalRef) {
    EXPECT_EQ(plan("tile as t (?! <stands_on- unit -owned_by> player as q & -city> city -owned_by> q)"),
              "Concatenation(Vertex(tile as t), UnlessWithoutSetNonLocalRef(And("
              "Path(<-stands_on- unit -owned_by-> player as q), Path(-city-> city -owned_by-> * as q))))");
}

TEST_F(PlannerTest, UnlessMatchingBoundSharedRef) {
    EXPECT_EQ(plan("? -a> * as x (?! -b> x)"),
              "Concatenation(Path(? -a-> * as x), UnlessWithoutSetNonLocalRef(EdgeToVertex(-b-> * == x)))");
}

TEST_F(PlannerTest, UnlessSettingSharedRef) {
    EXPECT_EQ(plan("? (?! -a> * as x) -b> * as x"),
              "Concatenation(Vertex(?), Unless(Path(-a-> * as x)), Path(-b-> * as x))");
}

TEST_F(PlannerTest, ExclusionsOfSharedRefsSetRefs) {
    // The inner negation excludes values of x, so the lookahead changes refs.
    EXPECT_EQ(plan("? (?= (?! -a> * as x)) -b> x"),
              "Concatenation(Vertex(?), If(Unless(Path(-a-> * as x))), Path(-b-> * as x))");
}

TEST_F(PlannerTest, LocalRefsOfNegationDontSetRefs) {
    EXPECT_EQ(plan("? (?= (?! -a> * as y))"),
              "Concatenation(Vertex(?), IfWithoutSetRef(UnlessWithoutSetNonLocalRef(Path(-a-> * as y))))");
}
