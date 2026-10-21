#include "refs.h"

namespace saengra {

static Literal negated(const Literal& literal) {
    return Literal{literal.name, !literal.negative, literal.value};
}

std::optional<Refs> Refs::normalized(Literals literals) {
    std::sort(literals.begin(), literals.end());
    Refs result;
    result.literals_.reserve(literals.size());
    for (auto it = literals.begin(); it != literals.end();) {
        const RefName& name = it->name;
        auto group_end = std::find_if(it, literals.end(), [&name](const Literal& l) { return l.name != name; });
        if (!it->negative) {
            // Positive literals go first. The ref can't be set to two vertices, or to an excluded one;
            // exclusions of other vertices follow from the positive literal and are dropped.
            for (auto jt = it; jt != group_end; ++jt) {
                if (!jt->negative ? jt->value != it->value : jt->value == it->value) {
                    return std::nullopt;
                }
            }
            result.literals_.push_back(*it);
        } else {
            for (auto jt = it; jt != group_end; ++jt) {
                if (jt == it || jt->value != (jt - 1)->value) {
                    result.literals_.push_back(*jt);
                }
            }
        }
        it = group_end;
    }
    return result;
}

bool Refs::has_negatives() const {
    return std::any_of(literals_.begin(), literals_.end(), [](const Literal& l) { return l.negative; });
}

Refs::Binding Refs::binding(const RefName& name, VertexID value) const {
    for (const Literal& literal : literals_) {
        if (literal.name != name) {
            continue;
        }
        if (literal.value == value) {
            return !literal.negative ? Binding::AlreadySet : Binding::Impossible;
        }
        if (!literal.negative) {
            return Binding::Impossible;
        }
    }
    return Binding::Possible;
}

Refs Refs::copy_and_set(const RefName& name, VertexID value) const {
    Refs result;
    result.literals_.reserve(literals_.size() + 1);
    auto it = literals_.begin();
    for (; it != literals_.end() && it->name < name; ++it) {
        result.literals_.push_back(*it);
    }
    result.literals_.push_back(Literal{name, false, value});
    while (it != literals_.end() && it->name == name) {
        ++it;  // replaced by the new value
    }
    result.literals_.insert(result.literals_.end(), it, literals_.end());
    return result;
}

std::optional<Refs> Refs::intersect_with(const Refs& other) const {
    Literals literals;
    literals.reserve(literals_.size() + other.literals_.size());
    literals.insert(literals.end(), literals_.begin(), literals_.end());
    literals.insert(literals.end(), other.literals_.begin(), other.literals_.end());
    return normalized(std::move(literals));
}

bool Refs::implies(const Refs& other) const {
    for (const Literal& required : other.literals_) {
        const RefName& name = required.name;
        const VertexID value = required.value;
        const bool satisfied = std::any_of(literals_.begin(), literals_.end(), [&](const Literal& l) {
            if (l.name != name) {
                return false;
            }
            if (!required.negative) {
                return !l.negative && l.value == value;
            }
            // x ≠ value follows from x ≠ value, or from x being set to another vertex.
            return !l.negative ? l.value != value : l.value == value;
        });
        if (!satisfied) {
            return false;
        }
    }
    return true;
}

// Removes duplicates and refs contained in other refs of the union.
static void simplify_union(std::vector<Refs>& refs) {
    std::sort(refs.begin(), refs.end());
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    if (refs.size() <= 1) {
        return;
    }
    std::vector<Refs> result;
    result.reserve(refs.size());
    for (size_t i = 0; i < refs.size(); ++i) {
        bool contained = false;
        for (size_t j = 0; j < refs.size() && !contained; ++j) {
            contained = i != j && refs[i].implies(refs[j]);
        }
        if (!contained) {
            result.push_back(std::move(refs[i]));
        }
    }
    refs = std::move(result);
}

std::vector<Refs> Refs::subtract(const Refs& other) const {
    if (!intersect_with(other).has_value()) {
        return {*this};
    }
    // De Morgan: this ∧ ¬(l1 ∧ l2 ∧ ...) = (this ∧ ¬l1) ∨ (this ∧ ¬l2) ∨ ...
    std::vector<Refs> result;
    for (const Literal& literal : other.literals_) {
        Refs negation;
        negation.literals_.push_back(negated(literal));
        if (auto term = intersect_with(negation)) {
            result.push_back(std::move(*term));
        }
    }
    simplify_union(result);
    return result;
}

std::vector<std::pair<RefName, std::vector<VertexID>>> Refs::excluded_values() const {
    std::vector<std::pair<RefName, std::vector<VertexID>>> result;
    for (const Literal& literal : literals_) {
        if (!literal.negative) {
            continue;
        }
        if (result.empty() || result.back().first != literal.name) {
            result.emplace_back(literal.name, std::vector<VertexID>{});
        }
        result.back().second.push_back(literal.value);
    }
    return result;
}

// ============================================================================
// Arrangement
// ============================================================================

static std::vector<Refs> subtract_from_union(const std::vector<Refs>& terms, const Refs& refs) {
    std::vector<Refs> result;
    for (const Refs& term : terms) {
        auto rest = term.subtract(refs);
        std::move(rest.begin(), rest.end(), std::back_inserter(result));
    }
    simplify_union(result);
    return result;
}

static bool same_positive_names(const Refs& a, const Refs& b) {
    return std::equal(a.positives_begin(), a.positives_end(), b.positives_begin(), b.positives_end(), [](const auto& x, const auto& y) {
        return x.first == y.first;
    });
}

std::vector<RefsCell> arrange(const std::vector<Refs>& refs) {
    std::vector<RefsCell> cells;

    // Typical case: distinct refs set the same refs (e.g., the operand of (all: ) sets one ref).
    // Then they don't intersect, and each one is a cell by itself.
    const bool disjoint = std::all_of(refs.begin(), refs.end(), [&refs](const Refs& r) {
        return !r.has_negatives() && same_positive_names(r, refs[0]);
    });
    if (disjoint) {
        cells.reserve(refs.size());
        for (size_t i = 0; i < refs.size(); ++i) {
            cells.push_back(RefsCell{{refs[i]}, {i}});
        }
        return cells;
    }

    // Cells are split by every refs in turn: into the part inside it and the part outside it.
    for (size_t i = 0; i < refs.size(); ++i) {
        std::vector<RefsCell> next;
        next.reserve(cells.size() * 2 + 1);
        for (RefsCell& cell : cells) {
            RefsCell inside{{}, cell.members};
            inside.members.push_back(i);
            for (const Refs& term : cell.terms) {
                if (auto intersection = term.intersect_with(refs[i])) {
                    inside.terms.push_back(std::move(*intersection));
                }
            }
            if (inside.terms.empty()) {
                // The cell doesn't intersect the refs and stays as it is.
                next.push_back(std::move(cell));
                continue;
            }
            simplify_union(inside.terms);
            RefsCell outside{subtract_from_union(cell.terms, refs[i]), std::move(cell.members)};
            next.push_back(std::move(inside));
            if (!outside.terms.empty()) {
                next.push_back(std::move(outside));
            }
        }

        // The part of the refs outside all the previous ones.
        std::vector<Refs> fresh{refs[i]};
        for (size_t j = 0; j < i && !fresh.empty(); ++j) {
            fresh = subtract_from_union(fresh, refs[j]);
        }
        if (!fresh.empty()) {
            next.push_back(RefsCell{std::move(fresh), {i}});
        }

        cells = std::move(next);
    }
    return cells;
}

}
