#pragma once

#include <map>
#include <set>
#include "vertex.h"

namespace saengra {

using RefName = std::string;

using RefSpace = std::set<RefName>;

class Refs : public std::map<RefName, VertexID> {
public:
    RefSpace refspace() const {
        RefSpace result;
        for (const auto& [k, v] : *this) {
            result.insert(result.end(), k);  // hint insertion at end - O(1) amortized
        }
        return result;
    }

    bool can_be_lifted_to(const Refs& complete_refs) const;
};

struct RefNamesAndValues {
    std::set<RefName> names;
    std::map<RefName, std::unordered_set<VertexID>> values;
};

RefNamesAndValues collect_all_ref_names_and_values(const std::set<Refs>& refs);

struct RefSpaceAnalysis {
    std::set<RefSpace> maximal_refspaces;
    std::map<RefSpace, std::vector<RefSpace>> refspace_to_maximal_refspaces;
};

using RefsDomain = std::vector<Refs>;

RefsDomain cartesian_product(const std::map<RefName, std::unordered_set<VertexID>>& all_values);

RefSpaceAnalysis find_maximal_refspaces(const std::vector<RefSpace>& refspaces);
/*
"Maximal" here is respective to the non-absolute order of set inclusion.

So, for refspaces {}, {a}, {b}, {a, b}, {b, c} maximal refspaces are {a, b} and {b, c}.

Returns mapping of a refspace to its maximal refspaces. As seen in the example above,
single refspace can be a subset of multiple maximal refspaces.
*/

}
