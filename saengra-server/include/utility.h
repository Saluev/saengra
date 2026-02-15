#pragma once

namespace saengra {

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
