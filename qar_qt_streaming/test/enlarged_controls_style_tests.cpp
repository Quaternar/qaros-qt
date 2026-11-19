// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// EnlargedControlsStyle and the spreader: the controls of a widget tree are drawn, laid out and
// hit a factor larger while the text keeps its size. Every assertion compares a tree that uses the
// style with a twin that uses a plain instance of the same base style, so no test depends on an
// absolute pixel number (the font of the test machine may differ). The widgets are shown with
// WA_DontShowOnScreen under the offscreen platform.

#include <gtest/gtest.h>

#include "qar_qt_streaming/enlarged_controls_style.hpp"
#include "qar_qt_streaming/qt_compat.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndex>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QRegion>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace qt_compat = qar_qt_streaming::qt_compat;

using qar_qt_streaming::apply_style_to_tree;
using qar_qt_streaming::EnlargedControlsStyle;
using qar_qt_streaming::enlarged_controls_factor;
using qar_qt_streaming::kEnlargedControlsDefaultFactor;
using qar_qt_streaming::kEnlargedControlsMaxFactor;
using qar_qt_streaming::kEnlargedControlsMinFactor;
using qar_qt_streaming::remove_style_from_tree;

namespace
{

/// The editor an item delegate makes on demand: a spin box without a frame, as the property
/// tree of RViz makes for an integer.
class SpinDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QWidget * createEditor(
    QWidget * parent, const QStyleOptionViewItem &, const QModelIndex &) const override
  {
    auto * editor = new QSpinBox(parent);
    editor->setFrame(false);
    editor->setRange(1, 1000);
    return editor;
  }
};

/// A panel of the controls the style enlarges, built the same way every time.
struct Panel
{
  std::unique_ptr<QWidget> root;
  QCheckBox * check = nullptr;
  QLabel * label = nullptr;
  QSpinBox * spin = nullptr;
  QDoubleSpinBox * dspin = nullptr;
  QSlider * slider = nullptr;
  QScrollBar * bar = nullptr;
  QComboBox * combo = nullptr;
  QPushButton * button = nullptr;
  QTreeView * tree = nullptr;
  QStandardItemModel * model = nullptr;
};

Panel
make_panel()
{
  Panel p;
  p.root = std::make_unique<QWidget>();
  auto * column = new QVBoxLayout(p.root.get());
  p.check = new QCheckBox(QStringLiteral("Default Light"));
  column->addWidget(p.check);
  auto * row = new QHBoxLayout;
  column->addLayout(row);
  p.label = new QLabel(QStringLiteral("Frame Rate"));
  row->addWidget(p.label);
  p.spin = new QSpinBox;
  p.spin->setRange(1, 1000);
  p.spin->setValue(30);
  row->addWidget(p.spin);
  p.dspin = new QDoubleSpinBox;
  p.dspin->setValue(1.5);
  row->addWidget(p.dspin);
  p.slider = new QSlider(Qt::Horizontal);
  p.slider->setRange(0, 100);
  p.slider->setValue(40);
  column->addWidget(p.slider);
  p.bar = new QScrollBar(Qt::Vertical);
  p.bar->setRange(0, 100);
  p.bar->setPageStep(20);
  p.combo = new QComboBox;
  p.combo->addItems({QStringLiteral("map"), QStringLiteral("odom"), QStringLiteral("base_link")});
  column->addWidget(p.combo);
  p.button = new QPushButton(QStringLiteral("Add"));
  column->addWidget(p.button);

  p.tree = new QTreeView;
  p.tree->setUniformRowHeights(true);
  p.tree->setHeaderHidden(true);
  p.tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
  p.model = new QStandardItemModel(0, 2, p.tree);
  auto * global = new QStandardItem(QStringLiteral("Global Options"));
  auto * rate = new QStandardItem(QStringLiteral("Frame Rate"));
  auto * rate_value = new QStandardItem;
  rate_value->setData(30, Qt::EditRole);
  global->appendRow({rate, rate_value});
  auto * light = new QStandardItem(QStringLiteral("Default Light"));
  light->setCheckable(true);
  light->setCheckState(Qt::Checked);
  global->appendRow({light, new QStandardItem});
  p.model->appendRow({global, new QStandardItem});
  for (int i = 0; i < 4; ++i) {
    auto * display = new QStandardItem(QStringLiteral("Display %1").arg(i));
    display->setCheckable(true);
    display->setCheckState(Qt::Checked);
    display->appendRow({new QStandardItem(QStringLiteral("Topic")), new QStandardItem});
    p.model->appendRow({display, new QStandardItem});
  }
  p.tree->setModel(p.model);
  p.tree->setItemDelegateForColumn(1, new SpinDelegate(p.tree));
  p.tree->expand(p.model->index(0, 0));
  p.tree->setColumnWidth(0, 200);
  auto * lower = new QHBoxLayout;
  lower->addWidget(p.tree, 1);
  lower->addWidget(p.bar);
  column->addLayout(lower, 1);
  p.root->resize(420, 540);
  return p;
}

/// A panel and the style it uses. The style is declared first, so it outlives the widgets.
/// A factor of 0 or less gives the plain base style (the twin the enlarged tree is compared to).
class Rig
{
public:
  explicit Rig(double factor = 0.0)
  {
    if (factor > 0.0) {
      style_ = std::make_unique<EnlargedControlsStyle>(factor);
    } else {
      style_.reset(QStyleFactory::create(QStringLiteral("fusion")));
    }
    panel_ = make_panel();
    apply_style_to_tree(panel_.root.get(), style_.get());
    panel_.root->setAttribute(Qt::WA_DontShowOnScreen);
    panel_.root->show();
    QCoreApplication::processEvents();
  }

  Panel & panel() {return panel_;}
  QStyle * style() const {return style_.get();}

private:
  std::unique_ptr<QStyle> style_;
  Panel panel_;
};

QStyleOptionSpinBox
spin_option(QAbstractSpinBox * spin, bool frame)
{
  QStyleOptionSpinBox option;
  option.initFrom(spin);
  option.rect = spin->rect();
  option.frame = frame;
  option.subControls = QStyle::SC_All;
  option.buttonSymbols = QAbstractSpinBox::UpDownArrows;
  option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
  return option;
}

QRect
spin_part(QAbstractSpinBox * spin, QStyle::SubControl part, bool frame = true)
{
  const QStyleOptionSpinBox option = spin_option(spin, frame);
  return spin->style()->subControlRect(QStyle::CC_SpinBox, &option, part, spin);
}

QStyleOptionSlider
slider_option(QAbstractSlider * slider)
{
  QStyleOptionSlider option;
  option.initFrom(slider);
  option.orientation = slider->orientation();
  option.minimum = slider->minimum();
  option.maximum = slider->maximum();
  option.sliderPosition = slider->sliderPosition();
  option.sliderValue = slider->value();
  option.pageStep = slider->pageStep();
  option.rect = slider->rect();
  option.subControls = QStyle::SC_All;
  return option;
}

void
click(QWidget * widget, const QPoint & at)
{
  qt_compat::InputDevices devices;
  const QPoint global = widget->mapToGlobal(at);
  auto press = qt_compat::make_mouse_event(
    devices, QEvent::MouseButtonPress, at, at, global, Qt::LeftButton, Qt::LeftButton);
  QCoreApplication::sendEvent(widget, press.get());
  auto release = qt_compat::make_mouse_event(
    devices, QEvent::MouseButtonRelease, at, at, global, Qt::LeftButton, Qt::NoButton);
  QCoreApplication::sendEvent(widget, release.get());
}

/// The rectangle of the check box of the first row below the root row of the tree, in the
/// coordinates of the viewport.
QRect
tree_check_rect(QTreeView * tree, const QModelIndex & index)
{
  QStyleOptionViewItem option;
  option.initFrom(tree->viewport());
  option.rect = tree->visualRect(index);
  option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
  option.checkState = Qt::Checked;
  option.font = tree->font();
  option.fontMetrics = QFontMetrics(option.font);
  return tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, tree);
}

/// The palette the pixel tests use: a white face and black marks, so that a
/// dark pixel is a mark and nothing else.
QPalette
mark_palette()
{
  QPalette palette;
  for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    palette.setColor(group, QPalette::Window, Qt::white);
    palette.setColor(group, QPalette::Button, Qt::white);
    palette.setColor(group, QPalette::Base, Qt::white);
    palette.setColor(group, QPalette::WindowText, Qt::black);
    palette.setColor(group, QPalette::ButtonText, Qt::black);
    palette.setColor(group, QPalette::Text, Qt::black);
  }
  return palette;
}

/// The number of pixel columns of @p area in which some pixel is darker than @p limit.
int
dark_columns(const QImage & image, const QRect & area, int limit)
{
  int columns = 0;
  for (int x = area.left(); x <= area.right(); ++x) {
    for (int y = area.top(); y <= area.bottom(); ++y) {
      if (qGray(image.pixel(x, y)) < limit) {
        ++columns;
        break;
      }
    }
  }
  return columns;
}

int
dark_pixels(const QImage & image, const QRect & area, int limit)
{
  int count = 0;
  for (int x = area.left(); x <= area.right(); ++x) {
    for (int y = area.top(); y <= area.bottom(); ++y) {
      if (qGray(image.pixel(x, y)) < limit) {
        ++count;
      }
    }
  }
  return count;
}

}  // namespace

