#pragma once

#include <algorithm>
#include <compare>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "vertex.h"

namespace saengra {

using RefName = std::string;

struct Literal {
    RefName name;
    bool negative;
    VertexID value;

    auto operator<=>(const Literal& other) const = default;
};

class Refs {
    // Patterns have only a few refs, so a plain list beats a map. It's kept sorted by name, then
    // positive before negative, then value, so that iteration order and comparisons are stable.
    using Literals = std::vector<Literal>;

public:
    class const_positives_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const RefName&, VertexID>;
        using difference_type = std::ptrdiff_t;
        using reference = value_type;

        const_positives_iterator(Literals::const_iterator it, Literals::const_iterator end) : it_(it), end_(end) { skip(); }

        reference operator*() const { return {it_->name, it_->value}; }
        const_positives_iterator& operator++() { ++it_; skip(); return *this; }
        bool operator==(const const_positives_iterator& other) const { return it_ == other.it_; }

    private:
        void skip() {
            while (it_ != end_ && it_->negative) ++it_;
        }

        Literals::const_iterator it_;
        Literals::const_iterator end_;
    };

    const_positives_iterator positives_begin() const { return {literals_.begin(), literals_.end()}; }
    const_positives_iterator positives_end() const { return {literals_.end(), literals_.end()}; }

    struct Positives {
        const_positives_iterator begin_;
        const_positives_iterator end_;

        const_positives_iterator begin() const { return begin_; }
        const_positives_iterator end() const { return end_; }
    };
    Positives positives() const { return {positives_begin(), positives_end()}; }

    const Literals& literals() const { return literals_; }
    bool has_negatives() const;

    enum class Binding {
        Possible,     // the ref can be set to the vertex
        AlreadySet,   // the ref is already set to the vertex
        Impossible,   // the ref is set to another vertex, or the vertex is excluded
    };
    Binding binding(const RefName& name, VertexID value) const;

    Refs copy_and_set(const RefName& name, VertexID value) const;

    // Region algebra.
    std::optional<Refs> intersect_with(const Refs& other) const;
    std::vector<Refs> subtract(const Refs& other) const;
    bool implies(const Refs& other) const;
    Refs restricted_to(const std::vector<RefName>& names) const;
    std::vector<std::pair<RefName, std::vector<VertexID>>> excluded_values() const;

    bool operator==(const Refs& other) const { return literals_ == other.literals_; }
    bool operator<(const Refs& other) const { return literals_ < other.literals_; }

private:
    static std::optional<Refs> normalized(Literals literals);

    Literals literals_;
};

std::vector<Refs> subtract_from_union(const std::vector<Refs>& terms, const Refs& refs);

// A cell of the arrangement of refs: the points of the refspace covered by exactly the refs
// in `members` (indices into the arranged refs). The cell is a union of `terms`.
struct RefsCell {
    std::vector<Refs> terms;
    std::vector<size_t> members;
};

// Cells of the arrangement of the given (distinct) refs, except the one covered by none of them.
std::vector<RefsCell> arrange(const std::vector<Refs>& refs);

}

namespace std {

template <>
struct hash<saengra::Refs> {
    std::size_t operator()(const saengra::Refs& refs) const {
        std::size_t result = refs.literals().size();
        for (const saengra::Literal& literal : refs.literals()) {
            result = result * 31 + std::hash<std::string>{}(literal.name);
            result = result * 31 + std::hash<saengra::VertexID>{}(literal.value);
            result = result * 31 + literal.negative;
        }
        return result;
    }
};

}
