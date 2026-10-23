// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The scroll-area census: what it counts (visible areas, hidden ones, ranges per pixel and per
// item, stacked and largest overflow, horizontal bars, areas that scroll by design) and the
// lines a stream says about it (the first frame, 1.5 s later, a request, a revival, a rebuild).
// Real widgets under the offscreen platform; the stream runs against the recording sender.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_panel_stream.hpp"
#include "qar_qt_streaming/scroll_census.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using qar_qt_streaming::describe_scroll_census;
using qar_qt_streaming::kScrollsByDesignProperty;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::ScrollAreaCensus;
using qar_qt_streaming::ScrollCensus;
using qar_qt_streaming::take_scroll_census;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

/// Lets the queued calls of a widget tree run: delayed item layouts, scroll bar updates.
void
settle()
{
  for (int i = 0; i < 5; ++i) {
    QCoreApplication::processEvents();
  }
}

/// A tree of @p rows rows, no header, no columns wider than the view, so that only its vertical
/// range can be non-empty.
QTreeWidget *
make_tree(QWidget * parent, int rows, const char * name)
{
  auto * tree = new QTreeWidget(parent);
  tree->setObjectName(name);
  tree->setHeaderHidden(true);
  tree->setColumnCount(1);
  tree->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
  for (int i = 0; i < rows; ++i) {
    tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("row %1").arg(i))));
  }
  return tree;
}

/// A shown 300 x 200 widget; the test puts its content into it.
struct Shown
{
  Shown()
  {
    root.setObjectName("root");
    root.resize(300, 200);
  }

  void show()
  {
    root.show();
    root.ensurePolished();
    settle();
  }

  QWidget root;
};

const ScrollAreaCensus *
find_area(const ScrollCensus & census, const std::string & object_name)
{
  for (const ScrollAreaCensus & area : census.areas) {
    if (area.object_name == object_name) {
      return &area;
    }
  }
  return nullptr;
}

}  // namespace

// --- what the census counts -----------------------------------------------------

TEST(ScrollCensus, ATreeWithMoreRowsThanFitIsAPerItemAreaWithARangeInRows)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * tree = make_tree(&s.root, 40, "tree");
  layout->addWidget(tree);
  s.show();
  tree->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u) << "the tree; its header view is not an area";
  const ScrollAreaCensus & a = census.areas[0];
  EXPECT_EQ(a.class_name, "QTreeWidget");
  EXPECT_EQ(a.object_name, "tree");
  EXPECT_GT(a.v_max, a.v_min);
  EXPECT_EQ(a.v_min, tree->verticalScrollBar()->minimum());
  EXPECT_EQ(a.v_max, tree->verticalScrollBar()->maximum());
  EXPECT_EQ(a.page_step, tree->verticalScrollBar()->pageStep());
  EXPECT_EQ(a.viewport_w, tree->viewport()->width());
  EXPECT_EQ(a.viewport_h, tree->viewport()->height());
  EXPECT_TRUE(a.v_bar);
  EXPECT_FALSE(a.h_bar);
  EXPECT_EQ(a.h_bar_extent, 0);
  EXPECT_TRUE(a.per_item);
  EXPECT_GT(a.row_h, 0);
  EXPECT_EQ(a.row_h, tree->sizeHintForRow(0));
  EXPECT_EQ(a.overflow_px, (a.v_max - a.v_min) * a.row_h) << "rows, turned into pixels";
  EXPECT_FALSE(a.by_design);
  EXPECT_EQ(census.hidden_areas, 0);
  EXPECT_EQ(census.unmeasured, 0);
  EXPECT_TRUE(census.any_vertical_bar);
  EXPECT_FALSE(census.any_vertical_bar_by_design);
  EXPECT_EQ(census.largest_overflow_px, a.overflow_px);
  EXPECT_EQ(census.stacked_overflow_px, a.overflow_px);
}

TEST(ScrollCensus, ATreeInPerPixelModeCountsPixels)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * tree = make_tree(&s.root, 40, "tree");
  tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  layout->addWidget(tree);
  s.show();
  tree->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  const ScrollAreaCensus & a = census.areas[0];
  EXPECT_FALSE(a.per_item);
  EXPECT_EQ(a.row_h, 0);
  EXPECT_GT(a.v_max, 0);
  EXPECT_EQ(a.overflow_px, a.v_max - a.v_min) << "the range already is pixels";
  EXPECT_TRUE(a.v_bar);
}

TEST(ScrollCensus, AScrollAreaWithATallChildIsAPerPixelRange)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * scroll = new QScrollArea(&s.root);
  scroll->setObjectName("scroll");
  auto * tall = new QWidget();
  tall->setFixedSize(150, 700);
  scroll->setWidget(tall);
  layout->addWidget(scroll);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  const ScrollAreaCensus * a = find_area(census, "scroll");
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->class_name, "QScrollArea");
  EXPECT_FALSE(a->per_item);
  EXPECT_EQ(a->v_max - a->v_min, 700 - scroll->viewport()->height());
  EXPECT_EQ(a->overflow_px, 700 - scroll->viewport()->height());
  EXPECT_TRUE(a->v_bar);
  EXPECT_FALSE(a->h_bar);
}

TEST(ScrollCensus, AnAreaThatFitsHasNoRangeAndNoBar)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * tree = make_tree(&s.root, 2, "tree");
  layout->addWidget(tree);
  s.show();
  tree->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  const ScrollAreaCensus & a = census.areas[0];
  EXPECT_EQ(a.v_max, a.v_min);
  EXPECT_FALSE(a.v_bar);
  EXPECT_EQ(a.overflow_px, 0);
  EXPECT_FALSE(census.any_vertical_bar);
  EXPECT_EQ(census.stacked_overflow_px, 0);
  EXPECT_EQ(census.largest_overflow_px, 0);
}

