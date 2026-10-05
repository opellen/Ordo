#include <ordo/core/version.h>

#include <string>

#include <gtest/gtest.h>

TEST(OrdoCoreVersion, MatchesVersionHeader) {
    EXPECT_EQ(std::string(ordo::core::versionString()), std::string(ordo::core::kVersionString));
}
