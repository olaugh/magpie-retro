/*
 * KLV16 Reader - Leave value lookup for Scrabble racks
 *
 * Reads .klv16 format files and provides leave value lookup.
 * Based on the ROM's klv.c implementation.
 */

#ifndef GXTEST_KLV_H
#define GXTEST_KLV_H

#include <cstdint>
#include <string>
#include <vector>

namespace gxtest {

// Machine letter encoding: 0=blank, 1-26=A-Z
constexpr int ALPHABET_SIZE = 27;
constexpr int RACK_SIZE = 7;
constexpr uint32_t KLV_UNFOUND_INDEX = 0xFFFFFFFF;

// Rack representation: counts per letter
struct Rack {
    uint8_t counts[ALPHABET_SIZE] = {0};
    uint8_t total = 0;

    void Clear();
    void AddTile(uint8_t ml);
    bool RemoveTile(uint8_t ml);

    // Parse from string like "RETINAS" or "?AEINST" (? = blank)
    static Rack FromString(const std::string& s);

    // Convert to string
    std::string ToString() const;
};

// KLV16 file reader
class KLV {
public:
    KLV() = default;

    // Load from .klv16 file
    bool Load(const std::string& path);

    // Check if loaded successfully
    bool IsLoaded() const { return !kwg_.empty(); }

    // Get leave value for a rack (in eighths of a point)
    int16_t GetLeaveValue(const Rack& rack) const;

    // Get leave value for a rack string like "RETINAS" or "?AEINST"
    int16_t GetLeaveValue(const std::string& rack_str) const;

    // Compute average 6-tile leave value for a 7-tile rack
    // Returns average in eighths of a point
    double GetAverage6TileLeave(const std::string& rack_str) const;

    // Get word index for a rack (for testing/debugging)
    uint32_t GetWordIndex(const Rack& rack) const;

    // Accessors for testing
    uint32_t kwg_size() const { return kwg_.size(); }
    uint32_t num_leaves() const { return leaves_.size(); }
    uint32_t word_count(uint32_t idx) const {
        return idx < word_counts_.size() ? word_counts_[idx] : 0;
    }

private:
    std::vector<uint32_t> kwg_;
    std::vector<int16_t> leaves_;
    std::vector<uint32_t> word_counts_;

    // KWG node accessors
    static uint8_t NodeTile(uint32_t node) { return node >> 24; }
    static bool NodeAccepts(uint32_t node) { return (node & 0x00800000) != 0; }
    static bool NodeIsEnd(uint32_t node) { return (node & 0x00400000) != 0; }
    static uint32_t NodeArcIndex(uint32_t node) { return node & 0x003FFFFF; }

    // Get DAWG root index
    uint32_t GetDawgRoot() const;

    // Traverse to find a letter in sibling list
    uint32_t IncrementToLetter(uint32_t node_index, uint32_t word_index,
                                uint32_t* next_word_index, uint8_t ml) const;

    // Follow arc to children
    uint32_t FollowArc(uint32_t node_index, uint32_t word_index,
                       uint32_t* next_word_index) const;

    // Compute word counts for the DAWG
    void ComputeWordCounts();
};

}  // namespace gxtest

#endif  // GXTEST_KLV_H