TEST(ScrollCensus, ThePageThatIsNotShownIsHiddenAndTheCurrentOneIsVisible)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * tabs = new QTabWidget(&s.root);
  QTreeWidget * first = make_tree(nullptr, 40, "first");
  QTreeWidget * second = make_tree(nullptr, 40, "second");
  tabs->addTab(first, "one");
  tabs->addTab(second, "two");
  layout->addWidget(tabs);
  s.show();
  first->doItemsLayout();
  second->doItemsLayout();
  settle();

  ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_EQ(census.areas[0].object_name, "first");
  EXPECT_EQ(census.hidden_areas, 1);

  tabs->setCurrentIndex(1);
  second->doItemsLayout();
  settle();
  census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_EQ(census.areas[0].object_name, "second");
  EXPECT_EQ(census.hidden_areas, 1);
}

TEST(ScrollCensus, ARootThatIsAScrollAreaCountsItself)
{
  QTreeWidget tree;
  tree.setObjectName("root_tree");
  tree.setHeaderHidden(true);
  tree.setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
  for (int i = 0; i < 40; ++i) {
    tree.addTopLevelItem(new QTreeWidgetItem(QStringList(QString("row %1").arg(i))));
  }
  tree.resize(200, 150);
  tree.show();
  settle();
  tree.doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(tree);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_EQ(census.areas[0].object_name, "root_tree");
  EXPECT_TRUE(census.areas[0].v_bar);
  EXPECT_EQ(census.hidden_areas, 0);
}

TEST(ScrollCensus, ADialogParentedIntoTheRootIsAPanelOfItsOwnAndIsNotCounted)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * own = make_tree(&s.root, 40, "own");
  layout->addWidget(own);
  s.show();
  auto * dialog = new QDialog(&s.root);
  auto * dialog_layout = new QVBoxLayout(dialog);
  QTreeWidget * inner = make_tree(dialog, 40, "inner");
  dialog_layout->addWidget(inner);
  dialog->resize(200, 120);
  dialog->show();
  settle();
  own->doItemsLayout();
  inner->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_EQ(census.areas[0].object_name, "own");
  EXPECT_EQ(census.hidden_areas, 0) << "the dialog's tree is neither visible nor hidden here";
  delete dialog;
}

// --- sums ---------------------------------------------------------------------

TEST(ScrollCensus, TwoTreesInASplitterAreStackedByTheirSumAndLargestByTheirMaximum)
{
  Shown s;
  s.root.resize(300, 260);
  auto * layout = new QVBoxLayout(&s.root);
  auto * splitter = new QSplitter(Qt::Vertical, &s.root);
  QTreeWidget * upper = make_tree(nullptr, 30, "upper");
  QTreeWidget * lower = make_tree(nullptr, 50, "lower");
  splitter->addWidget(upper);
  splitter->addWidget(lower);
  layout->addWidget(splitter);
  s.show();
  upper->doItemsLayout();
  lower->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(s.root);
  const ScrollAreaCensus * a = find_area(census, "upper");
  const ScrollAreaCensus * b = find_area(census, "lower");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_GT(a->overflow_px, 0);
  ASSERT_GT(b->overflow_px, 0);
  EXPECT_NE(a->overflow_px, b->overflow_px) << "30 and 50 rows in about the same height";
  EXPECT_EQ(census.stacked_overflow_px, a->overflow_px + b->overflow_px);
  EXPECT_EQ(census.largest_overflow_px, std::max(a->overflow_px, b->overflow_px));
  EXPECT_LT(census.largest_overflow_px, census.stacked_overflow_px);
}

TEST(ScrollCensus, AHorizontalBarAloneAddsNothingToTheSumsAndIsCountedApart)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * scroll = new QScrollArea(&s.root);
  scroll->setObjectName("wide");
  auto * wide = new QWidget();
  wide->setFixedSize(900, 40);
  scroll->setWidget(wide);
  layout->addWidget(scroll);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  const ScrollAreaCensus & a = census.areas[0];
  EXPECT_TRUE(a.h_bar);
  EXPECT_FALSE(a.v_bar);
  EXPECT_GT(a.h_bar_extent, 0);
  EXPECT_EQ(a.h_bar_extent, scroll->horizontalScrollBar()->sizeHint().height());
  EXPECT_EQ(a.overflow_px, 0);
  EXPECT_EQ(census.stacked_overflow_px, 0) << "a taller area keeps a horizontal bar";
  EXPECT_EQ(census.largest_overflow_px, 0);
  EXPECT_EQ(census.horizontal_only_px, a.h_bar_extent);
  EXPECT_FALSE(census.any_vertical_bar) << "only a horizontal bar";
}

TEST(ScrollCensus, AHorizontalBarNextToAVerticalOverflowAddsItsHeightToIt)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * scroll = new QScrollArea(&s.root);
  scroll->setObjectName("both");
  auto * big = new QWidget();
  big->setFixedSize(900, 700);
  scroll->setWidget(big);
  layout->addWidget(scroll);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  const ScrollAreaCensus & a = census.areas[0];
  ASSERT_TRUE(a.h_bar);
  ASSERT_GT(a.overflow_px, 0);
  EXPECT_EQ(census.stacked_overflow_px, a.overflow_px + a.h_bar_extent)
    << "the bar takes room from the viewport, so the area needs its height as well";
  EXPECT_EQ(census.largest_overflow_px, a.overflow_px + a.h_bar_extent);
  EXPECT_EQ(census.horizontal_only_px, 0) << "taller, it would lose that bar too";
}

TEST(ScrollCensus, TheHorizontalBarsAreSummedOnlyWhereThereIsAVerticalOverflow)
{
  // Wide and low, wide and tall, narrow and tall: one area of each kind, one above the other.
  Shown s;
  s.root.resize(300, 700);
  auto * layout = new QVBoxLayout(&s.root);
  const auto add = [&](const char * name, int width, int height) {
      auto * scroll = new QScrollArea(&s.root);
      scroll->setObjectName(name);
      scroll->setMinimumHeight(110);
      auto * content = new QWidget();
      content->setFixedSize(width, height);
      scroll->setWidget(content);
      layout->addWidget(scroll);
    };
  add("wide_low", 900, 10);
  add("wide_tall", 900, 700);
  add("narrow_tall", 20, 700);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  const ScrollAreaCensus * low = find_area(census, "wide_low");
  const ScrollAreaCensus * both = find_area(census, "wide_tall");
  const ScrollAreaCensus * tall = find_area(census, "narrow_tall");
  ASSERT_NE(low, nullptr);
  ASSERT_NE(both, nullptr);
  ASSERT_NE(tall, nullptr);
  ASSERT_TRUE(low->h_bar);
  ASSERT_EQ(low->overflow_px, 0);
  ASSERT_TRUE(both->h_bar);
  ASSERT_GT(both->overflow_px, 0);
  ASSERT_FALSE(tall->h_bar);
  ASSERT_GT(tall->overflow_px, 0);
  const int both_cost = both->overflow_px + both->h_bar_extent;
  EXPECT_EQ(census.stacked_overflow_px, both_cost + tall->overflow_px);
  EXPECT_EQ(census.largest_overflow_px, std::max(both_cost, tall->overflow_px));
  EXPECT_EQ(census.horizontal_only_px, low->h_bar_extent);
}

