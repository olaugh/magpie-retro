/*
 * KLV16 Reader implementation
 *
 * Based on the ROM's klv.c implementation.
 */

#include "klv.h"

#include <fstream>
#include <cstring>

namespace gxtest {

// Rack implementation

void Rack::Clear() {
    memset(counts, 0, sizeof(counts));
    total = 0;
}

void Rack::AddTile(uint8_t ml) {
    if (ml < ALPHABET_SIZE) {
        counts[ml]++;
        total++;
    }
}

bool Rack::RemoveTile(uint8_t ml) {
    if (ml < ALPHABET_SIZE && counts[ml] > 0) {
        counts[ml]--;
        total--;
        return true;
    }
    return false;
}

Rack Rack::FromString(const std::string& s) {
    Rack rack;
    for (char c : s) {
        if (c == '?') {
            rack.AddTile(0);  // Blank
        } else if (c >= 'A' && c <= 'Z') {
            rack.AddTile(c - 'A' + 1);
        } else if (c >= 'a' && c <= 'z') {
            rack.AddTile(c - 'a' + 1);
        }
    }
    return rack;
}

std::string Rack::ToString() const {
    std::string result;
    for (int ml = 0; ml < ALPHABET_SIZE; ml++) {
        for (int j = 0; j < counts[ml]; j++) {
            if (ml == 0) {
                result += '?';
            } else {
                result += ('A' + ml - 1);
            }
        }
    }
    return result;
}

// KLV implementation

bool KLV::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    // Helper to read little-endian uint32
    auto read_u32 = [&f]() -> uint32_t {
        uint8_t buf[4];
        f.read(reinterpret_cast<char*>(buf), 4);
        return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (uint32_t(buf[3]) << 24);
    };

    // Helper to read little-endian int16
    auto read_i16 = [&f]() -> int16_t {
        uint8_t buf[2];
        f.read(reinterpret_cast<char*>(buf), 2);
        return int16_t(buf[0] | (buf[1] << 8));
    };

    // Read KWG size
    uint32_t kwg_size = read_u32();
    if (!f || kwg_size == 0 || kwg_size > 10000000) return false;

    // Read KWG nodes
    kwg_.resize(kwg_size);
    for (uint32_t i = 0; i < kwg_size; i++) {
        kwg_[i] = read_u32();
    }
    if (!f) return false;

    // Read number of leaves
    uint32_t num_leaves = read_u32();
    if (!f || num_leaves == 0 || num_leaves > 10000000) return false;

    // Read leave values
    leaves_.resize(num_leaves);
    for (uint32_t i = 0; i < num_leaves; i++) {
        leaves_[i] = read_i16();
    }
    if (!f) return false;

    // Compute word counts
    ComputeWordCounts();

    return true;
}

void KLV::ComputeWordCounts() {
    uint32_t kwg_size = kwg_.size();
    word_counts_.resize(kwg_size, 0);

    // Iterate until no changes (matching ROM's iterative approach)
    bool changed;
    do {
        changed = false;

        // Process nodes in reverse order
        for (uint32_t i = kwg_size; i > 0; ) {
            i--;
            uint32_t node = kwg_[i];
            uint32_t count = 0;

            // This node accepts?
            if (NodeAccepts(node)) {
                count = 1;
            }

            // Add children count
            uint32_t child_index = NodeArcIndex(node);
            if (child_index != 0 && child_index < kwg_size) {
                count += word_counts_[child_index];
            }

            // Add siblings count (if not last sibling)
            if (!NodeIsEnd(node) && i + 1 < kwg_size) {
                count += word_counts_[i + 1];
            }

            if (word_counts_[i] != count) {
                word_counts_[i] = count;
                changed = true;
            }
        }
    } while (changed);
}

uint32_t KLV::GetDawgRoot() const {
    if (kwg_.empty()) return 0;
    return NodeArcIndex(kwg_[0]);
}

