#include <ordo/core/version.h>

#include <gtest/gtest.h>

TEST(OrdoCoreVersion, ReturnsExpectedPlaceholderString) {
    EXPECT_STREQ(ordo::core::versionString(), "0.1.0");
}