TEST(ScrollCensus, ABarOfAnAreaThatScrollsByDesignIsNotCountedApart)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * scroll = new QScrollArea(&s.root);
  scroll->setProperty(kScrollsByDesignProperty, true);
  auto * wide = new QWidget();
  wide->setFixedSize(900, 40);
  scroll->setWidget(wide);
  layout->addWidget(scroll);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  ASSERT_TRUE(census.areas[0].h_bar);
  EXPECT_EQ(census.horizontal_only_px, 0);
  EXPECT_EQ(census.stacked_overflow_px, 0);
}

TEST(ScrollCensus, AHorizontalBarThatIsAlwaysOffAddsNothing)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * scroll = new QScrollArea(&s.root);
  scroll->setObjectName("wide");
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto * wide = new QWidget();
  wide->setFixedSize(900, 40);
  scroll->setWidget(wide);
  layout->addWidget(scroll);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_GT(scroll->horizontalScrollBar()->maximum(), 0);
  EXPECT_FALSE(census.areas[0].h_bar) << "the range is there, the policy keeps the bar away";
  EXPECT_EQ(census.areas[0].h_bar_extent, 0);
  EXPECT_EQ(census.stacked_overflow_px, 0);
}

// --- the bar is the range under the policy ------------------------------------------

TEST(ScrollCensus, ARangeUnderPolicyAlwaysOffShowsNoBarButStillOverflows)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * tree = make_tree(&s.root, 40, "tree");
  tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  layout->addWidget(tree);
  s.show();
  tree->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  const ScrollAreaCensus & a = census.areas[0];
  EXPECT_GT(a.v_max, a.v_min) << "the range is kept whatever the policy";
  EXPECT_FALSE(a.v_bar);
  EXPECT_FALSE(census.any_vertical_bar);
  EXPECT_GT(a.overflow_px, 0) << "cut-off content still needs the room";
  EXPECT_EQ(census.stacked_overflow_px, a.overflow_px);
}

TEST(ScrollCensus, AlwaysOnWithoutARangeIsNotABarThatNeedsRoom)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * tree = make_tree(&s.root, 2, "tree");
  tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  layout->addWidget(tree);
  s.show();
  tree->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_EQ(census.areas[0].v_max, census.areas[0].v_min);
  EXPECT_FALSE(census.areas[0].v_bar) << "a bar is shown by a range, as the census reads it";
  EXPECT_EQ(census.stacked_overflow_px, 0);
}

TEST(ScrollCensus, TheBarFollowsTheRangeBeforeQtHasShownIt)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * tree = make_tree(&s.root, 2, "tree");
  layout->addWidget(tree);
  s.show();
  tree->doItemsLayout();
  settle();
  ASSERT_FALSE(take_scroll_census(s.root).areas[0].v_bar);

  // Rows are added, and the item layout runs without the queued call that shows the bar having
  // run: the range is already there, the bar widget is not shown yet.
  for (int i = 0; i < 60; ++i) {
    tree->addTopLevelItem(new QTreeWidgetItem(QStringList(QString("late %1").arg(i))));
  }
  tree->doItemsLayout();
  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_GT(census.areas[0].v_max, 0);
  EXPECT_TRUE(census.areas[0].v_bar);
  EXPECT_TRUE(census.any_vertical_bar);
}

// --- by design -------------------------------------------------------------------------

TEST(ScrollCensus, ATextBrowserScrollsByDesignAndIsLeftOutOfTheSums)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * browser = new QTextBrowser(&s.root);
  browser->setObjectName("description");
  QString text;
  for (int i = 0; i < 120; ++i) {
    text += QString("line %1\n").arg(i);
  }
  browser->setPlainText(text);
  layout->addWidget(browser);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  const ScrollAreaCensus & a = census.areas[0];
  EXPECT_EQ(a.class_name, "QTextBrowser");
  EXPECT_TRUE(a.by_design);
  EXPECT_TRUE(a.v_bar);
  EXPECT_GT(a.overflow_px, 0);
  EXPECT_TRUE(census.any_vertical_bar_by_design);
  EXPECT_FALSE(census.any_vertical_bar);
  EXPECT_EQ(census.stacked_overflow_px, 0);
  EXPECT_EQ(census.largest_overflow_px, 0);
}

TEST(ScrollCensus, APlainTextEditCountsLinesAndScrollsByDesign)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * log = new QPlainTextEdit(&s.root);
  log->setObjectName("log");
  QString text;
  for (int i = 0; i < 100; ++i) {
    text += QString("entry %1\n").arg(i);
  }
  log->setPlainText(text);
  layout->addWidget(log);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  const ScrollAreaCensus & a = census.areas[0];
  EXPECT_TRUE(a.by_design);
  EXPECT_TRUE(a.per_item) << "its bar counts lines";
  EXPECT_GT(a.row_h, 0);
  EXPECT_EQ(a.overflow_px, (a.v_max - a.v_min) * a.row_h);
  EXPECT_EQ(census.stacked_overflow_px, 0);
}