TEST(EnlargedControls, TheIndicatorIsTheFactorTimesTheBase)
{
  Rig plain;
  Rig twice(2.0);
  Rig one_and_a_half(1.5);
  QStyleOptionButton option;
  option.initFrom(plain.panel().check);
  const int base = plain.panel().check->style()->pixelMetric(
    QStyle::PM_IndicatorWidth, &option, plain.panel().check);
  ASSERT_GT(base, 0);
  EXPECT_EQ(
    twice.panel().check->style()->pixelMetric(
      QStyle::PM_IndicatorWidth, &option, twice.panel().check),
    2 * base);
  EXPECT_EQ(
    twice.panel().check->style()->pixelMetric(
      QStyle::PM_IndicatorHeight, &option, twice.panel().check),
    2 * base);
  EXPECT_EQ(
    one_and_a_half.panel().check->style()->pixelMetric(
      QStyle::PM_IndicatorWidth, &option, one_and_a_half.panel().check),
    static_cast<int>(std::lround(base * 1.5)));

  const QRect plain_box = plain.panel().check->style()->subElementRect(
    QStyle::SE_CheckBoxIndicator, &option, plain.panel().check);
  const QRect twice_box = twice.panel().check->style()->subElementRect(
    QStyle::SE_CheckBoxIndicator, &option, twice.panel().check);
  EXPECT_EQ(twice_box.width(), 2 * plain_box.width());
  EXPECT_EQ(twice_box.height(), 2 * plain_box.height());
}

TEST(EnlargedControls, TheFontAndALabelKeepTheirSize)
{
  Rig plain;
  Rig twice(2.0);
  Panel & a = plain.panel();
  Panel & b = twice.panel();
  EXPECT_EQ(a.spin->font(), b.spin->font());
  EXPECT_EQ(
    QFontMetrics(a.spin->font()).height(), QFontMetrics(b.spin->font()).height());
  EXPECT_EQ(
    QFontMetrics(a.check->font()).height(), QFontMetrics(b.check->font()).height());
  EXPECT_EQ(a.label->sizeHint(), b.label->sizeHint());
  // The check box keeps the width of its text: only the indicator grows.
  QStyleOptionButton option;
  option.initFrom(a.check);
  const int grown = b.check->sizeHint().width() - a.check->sizeHint().width();
  const int indicator_growth = a.check->style()->pixelMetric(
    QStyle::PM_IndicatorWidth, &option, a.check);
  EXPECT_NEAR(grown, indicator_growth, 1);
}

TEST(EnlargedControls, TheSpinButtonsAreTwiceAsWideAndTheEditFieldKeepsTheText)
{
  Rig plain;
  Rig twice(2.0);
  QSpinBox * a = plain.panel().spin;
  QSpinBox * b = twice.panel().spin;
  const QRect plain_up = spin_part(a, QStyle::SC_SpinBoxUp);
  const QRect plain_down = spin_part(a, QStyle::SC_SpinBoxDown);
  const QRect up = spin_part(b, QStyle::SC_SpinBoxUp);
  const QRect down = spin_part(b, QStyle::SC_SpinBoxDown);
  ASSERT_GT(plain_up.width(), 0);
  EXPECT_NEAR(up.width(), 2 * plain_up.width(), 1);
  EXPECT_NEAR(down.width(), 2 * plain_down.width(), 1);
  EXPECT_GT(up.height(), plain_up.height());
  const QRect edit = spin_part(b, QStyle::SC_SpinBoxEditField);
  EXPECT_GE(edit.height(), QFontMetrics(b->font()).height());
  // The buttons are inside the widget and do not cover the edit field.
  EXPECT_TRUE(b->rect().contains(up));
  EXPECT_TRUE(b->rect().contains(down));
  EXPECT_FALSE(up.intersects(edit));
  EXPECT_FALSE(down.intersects(edit));
  EXPECT_FALSE(up.intersects(down));
}

TEST(EnlargedControls, TheHitTestAgreesWithTheSubControlRects)
{
  Rig twice(2.0);
  QSpinBox * spin = twice.panel().spin;
  const QStyleOptionSpinBox option = spin_option(spin, true);
  QStyle * style = spin->style();
  const QRect up = style->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin);
  const QRect down =
    style->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, spin);
  EXPECT_EQ(
    style->hitTestComplexControl(QStyle::CC_SpinBox, &option, up.center(), spin),
    QStyle::SC_SpinBoxUp);
  EXPECT_EQ(
    style->hitTestComplexControl(QStyle::CC_SpinBox, &option, down.center(), spin),
    QStyle::SC_SpinBoxDown);
  // Two pixels inside a corner of each button still hit it.
  EXPECT_EQ(
    style->hitTestComplexControl(
      QStyle::CC_SpinBox, &option, QPoint(up.left() + 2, up.top() + 2), spin),
    QStyle::SC_SpinBoxUp);
  EXPECT_EQ(
    style->hitTestComplexControl(
      QStyle::CC_SpinBox, &option, QPoint(down.left() + 2, down.bottom() - 2), spin),
    QStyle::SC_SpinBoxDown);
  // The middle of the edit field is neither button.
  const QRect edit =
    style->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, spin);
  const QStyle::SubControl in_edit =
    style->hitTestComplexControl(QStyle::CC_SpinBox, &option, edit.center(), spin);
  EXPECT_NE(in_edit, QStyle::SC_SpinBoxUp);
  EXPECT_NE(in_edit, QStyle::SC_SpinBoxDown);
}

TEST(EnlargedControls, AClickAtTheUpRectStepsUpAndAtTheDownRectStepsDown)
{
  Rig twice(2.0);
  QSpinBox * spin = twice.panel().spin;
  const QRect up = spin_part(spin, QStyle::SC_SpinBoxUp);
  const QRect down = spin_part(spin, QStyle::SC_SpinBoxDown);
  const int start = spin->value();
  click(spin, up.center());
  EXPECT_EQ(spin->value(), start + 1);
  click(spin, down.center());
  click(spin, down.center());
  EXPECT_EQ(spin->value(), start - 1);
  // The corner 2 px inside the up button steps as well.
  click(spin, QPoint(up.left() + 2, up.top() + 2));
  EXPECT_EQ(spin->value(), start);
}

TEST(EnlargedControls, AClickJustLeftOfTheUpRectDoesNotStep)
{
  Rig twice(2.0);
  QSpinBox * spin = twice.panel().spin;
  const QRect up = spin_part(spin, QStyle::SC_SpinBoxUp);
  const int start = spin->value();
  click(spin, QPoint(up.left() - 2, up.center().y()));
  EXPECT_EQ(spin->value(), start);
}

TEST(EnlargedControls, ADelegateEditorMadeLaterGetsTheStyle)
{
  Rig plain;
  Rig twice(2.0);
  Panel & a = plain.panel();
  Panel & b = twice.panel();
  const QModelIndex a_value = a.model->index(0, 1, a.model->index(0, 0));
  const QModelIndex b_value = b.model->index(0, 1, b.model->index(0, 0));
  a.tree->openPersistentEditor(a_value);
  b.tree->openPersistentEditor(b_value);
  QCoreApplication::processEvents();
  auto * plain_editor = a.tree->viewport()->findChild<QSpinBox *>();
  auto * editor = b.tree->viewport()->findChild<QSpinBox *>();
  ASSERT_NE(plain_editor, nullptr);
  ASSERT_NE(editor, nullptr);
  EXPECT_EQ(editor->style(), twice.style());
  ASSERT_NE(editor->findChild<QLineEdit *>(), nullptr);
  EXPECT_EQ(editor->findChild<QLineEdit *>()->style(), twice.style());
  EXPECT_NE(plain_editor->style(), twice.style());

  const QRect plain_up = spin_part(plain_editor, QStyle::SC_SpinBoxUp, false);
  const QRect up = spin_part(editor, QStyle::SC_SpinBoxUp, false);
  ASSERT_GT(plain_up.width(), 0);
  // The base style puts the button of a frameless spin box a pixel or two past the widget; what
  // lies outside the widget is cut, so the button is that much narrower than twice the plain one.
  EXPECT_NEAR(up.width(), 2 * plain_up.width(), 6);
  EXPECT_GE(up.width(), plain_up.width());
  EXPECT_TRUE(editor->rect().contains(up));
  EXPECT_GT(up.height(), plain_up.height());

  // A press at the middle of its up button steps the value of the editor.
  const int start = editor->value();
  click(editor, up.center());
  EXPECT_EQ(editor->value(), start + 1);
}

TEST(EnlargedControls, AWidgetMadeLaterBelowTheRootGetsTheStyle)
{
  Rig twice(2.0);
  Panel & p = twice.panel();
  auto * late = new QCheckBox(QStringLiteral("late"), p.tree->viewport());
  EXPECT_EQ(late->style(), twice.style());
  auto * moved = new QCheckBox(QStringLiteral("moved"));
  EXPECT_NE(moved->style(), twice.style());
  moved->setParent(p.root.get());
  EXPECT_EQ(moved->style(), twice.style());
}

TEST(EnlargedControls, TreeRowsAreTwiceThePlainRow)
{
  Rig plain;
  Rig twice(2.0);
  const QModelIndex a = plain.panel().model->index(1, 0);
  const QModelIndex b = twice.panel().model->index(1, 0);
  const int plain_row = plain.panel().tree->visualRect(a).height();
  const int row = twice.panel().tree->visualRect(b).height();
  ASSERT_GT(plain_row, 0);
  // The uniform row height is the one of the first row: twice the plain row (the plain row
  // of this font; a first row that holds a check box would be taller).
  EXPECT_NEAR(row, 2 * plain_row, 1);
  EXPECT_EQ(
    twice.panel().tree->indentation(), 2 * plain.panel().tree->indentation());
}

TEST(EnlargedControls, TheTreeCheckIsHitAtItsCorner)
{
  Rig twice(2.0);
  Rig plain;
  Panel & p = twice.panel();
  const QModelIndex index = p.model->index(1, 0);
  const QRect box = tree_check_rect(p.tree, index);
  const QRect plain_box = tree_check_rect(plain.panel().tree, plain.panel().model->index(1, 0));
  EXPECT_EQ(box.width(), 2 * plain_box.width());
  EXPECT_EQ(box.height(), 2 * plain_box.height());
  QStandardItem * item = p.model->itemFromIndex(index);
  ASSERT_NE(item, nullptr);
  ASSERT_EQ(item->checkState(), Qt::Checked);
  click(p.tree->viewport(), box.center());
  EXPECT_EQ(item->checkState(), Qt::Unchecked);
  // One pixel inside the bottom right corner of the part of the box that lies in the row (a
  // small font makes the uniform row lower than the box).
  const QRect target = box.intersected(p.tree->visualRect(index));
  click(p.tree->viewport(), QPoint(target.right() - 1, target.bottom() - 1));
  EXPECT_EQ(item->checkState(), Qt::Checked);
}

