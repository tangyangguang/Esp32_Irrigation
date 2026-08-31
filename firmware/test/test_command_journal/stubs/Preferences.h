#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

class Preferences {
public:
    bool begin(const char* name, bool = false, const char* = nullptr) {
        if (!name || name[0] == '\0') return false;
        namespace_ = name;
        return true;
    }

    void end() { namespace_.clear(); }

    std::size_t getBytesLength(const char* key) const {
        const auto found = storage().find(fullKey(key));
        return found == storage().end() ? 0U : found->second.size();
    }

    std::size_t getBytes(const char* key, void* value, std::size_t length) const {
        const auto found = storage().find(fullKey(key));
        if (found == storage().end() || !value || length < found->second.size()) {
            return 0U;
        }
        std::memcpy(value, found->second.data(), found->second.size());
        return found->second.size();
    }

    std::size_t putBytes(const char* key, const void* value, std::size_t length) {
        if (namespace_.empty() || !key || !value) return 0U;
        const auto* bytes = static_cast<const uint8_t*>(value);
        storage()[fullKey(key)] = std::vector<uint8_t>(bytes, bytes + length);
        return length;
    }

    static void testReset() { storage().clear(); }

    static bool testCorrupt(const char* name, const char* key) {
        const std::string full = std::string(name) + "/" + key;
        auto found = storage().find(full);
        if (found == storage().end() || found->second.empty()) return false;
        found->second[found->second.size() / 2U] ^= 0x5AU;
        return true;
    }

private:
    static std::unordered_map<std::string, std::vector<uint8_t>>& storage() {
        static std::unordered_map<std::string, std::vector<uint8_t>> values;
        return values;
    }

    std::string fullKey(const char* key) const {
        return namespace_ + "/" + (key ? key : "");
    }

    std::string namespace_;
};