TEST(ScrollCensus, ThePropertyOnTheAreaOrOnAWidgetAboveItDecidesAndTheNearestWins)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * browser = new QTextBrowser(&s.root);
  browser->setObjectName("browser");
  browser->setPlainText(QString("x\n").repeated(120));
  auto * group = new QWidget(&s.root);
  group->setObjectName("group");
  auto * group_layout = new QVBoxLayout(group);
  QTreeWidget * inside = make_tree(group, 40, "inside");
  QTreeWidget * opted_out = make_tree(group, 40, "opted_out");
  group_layout->addWidget(inside);
  group_layout->addWidget(opted_out);
  QTreeWidget * plain = make_tree(&s.root, 40, "plain");
  layout->addWidget(browser);
  layout->addWidget(group);
  layout->addWidget(plain);
  s.root.resize(300, 500);
  s.show();
  for (QTreeWidget * t : {inside, opted_out, plain}) {
    t->doItemsLayout();
  }
  settle();

  // Nothing marked: the browser by class, the trees not.
  ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 4u);
  EXPECT_TRUE(find_area(census, "browser")->by_design);
  EXPECT_FALSE(find_area(census, "inside")->by_design);
  EXPECT_FALSE(find_area(census, "plain")->by_design);

  // The property on the group covers everything in it; false on one tree takes that one out;
  // false on the browser makes it an ordinary area.
  group->setProperty(kScrollsByDesignProperty, true);
  opted_out->setProperty(kScrollsByDesignProperty, false);
  browser->setProperty(kScrollsByDesignProperty, false);
  census = take_scroll_census(s.root);
  EXPECT_TRUE(find_area(census, "inside")->by_design);
  EXPECT_FALSE(find_area(census, "opted_out")->by_design);
  EXPECT_FALSE(find_area(census, "browser")->by_design);
  EXPECT_FALSE(find_area(census, "plain")->by_design);
  const int expected = find_area(census, "opted_out")->overflow_px +
    find_area(census, "browser")->overflow_px + find_area(census, "plain")->overflow_px;
  EXPECT_EQ(census.stacked_overflow_px, expected) << "the group's tree is not in the sum";

  // On the root it covers everything under it.
  s.root.setProperty(kScrollsByDesignProperty, true);
  census = take_scroll_census(s.root);
  EXPECT_TRUE(find_area(census, "plain")->by_design);
  EXPECT_TRUE(find_area(census, "inside")->by_design);
  EXPECT_FALSE(find_area(census, "opted_out")->by_design) << "the nearest property wins";
  EXPECT_FALSE(find_area(census, "browser")->by_design) << "the nearest property wins";
}

TEST(ScrollCensus, APropertyAboveTheRootIsNotLookedAt)
{
  QWidget outer;
  outer.setProperty(kScrollsByDesignProperty, true);
  auto * layout = new QVBoxLayout(&outer);
  auto * root = new QWidget(&outer);
  auto * root_layout = new QVBoxLayout(root);
  QTreeWidget * tree = make_tree(root, 40, "tree");
  root_layout->addWidget(tree);
  layout->addWidget(root);
  outer.resize(300, 200);
  outer.show();
  settle();
  tree->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(*root);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_FALSE(census.areas[0].by_design) << "the census root is where the search stops";
}

// --- the text -------------------------------------------------------------------------

TEST(ScrollCensus, DescribeIsOneAreaAfterTheOtherInAnExactFormat)
{
  ScrollCensus census;
  ScrollAreaCensus tree;
  tree.class_name = "QTreeView";
  tree.object_name = "";
  tree.v_min = 0;
  tree.v_max = 7;
  tree.page_step = 6;
  tree.viewport_w = 455;
  tree.viewport_h = 187;
  tree.v_bar = true;
  tree.per_item = true;
  tree.row_h = 17;
  tree.overflow_px = 119;
  ScrollAreaCensus text;
  text.class_name = "QTextBrowser";
  text.object_name = "description";
  text.v_max = 30;
  text.page_step = 100;
  text.viewport_w = 300;
  text.viewport_h = 100;
  text.v_bar = true;
  text.h_bar = true;
  text.overflow_px = 30;
  text.by_design = true;
  census.areas = {tree, text};

  EXPECT_EQ(
    describe_scroll_census(census),
    "QTreeView '' v=0..7 page=6 viewport=455x187 vbar=1 hbar=0 per-item overflow=119; "
    "QTextBrowser 'description' v=0..30 page=100 viewport=300x100 vbar=1 hbar=1 per-pixel "
    "overflow=30 by-design");
  census.areas = {tree};
  EXPECT_EQ(
    describe_scroll_census(census),
    "QTreeView '' v=0..7 page=6 viewport=455x187 vbar=1 hbar=0 per-item overflow=119");
  EXPECT_EQ(describe_scroll_census(ScrollCensus()), "none");
}

TEST(ScrollCensus, DescribeOfARealCensusNamesTheClassTheObjectAndTheRange)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * tree = make_tree(&s.root, 40, "tree");
  layout->addWidget(tree);
  s.show();
  tree->doItemsLayout();
  settle();

  const ScrollCensus census = take_scroll_census(s.root);
  const ScrollAreaCensus & a = census.areas.at(0);
  const std::string expected = "QTreeWidget 'tree' v=" + std::to_string(a.v_min) + ".." +
    std::to_string(a.v_max) + " page=" + std::to_string(a.page_step) + " viewport=" +
    std::to_string(a.viewport_w) + "x" + std::to_string(a.viewport_h) +
    " vbar=1 hbar=0 per-item overflow=" + std::to_string(a.overflow_px);
  EXPECT_EQ(describe_scroll_census(census), expected);
}

TEST(ScrollCensus, NamesWithLettersOutsideAsciiAreShownAsQuestionMarks)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  QTreeWidget * tree = make_tree(&s.root, 2, "tree");
  tree->setObjectName(QString::fromUtf8("str\xC3\xA1nka"));
  layout->addWidget(tree);
  s.show();

  const ScrollCensus census = take_scroll_census(s.root);
  ASSERT_EQ(census.areas.size(), 1u);
  EXPECT_EQ(census.areas[0].object_name, "str??nka");
  for (const char c : describe_scroll_census(census)) {
    EXPECT_LT(static_cast<unsigned char>(c), 0x80u);
  }
}

// --- the lines of a stream -------------------------------------------------------------