TEST(EnlargedControls, TheTreeArrowIsHitInTheWiderIndentation)
{
  Rig twice(2.0);
  Rig plain;
  Panel & p = twice.panel();
  ASSERT_EQ(p.tree->indentation(), 2 * plain.panel().tree->indentation());
  const QModelIndex display = p.model->index(2, 0);
  ASSERT_FALSE(p.tree->isExpanded(display));
  const QRect row = p.tree->visualRect(display);
  // The arrow cell is the indentation to the left of the row; its middle expands the row.
  click(p.tree->viewport(), QPoint(row.left() - p.tree->indentation() / 2, row.center().y()));
  EXPECT_TRUE(p.tree->isExpanded(display));
}

TEST(EnlargedControls, SliderHandleScrollBarAndComboArrowAreTwiceThePlain)
{
  Rig plain;
  Rig twice(2.0);
  Panel & a = plain.panel();
  Panel & b = twice.panel();

  const QStyleOptionSlider plain_slider = slider_option(a.slider);
  const QStyleOptionSlider slider = slider_option(b.slider);
  const QRect plain_handle = a.slider->style()->subControlRect(
    QStyle::CC_Slider, &plain_slider, QStyle::SC_SliderHandle, a.slider);
  const QRect handle = b.slider->style()->subControlRect(
    QStyle::CC_Slider, &slider, QStyle::SC_SliderHandle, b.slider);
  ASSERT_GT(plain_handle.width(), 0);
  EXPECT_NEAR(handle.width(), 2 * plain_handle.width(), 1);
  EXPECT_NEAR(handle.height(), 2 * plain_handle.height(), 1);
  EXPECT_TRUE(b.slider->rect().contains(handle));
  // The handle is hit at its middle.
  EXPECT_EQ(
    b.slider->style()->hitTestComplexControl(
      QStyle::CC_Slider, &slider, handle.center(), b.slider),
    QStyle::SC_SliderHandle);

  const int plain_extent =
    a.bar->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, a.bar);
  const int extent = b.bar->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, b.bar);
  ASSERT_GT(plain_extent, 0);
  EXPECT_EQ(extent, 2 * plain_extent);
  EXPECT_EQ(b.bar->sizeHint().width(), 2 * a.bar->sizeHint().width());
  const QStyleOptionSlider bar_option = slider_option(b.bar);
  const QRect bar_handle = b.bar->style()->subControlRect(
    QStyle::CC_ScrollBar, &bar_option, QStyle::SC_ScrollBarSlider, b.bar);
  EXPECT_EQ(
    b.bar->style()->hitTestComplexControl(
      QStyle::CC_ScrollBar, &bar_option, bar_handle.center(), b.bar),
    QStyle::SC_ScrollBarSlider);

  QStyleOptionComboBox plain_combo;
  plain_combo.initFrom(a.combo);
  plain_combo.rect = a.combo->rect();
  plain_combo.subControls = QStyle::SC_All;
  plain_combo.frame = true;
  QStyleOptionComboBox combo = plain_combo;
  combo.initFrom(b.combo);
  combo.rect = b.combo->rect();
  const QRect plain_arrow = a.combo->style()->subControlRect(
    QStyle::CC_ComboBox, &plain_combo, QStyle::SC_ComboBoxArrow, a.combo);
  const QRect arrow = b.combo->style()->subControlRect(
    QStyle::CC_ComboBox, &combo, QStyle::SC_ComboBoxArrow, b.combo);
  ASSERT_GT(plain_arrow.width(), 0);
  EXPECT_NEAR(arrow.width(), 2 * plain_arrow.width(), 1);
  EXPECT_EQ(
    b.combo->style()->hitTestComplexControl(
      QStyle::CC_ComboBox, &combo, arrow.center(), b.combo),
    QStyle::SC_ComboBoxArrow);
}

TEST(EnlargedControls, ThePushButtonIsTallerAndNotWider)
{
  Rig plain;
  Rig twice(2.0);
  const QSize a = plain.panel().button->sizeHint();
  const QSize b = twice.panel().button->sizeHint();
  EXPECT_GT(b.height(), a.height());
  EXPECT_LE(b.width(), a.width() + 2 * plain.panel().button->style()->pixelMetric(
      QStyle::PM_ButtonMargin, nullptr, plain.panel().button));
}

TEST(EnlargedControls, TheDrawnSpinArrowIsTwiceAsWide)
{
  Rig plain;
  Rig twice(2.0);
  QSpinBox * a = plain.panel().spin;
  QSpinBox * b = twice.panel().spin;
  a->setPalette(mark_palette());
  b->setPalette(mark_palette());
  QCoreApplication::processEvents();
  const QImage image_a = a->grab().toImage();
  const QImage image_b = b->grab().toImage();
  const QRect up_a = spin_part(a, QStyle::SC_SpinBoxUp);
  const QRect up_b = spin_part(b, QStyle::SC_SpinBoxUp);
  // The border of the widget is a light grey; an arrow is darker than 160.
  const QRect inner_a = up_a.adjusted(2, 2, -2, -2);
  const QRect inner_b = up_b.adjusted(2, 2, -2, -2);
  const int columns_a = dark_columns(image_a, inner_a, 160);
  const int columns_b = dark_columns(image_b, inner_b, 160);
  ASSERT_GT(columns_a, 0) << "no arrow found in the plain spin box";
  // The arrow is drawn at the final size, so its antialiased edge adds up to a column on each side
  // to the count of columns that hold a dark pixel.
  EXPECT_NEAR(columns_b, 2 * columns_a, 2);
}

TEST(EnlargedControls, TheDrawnCheckMarkFillsTwiceTheArea)
{
  Rig plain;
  Rig twice(2.0);
  QCheckBox * a = plain.panel().check;
  QCheckBox * b = twice.panel().check;
  for (QCheckBox * box : {a, b}) {
    box->setText(QString());
    box->setChecked(true);
    box->setPalette(mark_palette());
  }
  QCoreApplication::processEvents();
  const QImage image_a = a->grab().toImage();
  const QImage image_b = b->grab().toImage();
  QStyleOptionButton option;
  option.initFrom(a);
  const QRect box_a = a->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, a);
  option.initFrom(b);
  const QRect box_b = b->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, b);
  const int marks_a = dark_pixels(image_a, box_a.adjusted(2, 2, -2, -2), 120);
  const int marks_b = dark_pixels(image_b, box_b.adjusted(3, 3, -3, -3), 120);
  ASSERT_GT(marks_a, 0) << "no check mark found in the plain box";
  const double ratio = static_cast<double>(marks_b) / marks_a;
  EXPECT_GT(ratio, 3.0);
  EXPECT_LT(ratio, 5.0);
}

TEST(EnlargedControls, RemovingTheStyleGivesTheApplicationStyleBack)
{
  Rig plain;
  Rig twice(2.0);
  Panel & a = plain.panel();
  Panel & b = twice.panel();
  const QSize before = b.spin->sizeHint();
  EXPECT_NE(before, a.spin->sizeHint());
  const int changed = remove_style_from_tree(b.root.get());
  EXPECT_GT(changed, 0);
  EXPECT_NE(b.spin->style(), twice.style());
  EXPECT_NE(b.check->style(), twice.style());
  EXPECT_EQ(b.spin->style(), QApplication::style());
  QCoreApplication::processEvents();
  // The size is the one of the application's style again (the plain twin is Fusion; the
  // application style may be another, so compare with a widget made now).
  QSpinBox fresh;
  fresh.setRange(1, 1000);
  fresh.setValue(30);
  EXPECT_EQ(b.spin->sizeHint(), fresh.sizeHint());
  // A second removal finds no spreader.
  EXPECT_EQ(remove_style_from_tree(b.root.get()), 0);
  // A widget made after the removal is left alone.
  auto * late = new QCheckBox(QStringLiteral("late"), b.root.get());
  EXPECT_NE(late->style(), twice.style());
}

TEST(EnlargedControls, AStyleSheetOnTheRootKeepsTheMetrics)
{
  Rig plain;
  Rig twice(2.0);
  Panel & a = plain.panel();
  Panel & b = twice.panel();
  b.root->setStyleSheet(QStringLiteral("QLabel { color: #224488; }"));
  QCoreApplication::processEvents();
  QStyleOptionButton option;
  option.initFrom(a.check);
  const int base = a.check->style()->pixelMetric(QStyle::PM_IndicatorWidth, &option, a.check);
  EXPECT_EQ(b.check->style()->pixelMetric(QStyle::PM_IndicatorWidth, &option, b.check), 2 * base);
}

TEST(EnlargedControls, TheFactorIsClampedAndAnUnknownBaseIsFusion)
{
  EXPECT_DOUBLE_EQ(enlarged_controls_factor(2.5), 2.5);
  EXPECT_DOUBLE_EQ(enlarged_controls_factor(0.2), kEnlargedControlsMinFactor);
  EXPECT_DOUBLE_EQ(enlarged_controls_factor(-3.0), kEnlargedControlsMinFactor);
  EXPECT_DOUBLE_EQ(enlarged_controls_factor(9.0), kEnlargedControlsMaxFactor);
  EXPECT_DOUBLE_EQ(enlarged_controls_factor(std::nan("")), kEnlargedControlsDefaultFactor);
  EXPECT_DOUBLE_EQ(
    enlarged_controls_factor(std::numeric_limits<double>::infinity()),
    kEnlargedControlsDefaultFactor);

  EnlargedControlsStyle defaults;
  EXPECT_DOUBLE_EQ(defaults.factor(), kEnlargedControlsDefaultFactor);
  EXPECT_EQ(defaults.base_key(), QStringLiteral("fusion"));
  EnlargedControlsStyle clamped(9.0);
  EXPECT_DOUBLE_EQ(clamped.factor(), kEnlargedControlsMaxFactor);
  EnlargedControlsStyle unknown(2.0, QStringLiteral("no-such-style"));
  EXPECT_EQ(unknown.base_key(), QStringLiteral("fusion"));
  EnlargedControlsStyle empty(2.0, QString());
  EXPECT_EQ(empty.base_key(), QStringLiteral("fusion"));
  EnlargedControlsStyle upper(2.0, QStringLiteral("FUSION"));
  EXPECT_EQ(upper.base_key(), QStringLiteral("fusion"));
  // An unknown base still draws and measures like Fusion at the factor.
  QStyleOptionButton option;
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  EXPECT_EQ(
    unknown.pixelMetric(QStyle::PM_IndicatorWidth, &option),
    2 * fusion->pixelMetric(QStyle::PM_IndicatorWidth, &option));
}

