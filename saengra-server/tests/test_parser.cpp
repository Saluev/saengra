#include "parser.h"
#include "expression.h"
#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

using namespace saengra;
using ::testing::ElementsAre;

// Custom matcher for checking edge direction
MATCHER_P(HasDirection, expected_direction, "") {
    auto* edge = boost::get<EdgeExpr>(&arg);
    if (!edge) {
        *result_listener << "is not an EdgeExpr";
        return false;
    }
    if (edge->direction != expected_direction) {
        *result_listener << "has direction ";
        switch (edge->direction) {
            case Direction::Forward: *result_listener << "Forward"; break;
            case Direction::Backward: *result_listener << "Backward"; break;
            case Direction::Both: *result_listener << "Both"; break;
            case Direction::Any: *result_listener << "Any"; break;
        }
        *result_listener << " instead of ";
        switch (expected_direction) {
            case Direction::Forward: *result_listener << "Forward"; break;
            case Direction::Backward: *result_listener << "Backward"; break;
            case Direction::Both: *result_listener << "Both"; break;
            case Direction::Any: *result_listener << "Any"; break;
        }
        return false;
    }
    return true;
}

MATCHER_P(HasPlaceholderIdx, expected_idx, "") {
    auto* vertex = boost::get<VertexExpr>(&arg);
    if (!vertex) {
        *result_listener << "is not a VertexExpr";
        return false;
    }
    if (!vertex->placeholder_idx.has_value()) {
        *result_listener << "does not have a placeholder index";
        return false;
    }
    if (vertex->placeholder_idx != expected_idx) {
        *result_listener << "has placeholder index " << *vertex->placeholder_idx << " instead of " << expected_idx;
        return false;
    }
    return true;
}

class ParserTest : public ::testing::Test {
protected:
    Graph graph_;
    Parser parser_{graph_};

    EdgeLabel label(const std::string& s) {
        return graph_.internalize_label(s);
    }
};

TEST_F(ParserTest, ParseSimpleVertex) {
    auto expr = parser_.parse("Person");
    auto* vertex = boost::get<VertexExpr>(&expr);
    ASSERT_NE(vertex, nullptr);
    EXPECT_TRUE(vertex->type_name.has_value());
    EXPECT_EQ(vertex->type_name.value(), "Person");
    EXPECT_FALSE(vertex->match_ref.has_value());
    EXPECT_FALSE(vertex->set_ref.has_value());
    EXPECT_FALSE(vertex->placeholder_idx.has_value());
}

TEST_F(ParserTest, ParseVertexWithRef) {
    auto expr = parser_.parse("Person as p");
    auto* vertex = boost::get<VertexExpr>(&expr);
    ASSERT_NE(vertex, nullptr);
    EXPECT_TRUE(vertex->type_name.has_value());
    EXPECT_EQ(vertex->type_name.value(), "Person");
    EXPECT_FALSE(vertex->match_ref.has_value());
    EXPECT_TRUE(vertex->set_ref.has_value());
    EXPECT_EQ(vertex->set_ref.value(), "p");
    EXPECT_FALSE(vertex->placeholder_idx.has_value());
}

TEST_F(ParserTest, ParseVertexWithMatchRef) {
    auto expr = parser_.parse("Person as p <friend_of> p");
    auto* concat = boost::get<ConcatenationExpr>(&expr);
    ASSERT_NE(concat, nullptr);
    EXPECT_EQ(concat->operands.size(), 3);

    auto* vertex = boost::get<VertexExpr>(&concat->operands[2]);
    ASSERT_NE(vertex, nullptr);
    EXPECT_FALSE(vertex->type_name.has_value());
    EXPECT_EQ(vertex->match_ref, "p");
}

TEST_F(ParserTest, ParseWildcardVertex) {
    auto expr = parser_.parse("*");
    auto* vertex = boost::get<VertexExpr>(&expr);
    ASSERT_NE(vertex, nullptr);
    EXPECT_FALSE(vertex->type_name.has_value());;
    EXPECT_FALSE(vertex->match_ref.has_value());
    EXPECT_FALSE(vertex->set_ref.has_value());
    EXPECT_FALSE(vertex->placeholder_idx.has_value());
}

TEST_F(ParserTest, ParsePlaceholder) {
    auto expr = parser_.parse("?");
    auto* vertex = boost::get<VertexExpr>(&expr);
    ASSERT_NE(vertex, nullptr);
    EXPECT_FALSE(vertex->type_name.has_value());;
    EXPECT_FALSE(vertex->match_ref.has_value());
    EXPECT_FALSE(vertex->set_ref.has_value());
    EXPECT_EQ(vertex->placeholder_idx, 0);
}

TEST_F(ParserTest, ParseMultiplePlaceholders) {
    auto expr = parser_.parse("? -then> ? -and_then> ?");
    auto* concat = boost::get<ConcatenationExpr>(&expr);
    ASSERT_NE(concat, nullptr);
    EXPECT_EQ(concat->operands.size(), 5);
    EXPECT_THAT(concat->operands[0], HasPlaceholderIdx(0));
    EXPECT_THAT(concat->operands[2], HasPlaceholderIdx(1));
    EXPECT_THAT(concat->operands[4], HasPlaceholderIdx(2));
}