namespace
{

/// A streamable widget with one tree of @p rows rows in a 300 x 200 frame, and a repaintable
/// block so that the test can make the next frame differ.
class TreePanel
{
public:
  explicit TreePanel(int rows, const char * name = "tree")
  {
    root_ = new QWidget();
    root_->setObjectName("panel");
    auto * layout = new QVBoxLayout(root_);
    tree_ = make_tree(root_, rows, name);
    layout->addWidget(tree_);
    block_ = new QLabel(root_);
    block_->setAutoFillBackground(true);
    block_->setFixedHeight(10);
    layout->addWidget(block_);
    root_->resize(300, 200);
    root_->show();
    root_->ensurePolished();
    settle();
    tree_->doItemsLayout();
    settle();
  }

  ~TreePanel() { delete root_; }

  QWidget * get() const { return root_; }
  QTreeWidget * tree() const { return tree_; }

  /// A different frame from the one before.
  void change()
  {
    QPalette palette = block_->palette();
    palette.setColor(QPalette::Window, flip_ ? Qt::red : Qt::blue);
    flip_ = !flip_;
    block_->setPalette(palette);
    settle();
  }

private:
  QWidget * root_ = nullptr;
  QTreeWidget * tree_ = nullptr;
  QLabel * block_ = nullptr;
  bool flip_ = false;
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.max_fps = 0;
  options.keepalive_seconds = 0.0;
  return options;
}

std::vector<std::string>
lines_with(const std::vector<std::string> & lines, const std::string & needle)
{
  std::vector<std::string> out;
  for (const std::string & line : lines) {
    if (line.find(needle) != std::string::npos) {
      out.push_back(line);
    }
  }
  return out;
}

}  // namespace

TEST(StreamCensus, TheFirstFrameIsMeasuredAndTheLineHasTheContractedShape)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("dock:Views", panel.get(), options_no_pacing(), factory);
  EXPECT_TRUE(stream.take_notes().empty());
  ASSERT_TRUE(stream.tick(10.0));

  const ScrollCensus census = take_scroll_census(*panel.get());
  ASSERT_EQ(census.areas.size(), 1u);
  const std::vector<std::string> lines = lines_with(stream.take_notes(), "scroll census");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(
    lines[0],
    "'dock:Views' scroll census (first frame): 1 area(s), 0 hidden: " +
    describe_scroll_census(census));
  EXPECT_NE(lines[0].find("QTreeWidget 'tree' v=0.."), std::string::npos);
  EXPECT_NE(lines[0].find(" vbar=1 hbar=0 per-item overflow="), std::string::npos);
}

TEST(StreamCensus, AWidgetWithoutAScrollAreaSaysNone)
{
  QWidget plain;
  plain.resize(100, 80);
  plain.show();
  settle();
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", &plain, options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  const std::vector<std::string> lines = lines_with(stream.take_notes(), "scroll census");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "'p' scroll census (first frame): 0 area(s), 0 hidden: none");
}

TEST(StreamCensus, AHiddenPageIsCountedAsHidden)
{
  Shown s;
  auto * layout = new QVBoxLayout(&s.root);
  auto * tabs = new QTabWidget(&s.root);
  QTreeWidget * first = make_tree(nullptr, 2, "first");
  QTreeWidget * second = make_tree(nullptr, 2, "second");
  tabs->addTab(first, "one");
  tabs->addTab(second, "two");
  layout->addWidget(tabs);
  s.show();
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("tabs", &s.root, options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  const std::vector<std::string> lines = lines_with(stream.take_notes(), "scroll census");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_NE(lines[0].find("1 area(s), 1 hidden: QTreeWidget 'first'"), std::string::npos)
    << lines[0];
}

TEST(StreamCensus, TheSecondCensusComesWhen1_5SecondsOfTheStreamClockHavePassed)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(10.0));
  stream.take_notes();
  stream.take_warnings();

  // Not before: nothing, however many ticks there are.
  for (const double t : {10.2, 10.5, 11.0, 11.4}) {
    stream.tick(t);
  }
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());
  EXPECT_TRUE(stream.take_warnings().empty());

  stream.tick(11.5);
  const std::vector<std::string> notes = stream.take_notes();
  const std::vector<std::string> census = lines_with(notes, "scroll census (+1.5 s)");
  ASSERT_EQ(census.size(), 1u);
  EXPECT_NE(census[0].find("'p' scroll census (+1.5 s): 1 area(s), 0 hidden: QTreeWidget 'tree'"),
    std::string::npos)
    << census[0];

  // Once: later ticks do not repeat it.
  for (const double t : {11.6, 12.0, 14.0}) {
    stream.tick(t);
  }
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());
}

TEST(StreamCensus, ATreeThatStillScrollsIsAWarningWithItsOverflowAndItsShareOfTheViewport)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.take_notes();
  EXPECT_TRUE(stream.take_warnings().empty()) << "nothing is said as a warning at the first frame";

  stream.tick(1.5);
  const ScrollCensus census = take_scroll_census(*panel.get());
  ASSERT_EQ(census.areas.size(), 1u);
  const ScrollAreaCensus & a = census.areas[0];
  ASSERT_TRUE(a.v_bar);
  const int percent = static_cast<int>(std::lround(100.0 * a.overflow_px / a.viewport_h));
  const std::vector<std::string> warnings = stream.take_warnings();
  ASSERT_EQ(warnings.size(), 1u);
  EXPECT_EQ(
    warnings[0],
    "'p' still shows a vertical scroll bar 1.5 s after its first frame: QTreeWidget 'tree' "
    "overflows by " + std::to_string(a.overflow_px) + " px (" + std::to_string(percent) +
    " % of its " + std::to_string(a.viewport_h) + " px viewport)");
}

TEST(StreamCensus, AnAreaThatScrollsByDesignIsAnInfoLineAndNeverAWarning)
{
  TreePanel panel(40);
  panel.tree()->setProperty(kScrollsByDesignProperty, true);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.take_notes();

  stream.tick(1.5);
  EXPECT_TRUE(stream.take_warnings().empty());
  const ScrollCensus census = take_scroll_census(*panel.get());
  const ScrollAreaCensus & a = census.areas.at(0);
  const int percent = static_cast<int>(std::lround(100.0 * a.overflow_px / a.viewport_h));
  const std::vector<std::string> info =
    lines_with(stream.take_notes(), "an area that scrolls by design shows a vertical scroll bar");
  ASSERT_EQ(info.size(), 1u);
  EXPECT_EQ(
    info[0],
    "'p' 1.5 s after its first frame an area that scrolls by design shows a vertical scroll bar: "
    "QTreeWidget 'tree' overflows by " + std::to_string(a.overflow_px) + " px (" +
    std::to_string(percent) + " % of its " + std::to_string(a.viewport_h) + " px viewport)");
}

