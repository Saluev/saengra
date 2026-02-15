#include <string>
#include <tuple>
#include "absl/hash/hash.h"
#include "vertex.h"

namespace saengra {

VertexTypeName::VertexTypeName(const std::string& text):
    text(&text) {
}


VertexData::VertexData(const VertexTypeName& type_name, const VertexValue& value):
    type_name(type_name),
    value(value),
    precomputed_hash(absl::Hash<std::tuple<size_t, std::string>>()(make_tuple((size_t)(type_name.text), value))) {
}

std::ostream& operator<<(std::ostream& os, const VertexTypeName& obj) {
    return os << *obj.text;
}

std::ostream& operator<<(std::ostream& os, const VertexData& v) {
    return os << *v.type_name.text << "(" << v.value << ")";  // TODO encode value if binary
}

}
