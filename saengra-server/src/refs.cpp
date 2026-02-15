#include "refs.h"

namespace saengra {

bool Refs::can_be_lifted_to(const Refs& complete_refs) const {
    for (const auto& [k, v] : *this) {
        auto it = complete_refs.find(k);
        if (
            it == complete_refs.end()  // not supposed to happen, since `complete_refs` are complete
            || it->second != v
        ) {
            return false;
        }
    }
    return true;
}

RefNamesAndValues collect_all_ref_names_and_values(const std::set<Refs>& all_refs) {
    RefNamesAndValues result;
    for (const auto& refs : all_refs) {
        for (const auto [ref_name, ref_value] : refs) {
            result.names.insert(ref_name);
            result.values[ref_name].insert(ref_value);
        }
    }
    return result;
}

RefsDomain multiply(const RefsDomain& refs_domain, RefName ref_name, std::unordered_set<VertexID> values) {
    RefsDomain result;
    result.reserve(refs_domain.size() * values.size());
    for (const auto& prev_refs : refs_domain) {
        for (const auto& value : values) {
            auto refs = prev_refs;  // copy
            refs[ref_name] = value;
            result.push_back(refs);
        }
    }
    return result;
}

RefsDomain cartesian_product(const std::map<RefName, std::unordered_set<VertexID>>& all_values) {
    RefsDomain result{{}};
    for (const auto& [ref_name, values] : all_values) {
        result = multiply(result, ref_name, values);
    }
    return result;
}

RefSpaceAnalysis find_maximal_refspaces(const std::vector<RefSpace>& refspaces) {
    // TODO one day...
    return RefSpaceAnalysis{};
}

}