uint32_t KLV::IncrementToLetter(uint32_t node_index, uint32_t word_index,
                                 uint32_t* next_word_index, uint8_t ml) const {
    if (node_index == 0 || node_index >= kwg_.size()) {
        *next_word_index = KLV_UNFOUND_INDEX;
        return 0;
    }

    uint32_t idx = word_index;

    for (;;) {
        uint32_t node = kwg_[node_index];

        if (NodeTile(node) == ml) {
            *next_word_index = idx;
            return node_index;
        }

        if (NodeIsEnd(node)) {
            *next_word_index = KLV_UNFOUND_INDEX;
            return 0;
        }

        // Skip this sibling's subtree count
        // The count for this sibling = word_counts_[node_index] - word_counts_[node_index + 1]
        idx += word_counts_[node_index] - word_counts_[node_index + 1];
        node_index++;

        if (node_index >= kwg_.size()) {
            *next_word_index = KLV_UNFOUND_INDEX;
            return 0;
        }
    }
}

uint32_t KLV::FollowArc(uint32_t node_index, uint32_t word_index,
                        uint32_t* next_word_index) const {
    if (node_index == 0 || node_index >= kwg_.size()) {
        *next_word_index = KLV_UNFOUND_INDEX;
        return 0;
    }

    // After accepting a letter, word_index increments by 1
    *next_word_index = word_index + 1;
    uint32_t node = kwg_[node_index];
    return NodeArcIndex(node);
}

uint32_t KLV::GetWordIndex(const Rack& rack) const {
    if (rack.total == 0) {
        return KLV_UNFOUND_INDEX;
    }

    uint32_t node_index = GetDawgRoot();
    uint32_t idx = 0;

    // Find first letter with count > 0
    uint8_t ml = 0;
    int ml_count = 0;
    for (ml = 0; ml < ALPHABET_SIZE; ml++) {
        if (rack.counts[ml] > 0) {
            ml_count = rack.counts[ml];
            break;
        }
    }

    uint8_t remaining = rack.total;

    while (node_index != 0) {
        uint32_t next_word_index;
        node_index = IncrementToLetter(node_index, idx, &next_word_index, ml);

        if (node_index == 0) {
            return KLV_UNFOUND_INDEX;
        }
        idx = next_word_index;

        ml_count--;
        remaining--;

        // Advance to next letter if done with this one
        while (ml_count == 0) {
            ml++;
            if (ml >= ALPHABET_SIZE) {
                break;
            }
            ml_count = rack.counts[ml];
        }

        if (remaining == 0) {
            return idx;
        }

        node_index = FollowArc(node_index, idx, &next_word_index);
        idx = next_word_index;
    }

    return KLV_UNFOUND_INDEX;
}

int16_t KLV::GetLeaveValue(const Rack& rack) const {
    if (rack.total == 0) {
        return 0;
    }

    uint32_t index = GetWordIndex(rack);
    if (index == KLV_UNFOUND_INDEX || index >= leaves_.size()) {
        return 0;
    }
    return leaves_[index];
}

int16_t KLV::GetLeaveValue(const std::string& rack_str) const {
    return GetLeaveValue(Rack::FromString(rack_str));
}

double KLV::GetAverage6TileLeave(const std::string& rack_str) const {
    Rack rack = Rack::FromString(rack_str);
    if (rack.total != 7) {
        return 0.0;
    }

    double sum = 0.0;
    int count = 0;

    // Try removing each tile type
    for (int ml = 0; ml < ALPHABET_SIZE; ml++) {
        for (int j = 0; j < rack.counts[ml]; j++) {
            // Create 6-tile leave by removing one of this tile
            Rack leave = rack;
            leave.counts[ml]--;
            leave.total--;

            sum += GetLeaveValue(leave);
            count++;
        }
    }

    return count > 0 ? sum / count : 0.0;
}

}  // namespace gxtest
