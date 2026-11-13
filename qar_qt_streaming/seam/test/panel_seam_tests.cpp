// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The seam of the streamed panels: the defaults of a sender that cannot detach and calls
// QarOS at once, and the empty common name of a description.

#include "qar_qt_streaming/seam/panel_types.hpp"
#include "qar_qt_streaming/seam/panel_sender.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace qar_qt_streaming::seam;

namespace
{

class CountingPanelSender : public IPanelSender
{
public:
  bool try_begin_frame(bool & out_ready) override { out_ready = false; return true; }
  bool submit_frame(const PanelPixels &) override { return true; }
  bool set_placement(const PanelPlacement &) override { return true; }
  PanelCloseReport close(bool panel_closed_in_ar) override
  {
    ++closes;
    last_closed_in_ar = panel_closed_in_ar;
    return {};
  }
  const std::string & last_error() const override { return error; }

  int closes = 0;
  bool last_closed_in_ar = true;
  std::string error;
};

}  // namespace

TEST(PanelSeams, ASenderThatCannotDetachClosesItsPanelInstead)
{
  CountingPanelSender sender;
  EXPECT_FALSE(sender.supports_detach());
  sender.detach();
  EXPECT_EQ(sender.closes, 1);
  EXPECT_FALSE(sender.last_closed_in_ar);
}

TEST(PanelSeams, DescriptionsCarryAnEmptyCommonNameUntilTheHostFillsIt)
{
  EXPECT_TRUE(PanelDesc{}.common_name.empty());
}

TEST(PanelSeams, PausedIsADistinctNotReadyCause)
{
  EXPECT_NE(PanelNotReadyCause::Paused, PanelNotReadyCause::NoDestination);
  EXPECT_NE(PanelNotReadyCause::Paused, PanelNotReadyCause::NoActiveTransfers);
  EXPECT_NE(PanelNotReadyCause::Paused, PanelNotReadyCause::Reconnecting);
}

TEST(PanelSeams, CreatingIsADistinctNotReadyCause)
{
  EXPECT_NE(PanelNotReadyCause::Creating, PanelNotReadyCause::NoDestination);
  EXPECT_NE(PanelNotReadyCause::Creating, PanelNotReadyCause::NoActiveTransfers);
  EXPECT_NE(PanelNotReadyCause::Creating, PanelNotReadyCause::Reconnecting);
  EXPECT_NE(PanelNotReadyCause::Creating, PanelNotReadyCause::Paused);
}

TEST(PanelSeams, AQueuedCloseHasItsOwnName)
{
  EXPECT_STREQ(to_string(PanelCloseOutcome::Queued), "queued");
  EXPECT_STRNE(to_string(PanelCloseOutcome::Queued), to_string(PanelCloseOutcome::NoPanel));
}

TEST(PanelSeams, ASenderThatCallsQarOsAtOnceIsNeverPendingAndHasNoResults)
{
  CountingPanelSender sender;
  EXPECT_FALSE(sender.pending());
  std::vector<PanelOpResult> results;
  sender.drain_results(results);
  EXPECT_TRUE(results.empty());
}