TEST(EnlargedControls, OnlyTheListedMetricsPrimitivesAndComplexControlsAreScaled)
{
  EXPECT_TRUE(EnlargedControlsStyle::scales_metric(QStyle::PM_IndicatorWidth));
  EXPECT_TRUE(EnlargedControlsStyle::scales_metric(QStyle::PM_TreeViewIndentation));
  EXPECT_FALSE(EnlargedControlsStyle::scales_metric(QStyle::PM_SmallIconSize));
  EXPECT_FALSE(EnlargedControlsStyle::scales_metric(QStyle::PM_ButtonIconSize));
  EXPECT_FALSE(EnlargedControlsStyle::scales_metric(QStyle::PM_ListViewIconSize));
  EXPECT_FALSE(EnlargedControlsStyle::scales_metric(QStyle::PM_DefaultFrameWidth));
  EXPECT_TRUE(EnlargedControlsStyle::scales_primitive(QStyle::PE_IndicatorCheckBox));
  EXPECT_TRUE(EnlargedControlsStyle::scales_primitive(QStyle::PE_IndicatorBranch));
  EXPECT_FALSE(EnlargedControlsStyle::scales_primitive(QStyle::PE_Frame));
  EXPECT_FALSE(EnlargedControlsStyle::scales_primitive(QStyle::PE_PanelButtonCommand));
  EXPECT_TRUE(EnlargedControlsStyle::scales_complex_control(QStyle::CC_SpinBox));
  EXPECT_TRUE(EnlargedControlsStyle::scales_complex_control(QStyle::CC_Slider));
  EXPECT_FALSE(EnlargedControlsStyle::scales_complex_control(QStyle::CC_ToolButton));
  EXPECT_FALSE(EnlargedControlsStyle::scales_complex_control(QStyle::CC_GroupBox));
}

TEST(EnlargedControls, TheSpreaderCountsTheWidgetsAndTakesANullArgumentAsNothing)
{
  EnlargedControlsStyle style;
  QWidget root;
  auto * layout = new QVBoxLayout(&root);
  layout->addWidget(new QCheckBox(QStringLiteral("a")));
  layout->addWidget(new QSpinBox);
  EXPECT_EQ(apply_style_to_tree(nullptr, &style), 0);
  EXPECT_EQ(apply_style_to_tree(&root, nullptr), 0);
  EXPECT_EQ(remove_style_from_tree(nullptr), 0);
  // The root, the check box, the spin box and the line edit inside the spin box.
  EXPECT_EQ(apply_style_to_tree(&root, &style), 4);
  EXPECT_EQ(root.style(), &style);
  // A second call replaces the spreader and gives the style to the same widgets again.
  EXPECT_EQ(apply_style_to_tree(&root, &style), 4);
  EXPECT_EQ(root.findChildren<QObject *>(
      QString::fromLatin1(qar_qt_streaming::kStyleSpreaderObjectName)).size(), 1);
  EXPECT_EQ(remove_style_from_tree(&root), 4);
  EXPECT_EQ(root.findChildren<QObject *>(
      QString::fromLatin1(qar_qt_streaming::kStyleSpreaderObjectName)).size(), 0);
}

namespace
{

/// True when every pixel of @p image outside the rectangle @p inside is still white.
bool
nothing_drawn_outside(const QImage & image, const QRect & inside)
{
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (!inside.contains(x, y) && image.pixel(x, y) != QColor(Qt::white).rgb()) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

TEST(EnlargedControls, ASubControlStaysInsideTheWidgetAtAFractionalFactor)
{
  for (const double factor : {1.25, 1.5, 1.75}) {
    Rig rig(factor);
    QSpinBox * spin = rig.panel().spin;
    QComboBox * combo = rig.panel().combo;
    QSlider * slider = rig.panel().slider;
    QStyle * style = spin->style();
    for (int width = 41; width <= 97; ++width) {
      for (int height : {19, 22, 25, 31}) {
        const QRect rect(7, 5, width, height);
        QStyleOptionSpinBox spin_opt = spin_option(spin, true);
        spin_opt.rect = rect;
        for (const auto part :
          {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown, QStyle::SC_SpinBoxEditField,
            QStyle::SC_SpinBoxFrame})
        {
          const QRect sub = style->subControlRect(QStyle::CC_SpinBox, &spin_opt, part, spin);
          EXPECT_TRUE(rect.contains(sub))
            << "spin part " << part << " " << width << "x" << height << " at " << factor;
        }
        QStyleOptionComboBox combo_opt;
        combo_opt.initFrom(combo);
        combo_opt.rect = rect;
        combo_opt.subControls = QStyle::SC_All;
        combo_opt.frame = true;
        for (const auto part : {QStyle::SC_ComboBoxArrow, QStyle::SC_ComboBoxEditField,
            QStyle::SC_ComboBoxFrame})
        {
          const QRect sub = style->subControlRect(QStyle::CC_ComboBox, &combo_opt, part, combo);
          EXPECT_TRUE(rect.contains(sub))
            << "combo part " << part << " " << width << "x" << height << " at " << factor;
        }
        QStyleOptionSlider slider_opt = slider_option(slider);
        slider_opt.rect = rect;
        for (const auto part : {QStyle::SC_SliderHandle, QStyle::SC_SliderGroove}) {
          const QRect sub = style->subControlRect(QStyle::CC_Slider, &slider_opt, part, slider);
          EXPECT_TRUE(rect.contains(sub))
            << "slider part " << part << " " << width << "x" << height << " at " << factor;
        }
      }
    }
  }
}

TEST(EnlargedControls, NothingIsDrawnOutsideTheRectangleAtAFractionalFactor)
{
  for (const double factor : {1.5, 1.25, 2.0}) {
    EnlargedControlsStyle style(factor);
    QCheckBox widget;
    // The marks are black on white, so any changed pixel is a drawn one.
    const QPalette palette = mark_palette();
    for (const int size : {17, 20, 21, 23}) {
      QImage image(80, 80, QImage::Format_ARGB32);
      image.fill(Qt::white);
      const QRect rect(30, 30, size, size);
      {
        QPainter painter(&image);
        QStyleOptionButton option;
        option.rect = rect;
        option.palette = palette;
        option.state = QStyle::State_Enabled | QStyle::State_On;
        style.drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, &painter, &widget);
        option.state = QStyle::State_Enabled;
        style.drawPrimitive(QStyle::PE_IndicatorRadioButton, &option, &painter, &widget);
      }
      EXPECT_TRUE(nothing_drawn_outside(image, rect))
        << "indicator " << size << " at " << factor;
    }
    QSpinBox spin;
    for (const int width : {61, 62, 63, 77}) {
      QImage image(120, 80, QImage::Format_ARGB32);
      image.fill(Qt::white);
      const QRect rect(20, 20, width, 23);
      {
        QPainter painter(&image);
        QStyleOptionSpinBox option;
        option.rect = rect;
        option.palette = palette;
        option.state = QStyle::State_Enabled;
        option.frame = true;
        option.subControls = QStyle::SC_All;
        option.buttonSymbols = QAbstractSpinBox::UpDownArrows;
        option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
        style.drawComplexControl(QStyle::CC_SpinBox, &option, &painter, &spin);
      }
      EXPECT_TRUE(nothing_drawn_outside(image, rect))
        << "spin box " << width << " at " << factor;
    }
  }
}

// ---- sharp arrows, hover fill, combo rows, form labels, horizontal scroll ----------------------

namespace
{

/// The widget painted into an image at device pixel ratio @p ratio, as the capture of an AR
/// copy does it.
QImage
render_at_ratio(QWidget * widget, double ratio)
{
  QImage image(
    static_cast<int>(std::ceil(widget->width() * ratio)),
    static_cast<int>(std::ceil(widget->height() * ratio)), QImage::Format_RGBA8888);
  image.setDevicePixelRatio(ratio);
  image.fill(Qt::transparent);
  widget->render(
    &image, QPoint(), QRegion(), QWidget::DrawWindowBackground | QWidget::DrawChildren);
  image.setDevicePixelRatio(1.0);
  return image;
}

/// The share of the in-between grey pixels of @p area whose 2x2 neighbourhood is flat. A drawing
/// that was made at a lower resolution and magnified with nearest-neighbour sampling has an edge
/// whose in-between pixels sit in flat blocks several pixels wide (a share near 0.4 and more); a
/// drawing made at the final size, or magnified with a smooth filter, changes from pixel to pixel
/// along its edge (a share near 0). The in-between grey is the middle 70 % of the range of the
/// area; 99 is returned when the area has too little contrast to tell, 0 when it has hardly any
/// in-between pixels.
double
flat_block_share(const QImage & image, const QRect & area)
{
  const QImage c = image.copy(area.intersected(image.rect()))
    .convertToFormat(QImage::Format_RGBA8888);
  int low = 255;
  int high = 0;
  for (int y = 0; y < c.height(); ++y) {
    for (int x = 0; x < c.width(); ++x) {
      low = std::min(low, qGray(c.pixel(x, y)));
      high = std::max(high, qGray(c.pixel(x, y)));
    }
  }
  if (high - low < 60) {
    return 99.0;
  }
  const int from = low + (high - low) * 15 / 100;
  const int to = high - (high - low) * 15 / 100;
  int between = 0;
  int flat = 0;
  for (int y = 0; y + 1 < c.height(); ++y) {
    for (int x = 0; x + 1 < c.width(); ++x) {
      const int gray = qGray(c.pixel(x, y));
      if (gray <= from || gray >= to) {
        continue;
      }
      ++between;
      if (c.pixel(x + 1, y) == c.pixel(x, y) && c.pixel(x, y + 1) == c.pixel(x, y) &&
        c.pixel(x + 1, y + 1) == c.pixel(x, y))
      {
        ++flat;
      }
    }
  }
  // Hardly any in-between grey: the edge is crisp, nothing was magnified.
  return between >= 10 ? static_cast<double>(flat) / between : 0.0;
}

/// The box around the dark pixels (darker than @p limit) of @p area, grown by @p margin; empty when
/// there is none. It is where the arrow of a control is, whatever its size in a Qt version.
QRect
dark_bounds(const QImage & image, const QRect & area, int limit, int margin)
{
  QRect box;
  const QRect clipped = area.intersected(image.rect());
  for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
    for (int x = clipped.left(); x <= clipped.right(); ++x) {
      if (qGray(image.pixel(x, y)) < limit) {
        box = box.united(QRect(x, y, 1, 1));
      }
    }
  }
  return box.isEmpty() ? box : box.adjusted(-margin, -margin, margin, margin).intersected(clipped);
}

double
mean_luminance(const QImage & image, const QRect & area)
{
  const QImage c = image.copy(area.intersected(image.rect()))
    .convertToFormat(QImage::Format_RGBA8888);
  double sum = 0.0;
  for (int y = 0; y < c.height(); ++y) {
    for (int x = 0; x < c.width(); ++x) {
      sum += qGray(c.pixel(x, y));
    }
  }
  return c.width() * c.height() > 0 ? sum / (c.width() * c.height()) : 0.0;
}

QRect
scaled_rect(const QRect & r, double ratio)
{
  return QRect(
    static_cast<int>(r.x() * ratio), static_cast<int>(r.y() * ratio),
    static_cast<int>(r.width() * ratio), static_cast<int>(r.height() * ratio));
}

/// The number of pixels of @p area that are neither the background nor the mark colour: the
/// grey levels of an antialiased edge.
int
intermediate_pixels(const QImage & image, const QRect & area)
{
  int count = 0;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      const int gray = qGray(image.pixel(x, y));
      if (gray > 30 && gray < 225) {
        ++count;
      }
    }
  }
  return count;
}