TEST(StreamCensus, ATreeThatFitsIsNotWarnedAbout)
{
  TreePanel panel(2);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.tick(1.6);
  EXPECT_TRUE(stream.take_warnings().empty());
  const std::vector<std::string> notes = stream.take_notes();
  EXPECT_EQ(lines_with(notes, "scroll census").size(), 2u);
  EXPECT_TRUE(lines_with(notes, "scrolls by design").empty());
}

TEST(StreamCensus, ARequestIsTakenAtTheNextSubmittedFrameWithItsReason)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.take_notes();

  stream.request_scroll_census("tab 'AR tools' selected");
  // A tick that submits nothing takes nothing.
  EXPECT_FALSE(stream.tick(0.1));
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());

  panel.change();
  ASSERT_TRUE(stream.tick(0.2));
  const std::vector<std::string> lines = lines_with(stream.take_notes(), "scroll census");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(
    lines[0].rfind("'p' scroll census (tab 'AR tools' selected): 1 area(s), 0 hidden: ", 0), 0u)
    << lines[0];

  // One request, one line.
  panel.change();
  ASSERT_TRUE(stream.tick(0.3));
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());

  // A second request before the frame replaces the first reason.
  stream.request_scroll_census("first");
  stream.request_scroll_census("second");
  panel.change();
  ASSERT_TRUE(stream.tick(0.4));
  const std::vector<std::string> both = lines_with(stream.take_notes(), "scroll census");
  ASSERT_EQ(both.size(), 1u);
  EXPECT_NE(both[0].find("(second)"), std::string::npos) << both[0];
}

TEST(StreamCensus, ARequestWithoutAReasonIsLabelledRequested)
{
  TreePanel panel(2);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.take_notes();
  stream.request_scroll_census("");
  panel.change();
  ASSERT_TRUE(stream.tick(0.1));
  const std::vector<std::string> lines = lines_with(stream.take_notes(), "scroll census");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_NE(lines[0].find("scroll census (requested):"), std::string::npos) << lines[0];
}

TEST(StreamCensus, TheRequestedCensusDoesNotDisturbThePairOfTheFirstFrame)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  stream.request_scroll_census("before any frame");
  ASSERT_TRUE(stream.tick(0.0));
  const std::vector<std::string> first = lines_with(stream.take_notes(), "scroll census");
  ASSERT_EQ(first.size(), 2u) << "the first-frame line and the requested one";
  EXPECT_NE(first[0].find("(first frame)"), std::string::npos);
  EXPECT_NE(first[1].find("(before any frame)"), std::string::npos);
  stream.tick(1.5);
  EXPECT_EQ(lines_with(stream.take_notes(), "(+1.5 s)").size(), 1u);
}

TEST(StreamCensus, ARevivedParkedStreamMakesTheSamePairAgain)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.tick(1.5);
  stream.take_notes();
  stream.take_warnings();

  // Parked: the follow-up of a pair that was armed is gone, and nothing is measured meanwhile.
  stream.park("closed");
  for (const double t : {2.0, 3.0, 4.0}) {
    stream.tick(t);
  }
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());

  ASSERT_TRUE(stream.revive_parked(nullptr));
  panel.change();
  ASSERT_TRUE(stream.tick(5.0));
  const std::vector<std::string> again = lines_with(stream.take_notes(), "scroll census");
  ASSERT_EQ(again.size(), 1u);
  EXPECT_NE(again[0].find("(first frame)"), std::string::npos) << again[0];
  stream.take_warnings();

  stream.tick(6.5);
  EXPECT_EQ(lines_with(stream.take_notes(), "scroll census (+1.5 s)").size(), 1u);
  EXPECT_EQ(stream.take_warnings().size(), 1u);
}

TEST(StreamCensus, AParkedStreamDoesNotMeasureAWindowThatIsGoneOrNotShown)
{
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  std::unique_ptr<QtPanelStream> stream;
  {
    TreePanel panel(40);
    stream = std::make_unique<QtPanelStream>("p", panel.get(), options_no_pacing(), factory);
    ASSERT_TRUE(stream->tick(0.0));
    stream->take_notes();
    stream->park("closed");
  }  // the window is destroyed while the stream is parked and a second census was armed
  for (const double t : {1.0, 2.0, 3.0}) {
    stream->tick(t);
  }
  EXPECT_TRUE(stream->take_notes().empty());
  EXPECT_TRUE(stream->take_warnings().empty());
}

TEST(StreamCensus, ANewSenderAfterASizeChangeStartsTheSamePairAgain)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.tick(1.5);
  stream.take_notes();
  stream.take_warnings();
  ASSERT_EQ(stream.diagnostics().adapter_rebuilds, 1u);

  // The widget gets another size and keeps it until the resize hysteresis adopts it.
  panel.get()->resize(300, 320);
  settle();
  panel.tree()->doItemsLayout();
  settle();
  double t = 2.0;
  for (int i = 0; i < 8 && stream.diagnostics().adapter_rebuilds < 2; ++i, t += 0.2) {
    stream.tick(t);
    panel.change();
  }
  ASSERT_EQ(stream.diagnostics().adapter_rebuilds, 2u) << "the new size was adopted";
  const std::vector<std::string> notes = stream.take_notes();
  ASSERT_EQ(lines_with(notes, "(first frame)").size(), 1u) << "the pair starts again";
  stream.tick(t + 1.5);
  EXPECT_EQ(lines_with(stream.take_notes(), "(+1.5 s)").size(), 1u);
}

TEST(StreamCensus, ANoticeIsNotMeasured)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.take_notes();
  stream.tombstone(qar_qt_streaming::PanelTombstoneText{});
  stream.tick(1.6);
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());
  EXPECT_TRUE(stream.take_warnings().empty());
}

