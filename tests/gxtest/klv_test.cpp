/*
 * Unit tests for KLV16 reader
 */

#include <gtest/gtest.h>
#include "klv.h"

namespace gxtest {
namespace {

// Path to KLV16 files (set via BUILD.bazel defines)
#ifndef KLV_NWL23
#define KLV_NWL23 "data/NWL23.klv16"
#endif
#ifndef KLV_CSW24
#define KLV_CSW24 "data/CSW24.klv16"
#endif

class KLVTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(nwl_.Load(KLV_NWL23)) << "Failed to load " << KLV_NWL23;
    }

    KLV nwl_;
};

// Test Rack parsing
TEST(RackTest, FromString) {
    Rack r = Rack::FromString("RETINAS");
    EXPECT_EQ(r.total, 7);
    EXPECT_EQ(r.counts['R' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['E' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['T' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['I' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['N' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['A' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['S' - 'A' + 1], 1);
}

TEST(RackTest, FromStringWithBlank) {
    Rack r = Rack::FromString("?AEINST");
    EXPECT_EQ(r.total, 7);
    EXPECT_EQ(r.counts[0], 1);  // Blank
    EXPECT_EQ(r.counts['A' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['E' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['I' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['N' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['S' - 'A' + 1], 1);
    EXPECT_EQ(r.counts['T' - 'A' + 1], 1);
}

TEST(RackTest, FromStringWithDuplicates) {
    Rack r = Rack::FromString("EETTTSS");
    EXPECT_EQ(r.total, 7);
    EXPECT_EQ(r.counts['E' - 'A' + 1], 2);
    EXPECT_EQ(r.counts['T' - 'A' + 1], 3);
    EXPECT_EQ(r.counts['S' - 'A' + 1], 2);
}

TEST(RackTest, ToString) {
    Rack r = Rack::FromString("SATIRE");
    // ToString returns letters in sorted order
    EXPECT_EQ(r.ToString(), "AEIRST");
}

TEST(RackTest, ToStringWithBlank) {
    Rack r = Rack::FromString("?STING");
    // Blank comes first (ml=0)
    EXPECT_EQ(r.ToString(), "?GINST");
}

// Test KLV loading
TEST_F(KLVTest, LoadSucceeds) {
    EXPECT_TRUE(nwl_.IsLoaded());
    EXPECT_GT(nwl_.kwg_size(), 0u);
    EXPECT_GT(nwl_.num_leaves(), 0u);
}

// Test single-letter leaves
TEST_F(KLVTest, SingleLetterLeaves) {
    // Single vowels should have negative leave values
    // Single consonants vary
    int16_t a_leave = nwl_.GetLeaveValue("A");
    int16_t e_leave = nwl_.GetLeaveValue("E");
    int16_t s_leave = nwl_.GetLeaveValue("S");
    int16_t q_leave = nwl_.GetLeaveValue("Q");

    // S is the best single-letter leave
    EXPECT_GT(s_leave, a_leave);
    EXPECT_GT(s_leave, e_leave);

    // Q is terrible
    EXPECT_LT(q_leave, a_leave);

    // Print for debugging
    std::cout << "Single letter leaves (eighths):" << std::endl;
    std::cout << "  A: " << a_leave << " (" << a_leave / 8.0 << " pts)" << std::endl;
    std::cout << "  E: " << e_leave << " (" << e_leave / 8.0 << " pts)" << std::endl;
    std::cout << "  S: " << s_leave << " (" << s_leave / 8.0 << " pts)" << std::endl;
    std::cout << "  Q: " << q_leave << " (" << q_leave / 8.0 << " pts)" << std::endl;
}

// Test blank leave value
TEST_F(KLVTest, BlankLeaveValue) {
    int16_t blank_leave = nwl_.GetLeaveValue("?");
    // Blank should have very high leave value
    EXPECT_GT(blank_leave, 0);

    std::cout << "Blank leave: " << blank_leave << " (" << blank_leave / 8.0 << " pts)" << std::endl;
}

// Test multi-letter leaves
TEST_F(KLVTest, MultiLetterLeaves) {
    // SATIRE is a good 6-letter leave (one away from bingo)
    int16_t satire = nwl_.GetLeaveValue("SATIRE");

    // QU is bad
    int16_t qu = nwl_.GetLeaveValue("QU");

    EXPECT_GT(satire, qu);

    std::cout << "Multi-letter leaves (eighths):" << std::endl;
    std::cout << "  SATIRE: " << satire << " (" << satire / 8.0 << " pts)" << std::endl;
    std::cout << "  QU: " << qu << " (" << qu / 8.0 << " pts)" << std::endl;
}

// Test that order doesn't matter (rack is sorted internally)
TEST_F(KLVTest, OrderIndependent) {
    EXPECT_EQ(nwl_.GetLeaveValue("SATIRE"), nwl_.GetLeaveValue("EITRSA"));
    EXPECT_EQ(nwl_.GetLeaveValue("SATIRE"), nwl_.GetLeaveValue("TISERA"));
    EXPECT_EQ(nwl_.GetLeaveValue("?AB"), nwl_.GetLeaveValue("BA?"));
}

// Test empty rack
TEST_F(KLVTest, EmptyRack) {
    EXPECT_EQ(nwl_.GetLeaveValue(""), 0);
}

// Test average 6-tile leave
TEST_F(KLVTest, Average6TileLeave) {
    // RETINAS is a good rack
    double retinas = nwl_.GetAverage6TileLeave("RETINAS");

    // QUVWXYZ is terrible
    double quvwxyz = nwl_.GetAverage6TileLeave("QUVWXYZ");

    EXPECT_GT(retinas, quvwxyz);

    std::cout << "Average 6-tile leaves (eighths):" << std::endl;
    std::cout << "  RETINAS: " << retinas << " (" << retinas / 8.0 << " pts)" << std::endl;
    std::cout << "  QUVWXYZ: " << quvwxyz << " (" << quvwxyz / 8.0 << " pts)" << std::endl;
}

// Test non-7-tile rack returns 0 for average
TEST_F(KLVTest, Average6TileLeaveNon7) {
    EXPECT_EQ(nwl_.GetAverage6TileLeave("SATIRE"), 0.0);  // Only 6 tiles
    EXPECT_EQ(nwl_.GetAverage6TileLeave("RETINASS"), 0.0);  // 8 tiles
}

// Test word index for debugging
TEST_F(KLVTest, WordIndexBasic) {
    // Single letters should have small indices
    Rack a = Rack::FromString("A");
    Rack z = Rack::FromString("Z");

    uint32_t a_idx = nwl_.GetWordIndex(a);
    uint32_t z_idx = nwl_.GetWordIndex(z);

    EXPECT_NE(a_idx, KLV_UNFOUND_INDEX);
    EXPECT_NE(z_idx, KLV_UNFOUND_INDEX);
    EXPECT_LT(a_idx, z_idx);  // A comes before Z

    std::cout << "Word indices:" << std::endl;
    std::cout << "  A: " << a_idx << std::endl;
    std::cout << "  Z: " << z_idx << std::endl;
}

// Test that blank comes before A
TEST_F(KLVTest, BlankComesFirst) {
    Rack blank = Rack::FromString("?");
    Rack a = Rack::FromString("A");

    uint32_t blank_idx = nwl_.GetWordIndex(blank);
    uint32_t a_idx = nwl_.GetWordIndex(a);

    EXPECT_NE(blank_idx, KLV_UNFOUND_INDEX);
    EXPECT_NE(a_idx, KLV_UNFOUND_INDEX);
    EXPECT_LT(blank_idx, a_idx);  // Blank (ml=0) comes before A (ml=1)

    std::cout << "  Blank: " << blank_idx << std::endl;
}

// Print some sample leave values for manual verification
TEST_F(KLVTest, SampleLeaveValues) {
    std::vector<std::string> samples = {
        "?", "A", "E", "I", "O", "U", "S", "Q", "Z",
        "??", "?S", "SS", "QU",
        "AE", "AI", "ST", "ER", "IN",
        "?AEINST", "SATIRE", "RETINA", "RETINAS",
    };

    std::cout << "\nSample leave values:" << std::endl;
    std::cout << "  Rack          Index     Leave (eighths)  Leave (pts)" << std::endl;
    std::cout << "  -----------   --------  ---------------  -----------" << std::endl;

    for (const auto& s : samples) {
        Rack r = Rack::FromString(s);
        uint32_t idx = nwl_.GetWordIndex(r);
        int16_t leave = nwl_.GetLeaveValue(s);

        char idx_str[16];
        if (idx == KLV_UNFOUND_INDEX) {
            snprintf(idx_str, sizeof(idx_str), "UNFOUND");
        } else {
            snprintf(idx_str, sizeof(idx_str), "%u", idx);
        }

        printf("  %-12s  %8s  %15d  %11.2f\n",
               s.c_str(), idx_str, leave, leave / 8.0);
    }
}

}  // namespace
}  // namespace gxtest