void
hover_at(QWidget * widget, const QPoint & at)
{
  qt_compat::InputDevices devices;
  widget->setAttribute(Qt::WA_UnderMouse, true);
  QEnterEvent enter(at, widget->mapTo(widget->window(), at), widget->mapToGlobal(at));
  QCoreApplication::sendEvent(widget, &enter);
  auto in = qt_compat::make_hover_event(
    devices, QEvent::HoverEnter, at, widget->mapToGlobal(at), QPointF(-1.0, -1.0));
  QCoreApplication::sendEvent(widget, in.get());
  auto move = qt_compat::make_hover_event(
    devices, QEvent::HoverMove, at, widget->mapToGlobal(at), QPointF(at.x(), at.y() + 1));
  QCoreApplication::sendEvent(widget, move.get());
  QCoreApplication::processEvents();
}

/// The palette rule of Qt's Windows theme: the highlight of a window that is not the active one
/// is the colour of the window itself, so a part that is filled with the inactive highlight
/// shows no colour at all. The faces are white and the marks black, as in mark_palette().
QPalette
windows_rule_palette()
{
  QPalette palette = mark_palette();
  palette.setColor(QPalette::Active, QPalette::Highlight, QColor(0, 120, 215));
  palette.setColor(QPalette::Inactive, QPalette::Highlight, QColor(Qt::white));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(Qt::white));
  return palette;
}

/// The mean of blue minus red over the light pixels of @p area (the face of a button, not its
/// arrow or its border): 0 for a white or grey face, positive for a bluish one.
double
light_blue_minus_red(const QImage & image, const QRect & area)
{
  const QImage c = image.copy(area.intersected(image.rect()))
    .convertToFormat(QImage::Format_RGBA8888);
  double sum = 0.0;
  int count = 0;
  for (int y = 0; y < c.height(); ++y) {
    for (int x = 0; x < c.width(); ++x) {
      const QRgb pixel = c.pixel(x, y);
      if (qGray(pixel) > 150) {
        sum += qBlue(pixel) - qRed(pixel);
        ++count;
      }
    }
  }
  return count > 0 ? sum / count : 0.0;
}

/// The mean grey level of the darkest 4 % of the pixels of @p area: the arrow of a button.
double
darkest_luminance(const QImage & image, const QRect & area)
{
  const QImage c = image.copy(area.intersected(image.rect()))
    .convertToFormat(QImage::Format_RGBA8888);
  std::vector<int> grays;
  for (int y = 0; y < c.height(); ++y) {
    for (int x = 0; x < c.width(); ++x) {
      grays.push_back(qGray(c.pixel(x, y)));
    }
  }
  if (grays.empty()) {
    return 0.0;
  }
  std::sort(grays.begin(), grays.end());
  const std::size_t take = std::max<std::size_t>(1, grays.size() / 25);
  double sum = 0.0;
  for (std::size_t i = 0; i < take; ++i) {
    sum += grays[i];
  }
  return sum / static_cast<double>(take);
}

/// The left mouse button pressed on @p widget at @p at and not released.
void
press_at(QWidget * widget, const QPoint & at)
{
  qt_compat::InputDevices devices;
  auto press = qt_compat::make_mouse_event(
    devices, QEvent::MouseButtonPress, at, at, widget->mapToGlobal(at), Qt::LeftButton,
    Qt::LeftButton);
  QCoreApplication::sendEvent(widget, press.get());
  QCoreApplication::processEvents();
}

void
release_at(QWidget * widget, const QPoint & at)
{
  qt_compat::InputDevices devices;
  auto release = qt_compat::make_mouse_event(
    devices, QEvent::MouseButtonRelease, at, at, widget->mapToGlobal(at), Qt::LeftButton,
    Qt::NoButton);
  QCoreApplication::sendEvent(widget, release.get());
  QCoreApplication::processEvents();
}

}  // namespace

TEST(EnlargedControls, AVectorArrowIsNotAMagnifiedBlockDrawingAtTheDeviceRatioOfACopy)
{
  const double ratio = 1.5;
  EnlargedControlsStyle style(2.0);
  QWidget widget;
  for (const auto element : {
      QStyle::PE_IndicatorArrowDown, QStyle::PE_IndicatorArrowUp, QStyle::PE_IndicatorSpinUp,
      QStyle::PE_IndicatorHeaderArrow})
  {
    QImage image(120, 120, QImage::Format_RGBA8888);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::white);
    const QRect rect(5, 5, 40, 42);
    {
      QPainter painter(&image);
      QStyleOption option;
      option.rect = rect;
      option.palette = mark_palette();
      option.state = QStyle::State_Enabled | QStyle::State_Active;
      style.drawPrimitive(element, &option, &painter, &widget);
    }
    image.setDevicePixelRatio(1.0);
    const QRect area = scaled_rect(rect, ratio);
    ASSERT_GT(dark_pixels(image, area, 128), 20) << "no arrow drawn for element " << element;
    EXPECT_GT(intermediate_pixels(image, area), 20) << "hard edge for element " << element;
    EXPECT_LT(flat_block_share(image, area), 0.2) << "blocky edge for element " << element;
  }
}

TEST(EnlargedControls, TheExpandArrowOfATreeBranchIsDrawnAsAVector)
{
  const double ratio = 1.5;
  EnlargedControlsStyle style(2.0);
  QWidget widget;
  for (const bool open : {false, true}) {
    QImage image(120, 120, QImage::Format_RGBA8888);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::white);
    const QRect rect(5, 5, 40, 42);
    {
      QPainter painter(&image);
      QStyleOption option;
      option.rect = rect;
      option.palette = mark_palette();
      option.state = QStyle::State_Enabled | QStyle::State_Active | QStyle::State_Children |
        (open ? QStyle::State_Open : QStyle::State_None);
      style.drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &widget);
    }
    image.setDevicePixelRatio(1.0);
    const QRect area = scaled_rect(rect, ratio);
    ASSERT_GT(dark_pixels(image, area, 128), 20) << "no arrow, open " << open;
    EXPECT_GT(intermediate_pixels(image, area), 20) << "hard edge, open " << open;
    if (open) {
      EXPECT_LT(flat_block_share(image, area), 0.2);
    }
  }
}

TEST(EnlargedControls, ABranchWithoutChildrenDrawsNoArrow)
{
  EnlargedControlsStyle style(2.0);
  QWidget widget;
  QImage image(60, 60, QImage::Format_RGBA8888);
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    QStyleOption option;
    option.rect = QRect(5, 5, 40, 42);
    option.palette = mark_palette();
    option.state = QStyle::State_Enabled | QStyle::State_Active;
    style.drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &widget);
  }
  EXPECT_EQ(dark_pixels(image, QRect(0, 0, 60, 60), 128), 0);
}

