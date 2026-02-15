#pragma once

#include <iostream>
#include "subgraph.h"
#include "observable.h"
#include "matcher.h"
#include "graph.h"

namespace saengra {

struct delimiter {
    std::string text;
    bool used = false;
};

inline std::ostream& operator<<(std::ostream& os, delimiter& d) {
    if (!d.used) {
        d.used = true;
        return os;
    }
    return os << d.text;
}

inline std::ostream& operator<<(std::ostream& os, const PositionKind& kind) {
    os << (kind == PositionKind::CORE ? "CORE" : "ORBIT");
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Position& position) {
    os << "Position(vertex_id=" << position.vertex_id << ", kind=" << position.kind << ")";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Refs& refs) {
    os << "Refs(";
    for (const auto& [k, v] : refs) {
        os << k << ":" << v << ", ";
    }
    os << ")";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Subgraph& sg) {
    os << "Subgraph(start_position=" << sg.start_position << ", end_positions={";
    delimiter d{", "};
    for (const auto ep : sg.end_positions) {
        os << d << ep;
    }
    os << ", refs={";
    d = delimiter{", "};
    for (const auto [k, v] : sg.refs) {
        os << d << k << ": " << v;
    }
    os << "})";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const SubgraphDraft& sg) {
    os << "Subgraph(start_position=" << sg.start_position << ", end_positions={";
    delimiter d{", "};
    for (const auto ep : sg.end_positions) {
        os << d << ep;
    }
    os << ", refs={";
    d = {", "};
    for (const auto [k, v] : sg.refs) {
        os << d << k << ": " << v;
    }
    os << "})";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const std::set<Subgraph>& sgs) {
    os << "{\n";
    delimiter d{",\n  "};
    for (const auto& sg : sgs) {
        os << d << sg;
    }
    return os << "}";
}

inline std::ostream& operator<<(std::ostream& os, const std::vector<SubgraphDraft>& sgs) {
    os << "{\n  ";
    delimiter d{",\n  "};
    for (const auto& sg : sgs) {
        os << d << sg;
    }
    return os << "\n}";
}

inline std::ostream& operator<<(std::ostream& os, const ObservableStartPosition& obj) {
    if (auto p = std::get_if<Position>(&obj)) {
        os << *p;
    }
    if (auto p = std::get_if<AbsentPosition>(&obj)) {
        os << *p;
    }
    if (auto p = std::get_if<EveryAddedVertex>(&obj)) {
        os << "EveryAddedVertex";
    }
    if (auto p = std::get_if<EveryAddedEdge>(&obj)) {
        os << "EveryAddedEdge";
    }
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const QuerySetObservable& obs) {
    os << "QuerySetObservable(observable=" << obs.observable << ", start_position=" << obs.start_position << ")";
    return os;
}

//inline std::ostream& operator<<(std::ostream& os, const UpdateKind& kind) {
//    switch (kind) {
//    case UpdateKind::AddVertex:
//        os << "AddVertex";
//        break;
//    case UpdateKind::AddEdge:
//        os << "AddEdge";
//        break;
//    case UpdateKind::RemoveVertex:
//        os << "RemoveVertex";
//        break;
//    case UpdateKind::RemoveEdge:
//        os << "RemoveEdge";
//        break;
//    case UpdateKind::RemoveEdgesToAll:
//        os << "RemoveEdgesToAll";
//        break;
//    }
//    return os;
//}
//
//inline std::ostream& operator<<(std::ostream& os, const Update& u) {
//    os << "Update(kind=" << u.kind << ")"; // ", from=" << u.vertex_id << ", position=" << u.position << ")";
//    return os;
//}

}