TEST_F(ParserTest, ParseSimpleEdge) {
    auto expr = parser_.parse("-knows->");
    auto* edge = boost::get<EdgeExpr>(&expr);
    ASSERT_NE(edge, nullptr);
    EXPECT_EQ(edge->direction, Direction::Forward);
    EXPECT_THAT(edge->labels, ElementsAre(label("knows")));
}

TEST_F(ParserTest, ParseEdgeWithMultipleLabels) {
    auto expr = parser_.parse("-knows|likes->");
    auto* edge = boost::get<EdgeExpr>(&expr);
    ASSERT_NE(edge, nullptr);
    EXPECT_EQ(edge->direction, Direction::Forward);
    EXPECT_THAT(edge->labels, ElementsAre(label("knows"), label("likes")));
}

TEST_F(ParserTest, DirectionSemantics) {
    // Comprehensive test for all direction semantics

    // Forward: -> or >
    EXPECT_THAT(parser_.parse("--label->"), HasDirection(Direction::Forward));
    EXPECT_THAT(parser_.parse("-label>"), HasDirection(Direction::Forward));

    // Backward: <- or <
    EXPECT_THAT(parser_.parse("<-label--"), HasDirection(Direction::Backward));
    EXPECT_THAT(parser_.parse("<label-"), HasDirection(Direction::Backward));

    // Both (bidirectional): <>
    EXPECT_THAT(parser_.parse("<label>"), HasDirection(Direction::Both));
    EXPECT_THAT(parser_.parse("<-label->"), HasDirection(Direction::Both));

    // Any: --
    EXPECT_THAT(parser_.parse("-label-"), HasDirection(Direction::Any));
    EXPECT_THAT(parser_.parse("--label--"), HasDirection(Direction::Any));
}

TEST_F(ParserTest, ParseConcatenation) {
    auto expr = parser_.parse("Person -knows-> City");
    auto* concat = boost::get<ConcatenationExpr>(&expr);
    ASSERT_NE(concat, nullptr);
    EXPECT_EQ(concat->operands.size(), 3);
}

TEST_F(ParserTest, ParseOr) {
    auto expr = parser_.parse("Person | City");
    auto* or_expr = boost::get<OrExpr>(&expr);
    ASSERT_NE(or_expr, nullptr);
    EXPECT_EQ(or_expr->operands.size(), 2);
}

TEST_F(ParserTest, ParseAnd) {
    auto expr = parser_.parse("Person & City");
    auto* and_expr = boost::get<AndExpr>(&expr);
    ASSERT_NE(and_expr, nullptr);
    EXPECT_EQ(and_expr->operands.size(), 2);
}

TEST_F(ParserTest, ParseGroup) {
    auto expr = parser_.parse("(Person)");
    auto* vertex = boost::get<VertexExpr>(&expr);
    ASSERT_NE(vertex, nullptr);
    EXPECT_EQ(vertex->type_name.value(), "Person");
}

TEST_F(ParserTest, ParseAllOperation) {
    auto expr = parser_.parse("(all: Person)");
    auto* op = boost::get<OperationExpr>(&expr);
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->op_type, OperationType::All);
}

TEST_F(ParserTest, ParseIfOperation) {
    auto expr = parser_.parse("(if: Person)");
    auto* op = boost::get<OperationExpr>(&expr);
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->op_type, OperationType::If);
}

TEST_F(ParserTest, ParseMaybeOperation) {
    auto expr = parser_.parse("(maybe: Person)");
    auto* op = boost::get<OperationExpr>(&expr);
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->op_type, OperationType::Maybe);
}

TEST_F(ParserTest, ParseRepetition) {
    auto expr = parser_.parse("(repeat 3x: Person)");
    auto* rep = boost::get<RepetitionExpr>(&expr);
    ASSERT_NE(rep, nullptr);
    EXPECT_EQ(rep->min_repeats, 3);
    EXPECT_EQ(rep->max_repeats, 3);
}

TEST_F(ParserTest, ParseRepetitionRange) {
    auto expr = parser_.parse("(repeat 2-5x: Person)");
    auto* rep = boost::get<RepetitionExpr>(&expr);
    ASSERT_NE(rep, nullptr);
    EXPECT_EQ(rep->min_repeats, 2);
    EXPECT_EQ(rep->max_repeats, 5);
}

TEST_F(ParserTest, ParseComplexExpression) {
    auto expr = parser_.parse("Person -knows-> (City | Country)");
    auto* concat = boost::get<ConcatenationExpr>(&expr);
    ASSERT_NE(concat, nullptr);
    EXPECT_EQ(concat->operands.size(), 3);

    // Third operand should be an Or expression
    auto* or_expr = boost::get<OrExpr>(&concat->operands[2]);
    ASSERT_NE(or_expr, nullptr);
    EXPECT_EQ(or_expr->operands.size(), 2);
}

TEST_F(ParserTest, ParseReallyComplexExpression) {
    auto expr = parser_.parse(
        "tile_projection as tp"
        "    (?= -player> player -researched_tile_action_kinds> ?)"
        "(all:"
        "    -owned_by|has_fresh_water> |"
        "    -projection_of> tile (all:"
        "        -owned_by|has_fresh_water> |"
        "        -edge_segments> segment -along>"
        "    ) |"
        "    -adjacent> tile_projection (maybe: "
        "        -owned_by> |"
        "        -projection_of> tile (-owned_by> | -city> city -owned_by>)"
        "    ) |"
        "    -edge_segment_projections> segment_projection -along>"
        ")"
    );
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