TEST(EnlargedControls, TheArrowsOfSpinBoxComboBoxAndScrollBarAreNotMagnifiedBlocks)
{
  const double ratio = 1.5;
  Rig twice(2.0);
  QSpinBox * spin = twice.panel().spin;
  spin->setPalette(mark_palette());
  spin->resize(spin->sizeHint().width() + 40, spin->sizeHint().height());
  QCoreApplication::processEvents();
  // The arrow of a part: the box around its dark pixels, found in the image.
  auto arrow_of = [&](const QImage & image, const QRect & part) {
      return dark_bounds(image, scaled_rect(part, ratio).adjusted(3, 3, -3, -3), 160, 3);
    };
  const QImage spin_image = render_at_ratio(spin, ratio);
  const QRect up = arrow_of(spin_image, spin_part(spin, QStyle::SC_SpinBoxUp));
  const QRect down = arrow_of(spin_image, spin_part(spin, QStyle::SC_SpinBoxDown));
  ASSERT_FALSE(up.isEmpty()) << "no up arrow";
  ASSERT_FALSE(down.isEmpty()) << "no down arrow";
  EXPECT_LT(flat_block_share(spin_image, up), 0.2) << "spin up arrow";
  EXPECT_LT(flat_block_share(spin_image, down), 0.2) << "spin down arrow";

  QComboBox * combo = twice.panel().combo;
  combo->setPalette(mark_palette());
  QCoreApplication::processEvents();
  QStyleOptionComboBox combo_option;
  combo_option.initFrom(combo);
  combo_option.rect = combo->rect();
  combo_option.subControls = QStyle::SC_All;
  const QImage combo_image = render_at_ratio(combo, ratio);
  const QRect arrow = arrow_of(
    combo_image, combo->style()->subControlRect(
      QStyle::CC_ComboBox, &combo_option, QStyle::SC_ComboBoxArrow, combo));
  ASSERT_FALSE(arrow.isEmpty()) << "no combo arrow";
  EXPECT_LT(flat_block_share(combo_image, arrow), 0.2) << "combo arrow";

  QScrollBar * bar = twice.panel().bar;
  bar->setPalette(mark_palette());
  bar->resize(bar->sizeHint().width(), 200);
  bar->show();
  QCoreApplication::processEvents();
  QStyleOptionSlider bar_option = slider_option(bar);
  const QImage bar_image = render_at_ratio(bar, ratio);
  const QRect add_line = arrow_of(
    bar_image, bar->style()->subControlRect(
      QStyle::CC_ScrollBar, &bar_option, QStyle::SC_ScrollBarAddLine, bar));
  const QRect sub_line = arrow_of(
    bar_image, bar->style()->subControlRect(
      QStyle::CC_ScrollBar, &bar_option, QStyle::SC_ScrollBarSubLine, bar));
  ASSERT_FALSE(add_line.isEmpty()) << "no scroll bar down arrow";
  ASSERT_FALSE(sub_line.isEmpty()) << "no scroll bar up arrow";
  EXPECT_LT(flat_block_share(bar_image, add_line), 0.2) << "scroll bar down arrow";
  EXPECT_LT(flat_block_share(bar_image, sub_line), 0.2) << "scroll bar up arrow";
}

TEST(EnlargedControls, TheEditFieldOfASpinBoxEndsWhereItsButtonsBeginFramelessOrFramed)
{
  for (const bool frame : {false, true}) {
    SCOPED_TRACE(frame ? "framed spin box" : "frameless spin box (the editor of a property tree)");
    Rig twice(2.0);
    QSpinBox * spin = twice.panel().spin;
    spin->setFrame(frame);
    spin->resize(spin->sizeHint().width() + 40, spin->sizeHint().height());
    QCoreApplication::processEvents();
    const QRect up = spin_part(spin, QStyle::SC_SpinBoxUp, frame);
    const QRect down = spin_part(spin, QStyle::SC_SpinBoxDown, frame);
    const QRect edit = spin_part(spin, QStyle::SC_SpinBoxEditField, frame);
    const QLineEdit * line = spin->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    // The buttons lie inside the widget, whole, and the edit field stops before them.
    EXPECT_TRUE(spin->rect().contains(up));
    EXPECT_TRUE(spin->rect().contains(down));
    EXPECT_EQ(up.width(), down.width());
    EXPECT_FALSE(line->geometry().intersects(up));
    EXPECT_FALSE(line->geometry().intersects(down));
    EXPECT_EQ(line->geometry(), edit);
    EXPECT_LT(edit.right(), up.left());
    if (!frame) {
      EXPECT_EQ(edit.right() + 1, up.left());
    }
    // The buttons are as wide as the factor times the base style draws them: nothing is cut.
    Rig plain;
    QSpinBox * base = plain.panel().spin;
    base->setFrame(frame);
    base->resize(spin->size() / 2);
    QCoreApplication::processEvents();
    EXPECT_GE(up.width(), 2 * spin_part(base, QStyle::SC_SpinBoxUp, frame).width() - 2);
  }
}

TEST(EnlargedControls, TheArrowOfASpinButtonSitsInTheMiddleOfTheWholeButton)
{
  for (const bool frame : {false, true}) {
    for (const double ratio : {1.0, 1.5}) {
      SCOPED_TRACE(
        std::string(frame ? "framed" : "frameless") + ", device ratio " + std::to_string(ratio));
      Rig twice(2.0);
      QSpinBox * spin = twice.panel().spin;
      spin->setFrame(frame);
      spin->setPalette(mark_palette());
      spin->resize(spin->sizeHint().width() + 40, spin->sizeHint().height());
      QCoreApplication::processEvents();
      const QImage image = render_at_ratio(spin, ratio);
      for (const auto part : {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown}) {
        const QRect area = scaled_rect(spin_part(spin, part, frame), ratio);
        const QRect arrow = dark_bounds(image, area.adjusted(3, 3, -3, -3), 160, 0);
        ASSERT_FALSE(arrow.isEmpty()) << "no arrow in the button " << part;
        const int left_gap = arrow.left() - area.left();
        const int right_gap = area.right() - arrow.right();
        // Two device pixels of slack for the rounding of the edges and the antialiasing.
        EXPECT_LE(std::abs(left_gap - right_gap), 2 + static_cast<int>(ratio))
          << "the arrow is off the middle of the button " << part << ": " << left_gap
          << " pixels to its left, " << right_gap << " to its right";
      }
    }
  }
}

TEST(EnlargedControls, ThePointerOnAnSpinButtonTintsThatButtonBlueAndKeepsItsArrowDark)
{
  const double ratio = 1.5;
  for (const bool frame : {false, true}) {
    SCOPED_TRACE(frame ? "framed spin box" : "frameless spin box (the editor of a property tree)");
    Rig twice(2.0);
    QSpinBox * spin = twice.panel().spin;
    spin->setFrame(frame);
    spin->setPalette(windows_rule_palette());
    spin->resize(spin->sizeHint().width() + 40, spin->sizeHint().height());
    QCoreApplication::processEvents();
    // The window of a copy is never the active one, so the colour group is the inactive one.
    ASSERT_FALSE(spin->window()->isActiveWindow());
    const QRect up_rect = spin_part(spin, QStyle::SC_SpinBoxUp, frame);
    const QRect down_rect = spin_part(spin, QStyle::SC_SpinBoxDown, frame);
    const QRect up = scaled_rect(up_rect, ratio);
    const QRect down = scaled_rect(down_rect, ratio);
    const QImage idle = render_at_ratio(spin, ratio);
    hover_at(spin, up_rect.center());
    const QImage hovered = render_at_ratio(spin, ratio);
    EXPECT_GE(light_blue_minus_red(hovered, up) - light_blue_minus_red(idle, up), 10.0)
      << "the hovered button has no blue tint";
    EXPECT_LE(darkest_luminance(hovered, up), darkest_luminance(idle, up) + 5.0)
      << "the arrow of the hovered button is lighter";
    // The base style may move the arrow of the other button; its face gets no colour.
    EXPECT_NEAR(light_blue_minus_red(hovered, down), light_blue_minus_red(idle, down), 2.0)
      << "the other button changed";

    press_at(spin, up_rect.center());
    const QImage pressed = render_at_ratio(spin, ratio);
    release_at(spin, up_rect.center());
    EXPECT_GT(
      light_blue_minus_red(pressed, up), light_blue_minus_red(hovered, up) + 5.0)
      << "the pressed button is not bluer than the hovered one";
    EXPECT_LE(darkest_luminance(pressed, up), darkest_luminance(idle, up) + 5.0)
      << "the arrow of the pressed button is lighter";
  }
}

TEST(EnlargedControls, ThePointerOnTheComboArrowTintsTheArrowPartBlueAndKeepsTheArrowDark)
{
  Rig twice(2.0);
  QComboBox * combo = twice.panel().combo;
  combo->setPalette(windows_rule_palette());
  QCoreApplication::processEvents();
  ASSERT_FALSE(combo->window()->isActiveWindow());
  const double ratio = 1.5;
  QStyleOptionComboBox option;
  option.initFrom(combo);
  option.rect = combo->rect();
  option.subControls = QStyle::SC_All;
  const QRect arrow =
    combo->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, combo);
  ASSERT_FALSE(arrow.isEmpty());
  const QImage idle = render_at_ratio(combo, ratio);
  hover_at(combo, arrow.center());
  const QImage hovered = render_at_ratio(combo, ratio);
  const QRect device = scaled_rect(arrow, ratio);
  EXPECT_GE(light_blue_minus_red(hovered, device) - light_blue_minus_red(idle, device), 10.0);
  EXPECT_LE(darkest_luminance(hovered, device), darkest_luminance(idle, device) + 5.0);
}

TEST(EnlargedControls, TheRowsOfAComboPopupAreAsTallAsTheComboAndPlainAgainAfterTheStyleIsOff)
{
  Rig plain;
  Rig twice(2.0);
  QComboBox * wide = twice.panel().combo;
  QComboBox * base = plain.panel().combo;
  auto row_height = [](QComboBox * combo) {
      combo->showPopup();
      QCoreApplication::processEvents();
      const int height = combo->view()->visualRect(combo->model()->index(0, 0)).height();
      combo->hidePopup();
      QCoreApplication::processEvents();
      return height;
    };
  const int plain_row = row_height(base);
  ASSERT_GT(plain_row, 0);
  EXPECT_EQ(row_height(wide), wide->height());
  EXPECT_GT(wide->height(), plain_row);

  remove_style_from_tree(twice.panel().root.get());
  QCoreApplication::processEvents();
  EXPECT_EQ(row_height(wide), plain_row);
  EXPECT_EQ(
    wide->view()->itemDelegate()->objectName().indexOf(QStringLiteral("qar_combo")), -1);
}