TEST(StreamCensus, TheSecondCensusWaitsWhileThePanelIsHiddenInArAndComesWhenItIsShownAgain)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.take_notes();
  stream.take_warnings();

  stream.request_ar_visible(false);
  panel.change();
  EXPECT_FALSE(stream.tick(0.5));
  ASSERT_TRUE(stream.ar_hidden());

  // The 1.5 s pass while nobody can see the panel: nothing is said about it, however long.
  for (const double t : {1.5, 2.0, 5.0, 30.0}) {
    panel.change();
    stream.tick(t);
  }
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());
  EXPECT_TRUE(stream.take_warnings().empty()) << "a Warning never names a hidden panel";

  // Shown again: the census is still due, and it comes with the tick after the one that shows it.
  stream.request_ar_visible(true);
  panel.change();
  stream.tick(31.0);
  ASSERT_FALSE(stream.ar_hidden());
  panel.change();
  stream.tick(31.1);
  EXPECT_EQ(lines_with(stream.take_notes(), "scroll census (+1.5 s)").size(), 1u);
  EXPECT_EQ(stream.take_warnings().size(), 1u);

  // Once.
  for (const double t : {31.5, 33.0, 40.0}) {
    panel.change();
    stream.tick(t);
  }
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());
  EXPECT_TRUE(stream.take_warnings().empty());
}

TEST(StreamCensus, TheSecondCensusWaitsWhileThePanelIsClosedInArToo)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  stream.take_notes();
  stream.take_warnings();

  stream.suspend_capture();  // what the manager does when the runtime reports the panel closed
  for (const double t : {1.5, 2.0, 30.0}) {
    panel.change();
    stream.tick(t);
  }
  EXPECT_TRUE(lines_with(stream.take_notes(), "scroll census").empty());
  EXPECT_TRUE(stream.take_warnings().empty()) << "a Warning never names a panel closed in AR";

  stream.request_ar_visible(true);  // showing also brings back a panel the operator closed
  panel.change();
  stream.tick(31.0);
  ASSERT_FALSE(stream.capture_suspended());
  panel.change();
  stream.tick(31.1);
  EXPECT_EQ(lines_with(stream.take_notes(), "scroll census (+1.5 s)").size(), 1u);
  EXPECT_EQ(stream.take_warnings().size(), 1u);
}

TEST(StreamCensus, ALuminanceLineDoesNotEvictTheCensusLinesBeforeIt)
{
  // One queue for the luminance lines and the census lines, with one bound (24): nine lines are
  // waiting when the line of a long unserved streak is added, and the oldest is still there.
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  double t = 0.0;
  ASSERT_TRUE(stream.tick(t));  // the luminance line of the first frame, the first census
  for (int i = 0; i < 8; ++i) {
    t += 0.01;
    stream.request_scroll_census("request " + std::to_string(i));
    panel.change();
    ASSERT_TRUE(stream.tick(t));
  }

  // QarOS has no destination for twelve frames and then takes one.
  const auto adapter = factory->created().back();
  adapter->set_ready(false);
  for (int i = 0; i < 12; ++i) {
    t += 0.01;
    panel.change();
    EXPECT_FALSE(stream.tick(t));
  }
  adapter->set_ready(true);
  t += 0.01;
  panel.change();
  ASSERT_TRUE(stream.tick(t));

  const std::vector<std::string> notes = stream.take_notes();
  const std::vector<std::string> luminance = lines_with(notes, "frame luminance");
  ASSERT_EQ(luminance.size(), 2u) << "the first frame's line was not pushed out";
  EXPECT_NE(luminance[0].find("(first frame)"), std::string::npos);
  EXPECT_NE(luminance[1].find("(first frame after 12 unserved ticks)"), std::string::npos);
  EXPECT_EQ(notes.size(), 11u) << "two luminance lines, the first census and eight requests";
  EXPECT_EQ(notes.front(), luminance[0]) << "the oldest line is still the first";
}

TEST(StreamCensus, AStreamNobodyDrainsDoesNotGrowWithoutBound)
{
  TreePanel panel(40);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", panel.get(), options_no_pacing(), factory);
  ASSERT_TRUE(stream.tick(0.0));
  double t = 0.1;
  for (int i = 0; i < 200; ++i, t += 0.1) {
    stream.request_scroll_census("again");
    panel.change();
    stream.tick(t);
  }
  EXPECT_LE(stream.take_notes().size(), 24u);
  EXPECT_LE(stream.take_warnings().size(), 24u);
}

// --- through the manager ---------------------------------------------------------------

namespace
{

class RecordingDiagnostics : public qar_ar_core::PanelDiagnosticsSink
{
public:
  struct Entry
  {
    qar_ar_core::DiagnosticSeverity severity;
    std::string category;
    std::string message;
    std::string panel_key;
  };

  void on_message(
    qar_ar_core::DiagnosticSeverity severity, const std::string & category,
    const std::string & message, const std::string & panel_key) override
  {
    messages.push_back(Entry{severity, category, message, panel_key});
  }

  std::vector<Entry> with(const std::string & needle) const
  {
    std::vector<Entry> out;
    for (const Entry & entry : messages) {
      if (entry.message.find(needle) != std::string::npos) {
        out.push_back(entry);
      }
    }
    return out;
  }

  std::vector<Entry> messages;
};

struct ManagerHarness
{
  ManagerHarness()
  {
    factory = std::make_shared<MockQarPanelSenderFactory>();
    clock = std::make_shared<qar_qt_streaming::test_support::ManualClock>();
    qar_qt_streaming::QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.diagnostics = &diagnostics;
    config.dialog_panels = false;
    manager = std::make_unique<qar_qt_streaming::QtPanelStreamManager>(config);
  }

  std::uint32_t tick(double dt)
  {
    clock->advance(dt);
    return manager->tick();
  }

  std::shared_ptr<MockQarPanelSenderFactory> factory;
  std::shared_ptr<qar_qt_streaming::test_support::ManualClock> clock;
  RecordingDiagnostics diagnostics;
  std::unique_ptr<qar_qt_streaming::QtPanelStreamManager> manager;
};

}  // namespace

