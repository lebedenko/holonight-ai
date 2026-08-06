#include "holonight_platform/panel_surface.h"

#include <gtest/gtest.h>

namespace {

TEST(PanelSurfacePolicy, PreferredWidthClampsSmallOutputsToMinimum) {
  EXPECT_EQ(holonight_platform::preferredPanelWidth(1200), 440);
}

TEST(PanelSurfacePolicy, PreferredWidthScalesWithinBounds) {
  EXPECT_EQ(holonight_platform::preferredPanelWidth(1600), 480);
}

TEST(PanelSurfacePolicy, PreferredWidthClampsLargeOutputsToMaximum) {
  EXPECT_EQ(holonight_platform::preferredPanelWidth(2560), 560);
}

}  // namespace