namespace
{

/// The editor of a property tree cell that holds a choice: a combo box.
class ComboCellDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QWidget * createEditor(
    QWidget * parent, const QStyleOptionViewItem &, const QModelIndex &) const override
  {
    auto * editor = new QComboBox(parent);
    editor->addItems({QStringLiteral("<Fixed Frame>"), QStringLiteral("map"),
        QStringLiteral("odom")});
    return editor;
  }
};

int
popup_row_height(QComboBox * combo)
{
  combo->showPopup();
  QCoreApplication::processEvents();
  return combo->view()->visualRect(combo->model()->index(0, 0)).height();
}

}  // namespace

TEST(EnlargedControls, TheRowsOfAnInCellComboPopupAreAsTallAsTheEditorInTheCell)
{
  EnlargedControlsStyle style(2.0);
  QTreeView tree;
  tree.setUniformRowHeights(true);
  tree.setHeaderHidden(true);
  tree.setEditTriggers(QAbstractItemView::AllEditTriggers);
  auto * model = new QStandardItemModel(0, 2, &tree);
  for (int i = 0; i < 4; ++i) {
    model->appendRow({new QStandardItem(QStringLiteral("Reference Frame")),
        new QStandardItem(QStringLiteral("map"))});
  }
  tree.setModel(model);
  tree.setItemDelegateForColumn(1, new ComboCellDelegate(&tree));
  apply_style_to_tree(&tree, &style);
  tree.setAttribute(Qt::WA_DontShowOnScreen);
  tree.resize(500, 300);
  tree.show();
  QCoreApplication::processEvents();
  tree.edit(model->index(1, 1));
  auto * editor = tree.viewport()->findChild<QComboBox *>();
  ASSERT_NE(editor, nullptr);
  // The cell is lower than the drop-down wants to be, so the check below proves something.
  ASSERT_LT(editor->height(), editor->sizeHint().height());
  EXPECT_EQ(popup_row_height(editor), editor->height());
  editor->hidePopup();
}

TEST(EnlargedControls, AComboBoxThatChangesHeightGivesItsOpenPopupRowsOfTheNewHeight)
{
  Rig twice(2.0);
  QComboBox * combo = twice.panel().combo;
  EXPECT_EQ(popup_row_height(combo), combo->height());
  const int taller = combo->height() + 12;
  combo->setFixedHeight(taller);
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();
  // The popup is still open: its rows are laid out again for the new height.
  EXPECT_EQ(combo->view()->visualRect(combo->model()->index(0, 0)).height(), taller);
  combo->hidePopup();
  QCoreApplication::processEvents();
  EXPECT_EQ(popup_row_height(combo), taller);
  combo->hidePopup();
}

namespace
{

/// A delegate that records what the popup's wrapper hands to it.
class RecordingDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  bool editorEvent(
    QEvent * event, QAbstractItemModel *, const QStyleOptionViewItem &,
    const QModelIndex &) override
  {
    ++editor_events;
    last_event_type = event->type();
    return true;
  }

  bool helpEvent(
    QHelpEvent *, QAbstractItemView *, const QStyleOptionViewItem &,
    const QModelIndex &) override
  {
    ++help_events;
    return true;
  }

  int editor_events = 0;
  int help_events = 0;
  QEvent::Type last_event_type = QEvent::None;
};

}  // namespace

TEST(EnlargedControls, TheComboPopupWrapperPassesTheItemEventsOnToTheDelegateItWraps)
{
  RecordingDelegate recording;  // outlives the combo box that shows it
  QComboBox combo;
  combo.addItems({QStringLiteral("map"), QStringLiteral("odom")});
  combo.setItemDelegate(&recording);
  EnlargedControlsStyle style(2.0);
  apply_style_to_tree(&combo, &style);
  QCoreApplication::processEvents();

  QAbstractItemDelegate * wrapper = combo.view()->itemDelegate();
  ASSERT_NE(wrapper, &recording) << "the popup's delegate is the wrapper";
  ASSERT_NE(wrapper->objectName().indexOf(QStringLiteral("qar_combo")), -1);
  const QModelIndex index = combo.model()->index(0, 0);
  QStyleOptionViewItem option;
  QEvent release(QEvent::MouseButtonRelease);
  EXPECT_TRUE(wrapper->editorEvent(&release, combo.model(), option, index));
  EXPECT_EQ(recording.editor_events, 1);
  EXPECT_EQ(recording.last_event_type, QEvent::MouseButtonRelease);
  QHelpEvent help(QEvent::ToolTip, QPoint(3, 3), QPoint(3, 3));
  EXPECT_TRUE(wrapper->helpEvent(&help, combo.view(), option, index));
  EXPECT_EQ(recording.help_events, 1);

  remove_style_from_tree(&combo);
  EXPECT_EQ(combo.view()->itemDelegate(), &recording) << "the wrapped delegate is put back";
}

TEST(EnlargedControls, TheLabelOfAFormRowIsCentredInTheRow)
{
  QWidget host;
  auto * form = new QFormLayout(&host);
  auto * label = new QLabel(QStringLiteral("Device:"));
  auto * combo = new QComboBox;
  combo->addItems({QStringLiteral("HoloLens 2")});
  form->addRow(label, combo);
  EnlargedControlsStyle style(2.0);
  apply_style_to_tree(&host, &style);
  host.setAttribute(Qt::WA_DontShowOnScreen);
  host.resize(300, 120);
  host.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(label->height(), combo->height());
  EXPECT_EQ(label->y(), combo->y());
  EXPECT_TRUE(label->alignment() & Qt::AlignVCenter);

  const Qt::Alignment before_alignment = QLabel().alignment();
  remove_style_from_tree(&host);
  QCoreApplication::processEvents();
  EXPECT_EQ(label->alignment(), before_alignment);
  EXPECT_EQ(label->minimumHeight(), 0);
}

TEST(EnlargedControls, FormRowsKeepTheirSpacingWhenTheFormIsTaller)
{
  auto pitch = [](int host_height) {
      QWidget host;
      auto * form = new QFormLayout(&host);
      auto * first = new QLabel(QStringLiteral("One:"));
      auto * second = new QLabel(QStringLiteral("Two:"));
      form->addRow(first, new QComboBox);
      form->addRow(second, new QComboBox);
      EnlargedControlsStyle style(2.0);
      apply_style_to_tree(&host, &style);
      host.setAttribute(Qt::WA_DontShowOnScreen);
      host.resize(300, host_height);
      host.show();
      QCoreApplication::processEvents();
      const int result = second->y() - first->y();
      remove_style_from_tree(&host);
      return result;
    };
  EXPECT_EQ(pitch(140), pitch(600));
}

TEST(EnlargedControls, ALabelThatIsNoRowLabelIsLeftAlone)
{
  QWidget host;
  auto * column = new QVBoxLayout(&host);
  auto * label = new QLabel(QStringLiteral("Alone"));
  column->addWidget(label);
  column->addWidget(new QComboBox);
  EnlargedControlsStyle style(2.0);
  apply_style_to_tree(&host, &style);
  host.setAttribute(Qt::WA_DontShowOnScreen);
  host.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(label->minimumHeight(), 0);
  EXPECT_FALSE(label->property("qar_form_label_min_height").isValid());
}

namespace
{

/// A tree whose two columns together are wider than its viewport and that shows no horizontal
/// scroll bar.
struct WideTree
{
  explicit WideTree(QStyle * style)
  {
    tree.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    model = new QStandardItemModel(0, 2, &tree);
    for (int i = 0; i < 4; ++i) {
      model->appendRow({new QStandardItem(QStringLiteral("Name %1").arg(i)),
          new QStandardItem(QStringLiteral("Value %1").arg(i))});
    }
    tree.setModel(model);
    tree.header()->setStretchLastSection(false);
    tree.setColumnWidth(0, 170);
    tree.setColumnWidth(1, 150);
    if (style != nullptr) {
      apply_style_to_tree(&tree, style);
    }
    tree.setAttribute(Qt::WA_DontShowOnScreen);
    tree.resize(300, 120);
    tree.show();
    QCoreApplication::processEvents();
  }

  int select_second_column()
  {
    tree.setCurrentIndex(model->index(1, 1));
    tree.scrollTo(model->index(1, 1));
    QCoreApplication::processEvents();
    return tree.horizontalScrollBar()->value();
  }

  QTreeView tree;
  QStandardItemModel * model = nullptr;
};

}  // namespace

TEST(EnlargedControls, ASelectedCellDoesNotScrollAViewWithoutAHorizontalBar)
{
  {
    // Without the style the view does scroll, so the check below proves something.
    WideTree plain(nullptr);
    ASSERT_GT(plain.select_second_column(), 0);
  }
  EnlargedControlsStyle style(2.0);
  WideTree styled(&style);
  EXPECT_EQ(styled.select_second_column(), 0);

  remove_style_from_tree(&styled.tree);
  styled.tree.horizontalScrollBar()->setValue(0);
  EXPECT_GT(styled.select_second_column(), 0);
}

TEST(EnlargedControls, AViewThatShowsItsHorizontalBarStillScrolls)
{
  EnlargedControlsStyle style(2.0);
  WideTree styled(&style);
  styled.tree.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  QCoreApplication::processEvents();
  EXPECT_GT(styled.select_second_column(), 0);
}

namespace
{

/// What the property tree of RViz does to its two columns: they share the width of the whole
/// frame, which includes the vertical scroll bar, and a new split is set only when the width of
/// the first column changes.
class ColumnSplitter : public QObject
{
public:
  explicit ColumnSplitter(QTreeView * tree)
  : QObject(tree), tree_(tree)
  {
    tree->installEventFilter(this);
  }

