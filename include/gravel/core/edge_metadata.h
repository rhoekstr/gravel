/// @file edge_metadata.h
/// @brief Generic per-edge metadata store (key-value string tags).
///
/// Holds arbitrary string key-value pairs for each directed edge, indexed by
/// CSR edge position. OSM edges carry tags like "highway", "name", "surface".
/// Non-OSM graphs can use this for any custom edge categorization.

#pragma once

#include <string>
#include <vector>

namespace gravel {

struct EdgeMetadata {
    /// Available tag keys (e.g., {"highway", "name", "surface"}).
    std::vector<std::string> tag_keys;

    /// Per-edge tag values: tag_values[key_index][edge_index].
    /// Each inner vector has graph->edge_count() entries.
    std::vector<std::vector<std::string>> tag_values;

    /// Get all values for a specific tag key.
    /// Returns empty vector if the key doesn't exist.
    const std::vector<std::string>& get(const std::string& key) const {
        for (size_t i = 0; i < tag_keys.size(); ++i) {
            if (tag_keys[i] == key) return tag_values[i];
        }
        static const std::vector<std::string> empty;
        return empty;
    }

    /// Check if a tag key exists.
    bool has(const std::string& key) const {
        for (const auto& k : tag_keys) if (k == key) return true;
        return false;
    }
};

}  // namespace gravel