TEST(ManagerCensus, TheCensusLinesAreInfoLinesOfThePanelCategoryAndTheBarIsAWarning)
{
  ManagerHarness h;
  TreePanel panel(40);
  ASSERT_TRUE(h.manager->add_panel("dock:Views", panel.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(10.0), 1u);

  const auto first = h.diagnostics.with("scroll census (first frame)");
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first[0].severity, qar_ar_core::DiagnosticSeverity::Info);
  EXPECT_EQ(first[0].category, "panel");
  EXPECT_EQ(first[0].panel_key, "dock:Views");
  EXPECT_EQ(first[0].message.rfind("'dock:Views' scroll census (first frame): 1 area(s), ", 0), 0u);
  EXPECT_TRUE(h.diagnostics.with("still shows a vertical scroll bar").empty());

  h.tick(1.0);
  EXPECT_TRUE(h.diagnostics.with("still shows a vertical scroll bar").empty()) << "not yet";
  h.tick(0.5);
  const auto second = h.diagnostics.with("scroll census (+1.5 s)");
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second[0].severity, qar_ar_core::DiagnosticSeverity::Info);
  const auto warning = h.diagnostics.with("still shows a vertical scroll bar");
  ASSERT_EQ(warning.size(), 1u);
  EXPECT_EQ(warning[0].severity, qar_ar_core::DiagnosticSeverity::Warning);
  EXPECT_EQ(warning[0].category, "panel");
  EXPECT_EQ(warning[0].panel_key, "dock:Views");
  EXPECT_EQ(
    warning[0].message.rfind(
      "'dock:Views' still shows a vertical scroll bar 1.5 s after its first frame: QTreeWidget "
      "'tree' overflows by ", 0), 0u)
    << warning[0].message;
}

TEST(ManagerCensus, ARequestNamesTheStreamAndIsRefusedForAKeyThatIsNotStreaming)
{
  ManagerHarness h;
  TreePanel panel(2);
  ASSERT_TRUE(h.manager->add_panel("self", panel.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(1.0), 1u);

  EXPECT_FALSE(h.manager->request_scroll_census("nothing", "tab 'x'"));
  EXPECT_TRUE(h.manager->request_scroll_census("self", "tab 'AR panels' selected"));
  EXPECT_TRUE(h.diagnostics.with("tab 'AR panels' selected").empty()) << "taken at the next frame";
  panel.change();
  ASSERT_EQ(h.tick(0.1), 1u);
  const auto lines = h.diagnostics.with("scroll census (tab 'AR panels' selected)");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].severity, qar_ar_core::DiagnosticSeverity::Info);
  EXPECT_EQ(lines[0].panel_key, "self");
  EXPECT_EQ(
    lines[0].message.rfind(
      "'self' scroll census (tab 'AR panels' selected): 1 area(s), 0 hidden: ", 0),
    0u);
}

TEST(ManagerCensus, AParkedPanelShownAgainIsMeasuredFromItsFirstFrame)
{
  ManagerHarness h;
  h.manager->set_close_mode(qar_qt_streaming::PanelCloseMode::Park);
  TreePanel panel(40);
  ASSERT_TRUE(h.manager->add_panel("dock", panel.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(1.0), 1u);
  h.tick(1.6);
  ASSERT_EQ(h.diagnostics.with("scroll census").size(), 2u);

  ASSERT_TRUE(h.manager->close_panel("dock", "closed").ok());
  h.tick(1.0);
  ASSERT_TRUE(h.manager->is_parked("dock"));
  ASSERT_TRUE(h.manager->add_panel("dock", panel.get(), options_no_pacing()).ok()) << "revived";
  panel.change();
  h.tick(1.0);
  h.tick(0.1);
  const auto again = h.diagnostics.with("scroll census (first frame)");
  ASSERT_EQ(again.size(), 2u) << "the pair starts again with the revival";
  h.tick(1.6);
  EXPECT_EQ(h.diagnostics.with("scroll census (+1.5 s)").size(), 2u);
}

TEST(ManagerCensus, ARequestForAPanelThatIsNotOnShowIsRefusedAndAsksNothing)
{
  ManagerHarness h;
  h.manager->set_close_mode(qar_qt_streaming::PanelCloseMode::Park);
  TreePanel on_show(2);
  TreePanel hidden(2);
  TreePanel closed(2);
  TreePanel parked(2);
  TreePanel notice(2);
  ASSERT_TRUE(h.manager->add_panel("on_show", on_show.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("hidden", hidden.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("closed", closed.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("parked", parked.get(), options_no_pacing()).ok());
  ASSERT_TRUE(h.manager->add_panel("notice", notice.get(), options_no_pacing()).ok());
  ASSERT_EQ(h.tick(1.0), 5u);

  ASSERT_TRUE(h.manager->set_panel_ar_visible("hidden", false).ok());
  ASSERT_TRUE(h.manager->note_ar_panel_closed("closed"));
  ASSERT_TRUE(h.manager->close_panel("parked", "closed").ok());
  ASSERT_TRUE(h.manager->tombstone_panel("notice", "gone").ok());
  h.tick(0.1);
  ASSERT_TRUE(h.manager->is_ar_hidden("hidden"));
  ASSERT_TRUE(h.manager->is_parked("parked"));
  ASSERT_TRUE(h.manager->is_tombstoned("notice"));

  EXPECT_TRUE(h.manager->request_scroll_census("on_show", "tab 'x' selected"));
  for (const char * key : {"hidden", "closed", "parked", "notice"}) {
    EXPECT_FALSE(h.manager->request_scroll_census(key, "tab 'x' selected")) << key;
  }
  on_show.change();
  hidden.change();
  closed.change();
  h.tick(0.1);
  const auto lines = h.diagnostics.with("(tab 'x' selected)");
  ASSERT_EQ(lines.size(), 1u) << "only the panel that is on show took the request";
  EXPECT_EQ(lines[0].panel_key, "on_show");

  // Nothing was kept for the others: back on show, they say nothing under that reason.
  ASSERT_TRUE(h.manager->set_panel_ar_visible("hidden", true).ok());
  ASSERT_TRUE(h.manager->set_panel_ar_visible("closed", true).ok());
  for (int i = 0; i < 3; ++i) {
    hidden.change();
    closed.change();
    h.tick(0.1);
  }
  EXPECT_EQ(h.diagnostics.with("(tab 'x' selected)").size(), 1u);
}