  bool eventFilter(QObject * target, QEvent * event) override
  {
    if (target == tree_ && (event->type() == QEvent::Resize || event->type() == QEvent::Paint)) {
      split();
    }
    return false;
  }

  void split()
  {
    const int width = tree_->contentsRect().width();
    const int first = width / 2;
    if (first != tree_->columnWidth(0)) {
      tree_->setColumnWidth(0, first);
      tree_->setColumnWidth(1, width - first);
    }
  }

  /// The width that the split gives the last column.
  int last_width() const {return tree_->contentsRect().width() - tree_->columnWidth(0);}

private:
  QTreeView * tree_;
};

/// A tree like the property tree of RViz: two columns split by the frame width, no horizontal
/// scroll bar, a spin box editor in the second column and enough rows for a vertical bar.
struct SplitTree
{
  explicit SplitTree(QStyle * style, int rows = 40)
  {
    tree.setUniformRowHeights(true);
    tree.setHeaderHidden(true);
    tree.setEditTriggers(QAbstractItemView::AllEditTriggers);
    tree.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    model = new QStandardItemModel(0, 2, &tree);
    for (int i = 0; i < rows; ++i) {
      auto * value = new QStandardItem;
      value->setData(i + 1, Qt::EditRole);
      model->appendRow({new QStandardItem(QStringLiteral("Row %1").arg(i)), value});
    }
    tree.setModel(model);
    tree.setItemDelegateForColumn(1, new SpinDelegate(&tree));
    splitter = new ColumnSplitter(&tree);
    if (style != nullptr) {
      apply_style_to_tree(&tree, style);
    }
    tree.setAttribute(Qt::WA_DontShowOnScreen);
    tree.resize(600, 400);
    tree.show();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
  }

  /// The pixels by which the columns reach past the viewport.
  int overflow() const {return tree.header()->length() - tree.viewport()->width();}

  QTreeView tree;
  QStandardItemModel * model = nullptr;
  ColumnSplitter * splitter = nullptr;
};

}  // namespace

TEST(EnlargedControls, TheLastColumnEndsAtTheViewportWhenTheVerticalBarShows)
{
  {
    // Without the style the columns reach under the vertical bar, so the checks below prove
    // something.
    SplitTree plain(nullptr);
    ASSERT_TRUE(plain.tree.verticalScrollBar()->isVisibleTo(&plain.tree));
    ASSERT_GT(plain.overflow(), 0);
  }
  EnlargedControlsStyle style(2.0);
  SplitTree styled(&style);
  QTreeView & tree = styled.tree;
  ASSERT_TRUE(tree.verticalScrollBar()->isVisibleTo(&tree));
  EXPECT_EQ(styled.overflow(), 0);
  EXPECT_EQ(tree.horizontalScrollBar()->maximum(), 0);

  const QModelIndex index = styled.model->index(1, 1);
  tree.setCurrentIndex(index);
  tree.edit(index);
  auto * editor = tree.viewport()->findChild<QSpinBox *>();
  ASSERT_NE(editor, nullptr);
  EXPECT_LT(editor->geometry().right(), tree.viewport()->width());
  const QRect up = spin_part(editor, QStyle::SC_SpinBoxUp, false)
    .translated(editor->mapTo(tree.viewport(), QPoint(0, 0)));
  EXPECT_TRUE(tree.viewport()->rect().contains(up)) << "the up button reaches past the viewport";
  tree.scrollTo(index);
  QCoreApplication::processEvents();
  EXPECT_EQ(tree.horizontalScrollBar()->value(), 0);
}

TEST(EnlargedControls, TheLastColumnGetsItsWidthBackWithoutTheBarAndAfterTheStyleIsOff)
{
  EnlargedControlsStyle style(2.0);
  SplitTree styled(&style);
  QTreeView & tree = styled.tree;
  ASSERT_TRUE(tree.verticalScrollBar()->isVisibleTo(&tree));
  ASSERT_EQ(styled.overflow(), 0);
  ASSERT_LT(tree.columnWidth(1), styled.splitter->last_width());

  // Tall enough for every row: no vertical bar, the viewport is as wide as the frame, and the
  // column has the width of the split.
  tree.resize(600, tree.sizeHintForRow(0) * styled.model->rowCount() + 100);
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();
  ASSERT_FALSE(tree.verticalScrollBar()->isVisibleTo(&tree));
  EXPECT_EQ(styled.overflow(), 0);
  EXPECT_EQ(tree.columnWidth(1), styled.splitter->last_width());

  // Short again: fitted to the viewport; then the style goes and the split is back.
  tree.resize(600, 400);
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();
  ASSERT_TRUE(tree.verticalScrollBar()->isVisibleTo(&tree));
  ASSERT_EQ(styled.overflow(), 0);
  remove_style_from_tree(&tree);
  QCoreApplication::processEvents();
  EXPECT_EQ(tree.columnWidth(1), styled.splitter->last_width());
  EXPECT_GT(styled.overflow(), 0);
}

namespace
{

/// A tree of three columns of 200 pixels without a stretched column and a horizontal bar, in a
/// frame that holds 498 pixels of them: the pin cuts the last column to 98.
struct ThreeColumnTree
{
  explicit ThreeColumnTree(QStyle * style)
  {
    tree.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree.header()->setStretchLastSection(false);
    model = new QStandardItemModel(0, 3, &tree);
    for (int i = 0; i < 4; ++i) {
      model->appendRow({new QStandardItem(QStringLiteral("A %1").arg(i)),
          new QStandardItem(QStringLiteral("B %1").arg(i)),
          new QStandardItem(QStringLiteral("C %1").arg(i))});
    }
    tree.setModel(model);
    for (int column = 0; column < 3; ++column) {
      tree.setColumnWidth(column, 200);
    }
    if (style != nullptr) {
      apply_style_to_tree(&tree, style);
    }
    tree.setAttribute(Qt::WA_DontShowOnScreen);
    tree.resize(500, 300);
    tree.show();
    settle();
  }

  static void settle()
  {
    for (int i = 0; i < 3; ++i) {
      QCoreApplication::processEvents();
    }
  }

  int overflow() const {return tree.header()->length() - tree.viewport()->width();}

  QTreeView tree;
  QStandardItemModel * model = nullptr;
};

}  // namespace

TEST(EnlargedControls, EveryColumnGetsItsOwnWidthBackWhenTheLastColumnChanges)
{
  EnlargedControlsStyle style(2.0);
  ThreeColumnTree styled(&style);
  QTreeView & tree = styled.tree;
  ASSERT_EQ(styled.overflow(), 0);
  ASSERT_EQ(tree.columnWidth(2), 98);

  // The last column goes and the one before it ends at the viewport.
  tree.setColumnHidden(2, true);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(1), 200);
  EXPECT_LE(styled.overflow(), 0);

  // It comes back with the cut width, and with room it has the width its owner gave it.
  tree.setColumnHidden(2, false);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(2), 98);
  EXPECT_EQ(styled.overflow(), 0);
  tree.resize(1200, 300);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(0), 200);
  EXPECT_EQ(tree.columnWidth(1), 200);
  EXPECT_EQ(tree.columnWidth(2), 200);

  // Narrow again, then the middle column becomes the last one: the cut column is whole again.
  tree.resize(500, 300);
  ThreeColumnTree::settle();
  ASSERT_EQ(tree.columnWidth(2), 98);
  tree.header()->moveSection(2, 0);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(2), 200);
  EXPECT_EQ(styled.overflow(), 0);
  EXPECT_EQ(tree.columnWidth(1), 98);

  // The style goes: the column that was cut last has its width back.
  remove_style_from_tree(&tree);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(1), 200);
  EXPECT_EQ(tree.columnWidth(2), 200);
}

TEST(EnlargedControls, ThePinFitsAtOnceWhenAColumnMovesOrTheScrollPolicyChanges)
{
  EnlargedControlsStyle style(2.0);
  ThreeColumnTree styled(&style);
  QTreeView & tree = styled.tree;
  ASSERT_EQ(tree.columnWidth(2), 98);

  // A policy change fits at the next paint of the frame, with no resize of the view.
  tree.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(2), 200);
  tree.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(2), 98);
  EXPECT_EQ(styled.overflow(), 0);

  // A section that moves to the end is cut, the one that leaves it is whole.
  tree.header()->moveSection(0, 2);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(0), 98);
  EXPECT_EQ(tree.columnWidth(2), 200);
  EXPECT_EQ(styled.overflow(), 0);

  // A column that is added at the end is fitted (the others leave room for it).
  for (int column = 0; column < 3; ++column) {
    tree.setColumnWidth(column, 100);
  }
  ThreeColumnTree::settle();
  tree.model()->insertColumn(3);
  tree.setColumnWidth(3, 300);
  ThreeColumnTree::settle();
  EXPECT_EQ(tree.columnWidth(3), 198);
  EXPECT_EQ(styled.overflow(), 0);
}

TEST(EnlargedControls, AWindowMadeBelowAStyledTreeHasTheStyleAndOneBelowAPlainTreeHasNot)
{
  EnlargedControlsStyle style(2.0);
  QWidget styled_root;
  QWidget plain_root;
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(nullptr), nullptr);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&styled_root), nullptr);
  apply_style_to_tree(&styled_root, &style);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&styled_root), &style);

  // RViz parents its colour picker and its input boxes into the panel that opened them; the
  // spreader gives the style to every window made below the root, and all of its controls.
  QColorDialog color(Qt::white, &styled_root);
  QInputDialog input(&styled_root);
  QColorDialog plain(Qt::white, &plain_root);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&color), &style);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&input), &style);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&plain), nullptr);
  const auto spins = color.findChildren<QSpinBox *>();
  ASSERT_FALSE(spins.isEmpty());
  for (QSpinBox * spin : spins) {
    EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(spin), &style);
  }

  // Taking the style off the tree takes it off its windows as well.
  remove_style_from_tree(&styled_root);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&color), nullptr);
  EXPECT_EQ(qar_qt_streaming::enlarged_controls_style_of(&input), nullptr);
}
