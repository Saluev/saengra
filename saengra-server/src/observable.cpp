#include "observable.h"

namespace saengra {

std::ostream& operator<<(std::ostream& os, const NewVertex& obj) {
    return os << "NewVertex()";
}

std::ostream& operator<<(std::ostream& os, const NewVertexOfType& obj) {
    return os << "NewVertexOfType(type_name='" << *obj.type_name.text << "')";
}

std::ostream& operator<<(std::ostream& os, const ParticularVertex& obj) {
    return os << "ParticularVertex(vertex=" << obj.vertex << ")";
}

std::ostream& operator<<(std::ostream& os, const NewEdge& obj) {
    return os << "NewEdge()";
}

std::ostream& operator<<(std::ostream& os, const NewEdgeWithLabel& obj) {
    return os << "NewEdgeWithLabel(label='" << obj.label << "')";
}

std::ostream& operator<<(std::ostream& os, const EdgeFrom& obj) {
    return os << "EdgeFrom(from=" << obj.from_id << ")";
}

std::ostream& operator<<(std::ostream& os, const EdgeFromWithLabel& obj) {
    return os << "EdgeFromWithLabel(from=" << obj.from_id << ", label='" << obj.label << "')";
}

std::ostream& operator<<(std::ostream& os, const EdgeTo& obj) {
    return os << "EdgeTo(to=" << obj.to_id << ")";
}

std::ostream& operator<<(std::ostream& os, const EdgeToWithLabel& obj) {
    return os << "EdgeToWithLabel(to=" << obj.to_id << ", label='" << obj.label << "')";
}

namespace {
    struct PrintVisitor : boost::static_visitor<> {
        std::ostream& os;
        PrintVisitor(std::ostream& os) : os(os) {}

        template<typename T>
        void operator()(const T& observable) const {
            os << observable;
        }
    };
}

std::ostream& operator<<(std::ostream& os, const Observable& obj) {
    boost::apply_visitor(PrintVisitor(os), obj);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Observation& obj) {
    os << "Observation(" << obj.observable << ", vertices=[";
    for (size_t i = 0; i < obj.added_vertex_ids.size(); ++i) {
        if (i > 0) os << ", ";
        os << obj.added_vertex_ids[i];
    }
    os << "], edges=[";
    for (size_t i = 0; i < obj.added_edges.size(); ++i) {
        if (i > 0) os << ", ";
        os << obj.added_edges[i];
    }
    os << "])";
    return os;
}

}
