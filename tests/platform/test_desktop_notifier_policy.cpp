#include "holonight_platform/desktop_notifier.h"

#include <gtest/gtest.h>

namespace {

TEST(DesktopNotifierPolicy, ResolveSummaryUsesFallbackForEmptyTitle) {
  EXPECT_EQ(holonight_platform::resolveNotificationSummary(QString{}), QStringLiteral("New response"));
}

TEST(DesktopNotifierPolicy, ResolveSummaryReturnsNonEmptyTitleUnchanged) {
  EXPECT_EQ(holonight_platform::resolveNotificationSummary(QStringLiteral("Deploy plan")),
            QStringLiteral("Deploy plan"));
}

}  // namespace
