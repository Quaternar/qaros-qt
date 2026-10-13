// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// QAR_PANEL_CLOSE_MODE: the pure parser behind "destroy (default) or notice".
//
// The parser never reads the environment itself (the host passes getenv's result
// in), so everything here is plain strings: no QApplication, no session.

#include <gtest/gtest.h>

#include "qar_qt_streaming/panel_close_mode.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"

#include <string>

using qar_qt_streaming::PanelCloseMode;
using qar_qt_streaming::PanelCloseModeChoice;
using qar_qt_streaming::parse_panel_close_mode;
using qar_qt_streaming::to_string;

TEST(PanelCloseModeParse, UnsetIsTheDefaultWithoutAWarning)
{
  const PanelCloseModeChoice choice = parse_panel_close_mode(nullptr);
  EXPECT_EQ(choice.mode, PanelCloseMode::Destroy);
  EXPECT_FALSE(choice.explicitly_chosen);
  EXPECT_TRUE(choice.warning.empty());
}

TEST(PanelCloseModeParse, EmptyAndBlankMeanUnsetToo)
{
  for (const char * text : {"", " ", "\t", "  \r\n "}) {
    const PanelCloseModeChoice choice = parse_panel_close_mode(text);
    EXPECT_EQ(choice.mode, PanelCloseMode::Destroy) << "'" << text << "'";
    EXPECT_FALSE(choice.explicitly_chosen) << "'" << text << "'";
    EXPECT_TRUE(choice.warning.empty()) << "a variable set to nothing is not a typo";
  }
}

TEST(PanelCloseModeParse, DestroyIsRecognised)
{
  const PanelCloseModeChoice choice = parse_panel_close_mode("destroy");
  EXPECT_EQ(choice.mode, PanelCloseMode::Destroy);
  EXPECT_TRUE(choice.explicitly_chosen);
  EXPECT_TRUE(choice.warning.empty());
}

TEST(PanelCloseModeParse, NoticeIsRecognised)
{
  const PanelCloseModeChoice choice = parse_panel_close_mode("notice");
  EXPECT_EQ(choice.mode, PanelCloseMode::Notice);
  EXPECT_TRUE(choice.explicitly_chosen);
  EXPECT_TRUE(choice.warning.empty());
}

TEST(PanelCloseModeParse, CaseAndSurroundingWhitespaceDoNotMatter)
{
  EXPECT_EQ(parse_panel_close_mode("NOTICE").mode, PanelCloseMode::Notice);
  EXPECT_EQ(parse_panel_close_mode("Notice").mode, PanelCloseMode::Notice);
  EXPECT_EQ(parse_panel_close_mode("  notice\t").mode, PanelCloseMode::Notice);
  EXPECT_EQ(parse_panel_close_mode("\tDeStRoY \r\n").mode, PanelCloseMode::Destroy);
  EXPECT_TRUE(parse_panel_close_mode(" NOTICE ").explicitly_chosen);
  EXPECT_TRUE(parse_panel_close_mode(" NOTICE ").warning.empty());
}

TEST(PanelCloseModeParse, AnUnknownValueFallsBackToTheDefaultWithOneWarningThatNamesIt)
{
  const PanelCloseModeChoice choice = parse_panel_close_mode("notise");
  EXPECT_EQ(choice.mode, PanelCloseMode::Destroy) << "an unknown value must not select Notice";
  EXPECT_FALSE(choice.explicitly_chosen);
  ASSERT_FALSE(choice.warning.empty());
  EXPECT_NE(choice.warning.find("QAR_PANEL_CLOSE_MODE"), std::string::npos);
  EXPECT_NE(choice.warning.find("notise"), std::string::npos) << "the bad value, as typed";
  EXPECT_NE(choice.warning.find("destroy"), std::string::npos);
  EXPECT_NE(choice.warning.find("notice"), std::string::npos);
}

TEST(PanelCloseModeParse, NearMissesAndNumbersAreNotModes)
{
  for (const char * text : {"destroyed", "notices", "tombstone", "keep", "0", "1", "true", "off",
      "dest roy", "no tice"})
  {
    const PanelCloseModeChoice choice = parse_panel_close_mode(text);
    EXPECT_EQ(choice.mode, PanelCloseMode::Destroy) << "'" << text << "'";
    EXPECT_FALSE(choice.warning.empty()) << "'" << text << "' must be reported, not guessed at";
  }
}

TEST(PanelCloseModeParse, TheWarningKeepsTheValueExactlyAsTheOperatorTypedIt)
{
  // Not the trimmed or lower-cased form: the point of the warning is that they
  // can find what they wrote.
  const PanelCloseModeChoice choice = parse_panel_close_mode("  Tombstone ");
  EXPECT_NE(choice.warning.find("'  Tombstone '"), std::string::npos) << choice.warning;
}

TEST(PanelCloseModeNames, TheSpellingsAreTheOnesTheParserAccepts)
{
  EXPECT_STREQ(to_string(PanelCloseMode::Destroy), "destroy");
  EXPECT_STREQ(to_string(PanelCloseMode::Notice), "notice");
  // Round trip: what the log prints is what the variable takes.
  EXPECT_EQ(parse_panel_close_mode(to_string(PanelCloseMode::Destroy)).mode,
    PanelCloseMode::Destroy);
  EXPECT_EQ(parse_panel_close_mode(to_string(PanelCloseMode::Notice)).mode,
    PanelCloseMode::Notice);
  EXPECT_STREQ(qar_qt_streaming::kPanelCloseModeEnv, "QAR_PANEL_CLOSE_MODE");
}

TEST(PanelCloseModeConfig, TheManagerConfigDefaultsToDestroy)
{
  // The operator-visible default, pinned where a refactor would trip over it.
  qar_qt_streaming::QtPanelStreamingConfig config;
  EXPECT_EQ(config.close_mode, PanelCloseMode::Destroy);
}
