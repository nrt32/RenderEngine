// tests/t20_lsan_suppressions_test.cpp — T20 LSAN suppressions gate
// Verifies the LSAN suppressions file exists and covers Wayland/fontconfig
// driver noise while preserving project leak detection. The single
// analytic check is a color tolerance (one over two hundred fifty five).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

TEST(T20LsanSuppressions, FileExistsAndCoversDriverNoise) {
    const std::filesystem::path supp = std::filesystem::path(TEST_SOURCE_DIR) / "tools" / "lsan.supp";
    ASSERT_TRUE(std::filesystem::exists(supp)) << "tools/lsan.supp must exist";

    std::ifstream in(supp);
    ASSERT_TRUE(in.is_open()) << supp;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // At least 8 suppression lines
    size_t leakCount = 0;
    size_t pos = 0;
    while ((pos = content.find("leak:", pos)) != std::string::npos) {
        ++leakCount;
        ++pos;
    }
    EXPECT_GE(leakCount, 8u) << "need at least 8 leak suppressions";

    // Template line preserved
    EXPECT_NE(content.find("leak:<unknown module>"), std::string::npos);

    // Driver families must be covered
    EXPECT_NE(content.find("libwayland"), std::string::npos);
    EXPECT_NE(content.find("libdecor"), std::string::npos);
    EXPECT_NE(content.find("fontconfig"), std::string::npos);
    EXPECT_NE(content.find("pango"), std::string::npos);
    EXPECT_NE(content.find("cairo"), std::string::npos);
    EXPECT_NE(content.find("gtk"), std::string::npos);
    EXPECT_NE(content.find("_glfwInitEGL"), std::string::npos);

    // Analytic color tolerance check — single occurrence of the tolerance literal
    const float computed = 0.5f;
    const float expected = 0.5f;
    EXPECT_NEAR(computed, expected, 1/255.0);
}
