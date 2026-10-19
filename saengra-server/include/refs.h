#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "vertex.h"

namespace saengra {

using RefName = std::string;

using RefSpace = std::set<RefName>;

class Refs {
    // Patterns have only a few refs, so a plain list beats a map. It's kept sorted by name,
    // so that iteration order and comparisons are the same as they would be for std::map.
    using Item = std::pair<RefName, VertexID>;

public:
    using const_iterator = std::vector<Item>::const_iterator;

    const_iterator begin() const { return refs_.begin(); }
    const_iterator end() const { return refs_.end(); }

    const_iterator find(const RefName& name) const {
        return std::find_if(refs_.begin(), refs_.end(), [&name](const Item& item) { return item.first == name; });
    }

    // Unlike copying and then inserting, allocates only once.
    Refs copy_and_set(const RefName& name, VertexID value) const {
        Refs result;
        result.refs_.reserve(refs_.size() + 1);
        auto it = refs_.begin();
        for (; it != refs_.end() && it->first < name; ++it) {
            result.refs_.push_back(*it);
        }
        result.refs_.emplace_back(name, value);
        if (it != refs_.end() && it->first == name) {
            ++it;  // replaced by the new value
        }
        result.refs_.insert(result.refs_.end(), it, refs_.end());
        return result;
    }

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
    std::vector<Item> refs_;
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
