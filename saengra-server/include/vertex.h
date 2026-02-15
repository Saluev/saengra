#pragma once

#include <string>
#include "absl/hash/hash.h"

namespace saengra {

struct VertexTypeName {
    const std::string* text;

    explicit VertexTypeName(const std::string& text);

    inline bool operator<(const VertexTypeName& other) const {
        return (size_t)text < (size_t)other.text;
    }

    inline bool operator==(const VertexTypeName& other) const {
        return text == other.text;
    }

    inline bool operator==(const char* other) const { // for tests
        return *text == other;
    }
};

using VertexValue = std::string;

struct VertexData {
    VertexTypeName type_name;
    VertexValue value;
    size_t precomputed_hash;

    VertexData(const VertexData&) = default;
    VertexData(VertexData&&) = default;
    explicit VertexData(const VertexTypeName& type_name, const VertexValue& value);

    inline bool operator<(const VertexData& other) const {
        return type_name < other.type_name || (type_name == other.type_name && value < other.value);
    }

    VertexData& operator=(const VertexData&) = default;
    inline bool operator==(const VertexData& other) const {
        return type_name == other.type_name && value == other.value;
    }
};

struct StoredVertex {
    const VertexTypeName type_name;
    const VertexValue value;
    bool present;
};

using VertexID = StoredVertex*;

std::ostream& operator<<(std::ostream& os, const VertexTypeName& obj);
std::ostream& operator<<(std::ostream& os, const VertexData& obj);

}

namespace std {

template <>
struct hash<saengra::VertexTypeName> {
    std::size_t operator()(const saengra::VertexTypeName& v) const {
        return (size_t)(v.text);
    }
};

template <>
struct hash<saengra::VertexData> {
    std::size_t operator()(const saengra::VertexData& v) const {
        return v.precomputed_hash;
    }
};

template<>
class optional<saengra::VertexID> {
public:
    optional() = default;
    optional(nullopt_t) : id(nullptr) {}
    optional(saengra::VertexID id) : id(id) {}
    bool has_value() const { return id != nullptr; }
    saengra::VertexID value() const { return id; }
    saengra::VertexID value_or(saengra::VertexID default_) const { return id ? id : default_; }
    operator bool() const { return id != nullptr; }
    saengra::VertexID operator *() const { return id; }
private:
    saengra::VertexID id = nullptr;
};

}
