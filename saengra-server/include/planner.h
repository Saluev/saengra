#pragma once

#include "expression.h"
#include "plan.h"

namespace saengra {

// Converts an expression to a plan, choosing specialized ways of matching its parts depending on
// their structure and on which refs are guaranteed to be set at each point of the expression.
class Planner {
public:
    Plan plan(const Expression& expr) const;
};

} // namespace saengra
