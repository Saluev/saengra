#pragma once

#include <map>
#include <set>
#include "vertex.h"

namespace saengra {

using RefName = std::string;

using RefSpace = std::set<RefName>;

class Refs {
public:
    using const_iterator = std::map<RefName, VertexID>::const_iterator;

    const_iterator begin() const { return refs_.begin(); }
    const_iterator end() const { return refs_.end(); }
    const_iterator find(const RefName& name) const { return refs_.find(name); }
    VertexID& operator[](const RefName& name) { return refs_[name]; }

    bool operator==(const Refs& other) const { return refs_ == other.refs_; }
    bool operator<(const Refs& other) const { return refs_ < other.refs_; }

    RefSpace refspace() const {
        RefSpace result;
        for (const auto& [k, v] : refs_) {
            result.insert(result.end(), k);  // hint insertion at end - O(1) amortized
        }
        return result;
    }

    bool can_be_lifted_to(const Refs& complete_refs) const;

private:
    std::map<RefName, VertexID> refs_;
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

}
