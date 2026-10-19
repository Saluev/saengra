#pragma once

#include <cstddef>
#include <map>
#include <unordered_map>

namespace saengra {

// Unordered containers keep their bucket arrays on clear(), and clear() costs O(bucket count).
// After a large transaction (e.g. the initial load), that would slow down every later commit.
template<typename Container>
void clear_and_shrink(Container& container, size_t max_bucket_count = 1024) {
    if (container.bucket_count() > max_bucket_count) {
        Container().swap(container);
    } else {
        container.clear();
    }
}

template<typename K, typename V>
V find_or_default(const std::map<K, V>& container, const K& key) {
    auto it = container.find(key);
    if (it != container.end()) {
        return it->second;
    }
    return V{};
}

template<typename K, typename V>
V find_or_default(const std::unordered_map<K, V>& container, const K& key) {
    auto it = container.find(key);
    if (it != container.end()) {
        return it->second;
    }
    return V{};
}

}
