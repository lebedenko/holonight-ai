#include "holonight_application/token_formatting.h"

#include <QString>
#include <QVariant>

#include <gtest/gtest.h>

namespace holonight_application {
namespace {

TEST(TokenFormatting, FormatTokenCountReturnsPlainIntegerBelowOneThousand) {
  EXPECT_EQ(formatTokenCount(QVariant(0)), QStringLiteral("0"));
  EXPECT_EQ(formatTokenCount(QVariant(999)), QStringLiteral("999"));
}

TEST(TokenFormatting, FormatTokenCountReturnsKAbbreviationAtOrAboveOneThousand) {
  EXPECT_EQ(formatTokenCount(QVariant(1000)), QStringLiteral("1.0K"));
  EXPECT_EQ(formatTokenCount(QVariant(1500)), QStringLiteral("1.5K"));
  EXPECT_EQ(formatTokenCount(QVariant(12345)), QStringLiteral("12.3K"));
}

TEST(TokenFormatting, FormatTokenCountDoesNotInsertThousandsSeparatorBelowKThreshold) {
  EXPECT_EQ(formatTokenCount(QVariant(500)), QStringLiteral("500"));
  EXPECT_FALSE(formatTokenCount(QVariant(500)).contains(QLatin1Char(',')));
}

TEST(TokenFormatting, FormatTokenCountReturnsEmptyStringForInvalidOrNullVariant) {
  EXPECT_EQ(formatTokenCount(QVariant()), QString());
}

}  // namespace
}  // namespace holonight_application
