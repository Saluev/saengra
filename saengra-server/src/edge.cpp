#include "edge.h"
#include "absl/hash/hash.h"
#include <string>

namespace saengra {

EdgeLabel::EdgeLabel(const std::string& text):
    text(&text) {
}

std::ostream& operator<<(std::ostream& os, const EdgeLabel& obj) {
    return os << *obj.text;
}

std::ostream& operator<<(std::ostream& os, const Edge& obj) {
    return os << "Edge(" << obj.from << " -" << obj.label << "> " << obj.to << ")";
}

std::vector<Edge> reverse_all(const std::vector<Edge>& edges) {
    std::vector<Edge> reversed;
    reversed.reserve(edges.size());

    for (const Edge& edge : edges) {
        reversed.push_back(edge.reverse());
    }
    return reversed;
}

std::vector<Edge> sort_and_intersect(std::vector<Edge>& a, std::vector<Edge>& b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    std::vector<Edge> intersection;
    std::set_intersection(
        a.begin(), a.end(),
        b.begin(), b.end(),
        std::back_inserter(intersection)
    );
    return intersection;
}

}
