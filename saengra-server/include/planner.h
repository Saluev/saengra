#pragma once

#include "expression.h"
#include "plan.h"

namespace saengra {

// Converts an expression to a plan, choosing specialized ways of matching its parts depending on
// their structure and on which refs are guaranteed to be set at each point of the expression.
class Planner {
public:
    // With `nest_operands`, operands of operations and of & are nested expressions, whose subgraphs
    // the incremental matcher caches. It's pointless for queries that are matched only once.
    Plan plan(const Expression& expr, bool nest_operands = true) const;
};

} // namespace saengra
