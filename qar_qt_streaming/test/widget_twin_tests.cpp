// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// WidgetTwinBinder: a second widget tree shows the state of a first one (sync) and gives its
// user input to it (forward). Two small trees are made by one function, as a host makes the
// original and the twin of a panel; the twin tree is shown with WA_DontShowOnScreen under the
// offscreen platform. Mouse input is a press and a release sent to the widget under a point of
// the twin, the way an input injector sends it. Every assertion counts the signals of the
// ORIGINAL, whose slots a host has connected.

#include <gtest/gtest.h>

#include "qar_qt_streaming/qt_compat.hpp"
#include "qar_qt_streaming/widget_twin.hpp"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QObject>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QStyleOption>
#include <QStyleOptionSpinBox>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace qt_compat = qar_qt_streaming::qt_compat;

using qar_qt_streaming::kTwinTextBlocks;
using qar_qt_streaming::kTwinTextPeriodMs;
using qar_qt_streaming::WidgetTwinBinder;

namespace
{

/// A panel of one widget of each class the binder knows, built the same way every time.
struct Tree
{
  Tree()
  : root(new QWidget())
  {
    auto * layout = new QVBoxLayout(root.get());
    label = new QLabel(QStringLiteral("status"), root.get());
    button = new QPushButton(QStringLiteral("Start"), root.get());
    check = new QCheckBox(QStringLiteral("Advanced"), root.get());
    group = new QGroupBox(QStringLiteral("Group"), root.get());
    group->setCheckable(true);
    group->setChecked(false);
    combo = new QComboBox(root.get());
    edit_combo = new QComboBox(root.get());
    edit_combo->setEditable(true);
    edit_combo->setInsertPolicy(QComboBox::NoInsert);
    line = new QLineEdit(root.get());
    dspin = new QDoubleSpinBox(root.get());
    dspin->setRange(0.0, 10.0);
    dspin->setDecimals(2);
    dspin->setSingleStep(0.5);
    dspin->setValue(1.0);
    spin = new QSpinBox(root.get());
    spin->setRange(0, 100);
    spin->setValue(5);
    slider = new QSlider(Qt::Horizontal, root.get());
    slider->setRange(0, 100);
    slider->setValue(10);
    list = new QListWidget(root.get());
    list->setMaximumHeight(120);
    log = new QPlainTextEdit(root.get());
    log->setMaximumHeight(120);
    frame = new QFrame(root.get());
    tabs = new QTabWidget(root.get());
    tabs->setMaximumHeight(80);
    tabs->addTab(new QWidget(), QStringLiteral("One"));
    auto * second = new QWidget();
    page_button = new QPushButton(QStringLiteral("Page button"), second);
    tabs->addTab(second, QStringLiteral("Two"));
    for (const char * text : {"one", "two", "three"}) {
      combo->addItem(QString::fromLatin1(text));
      edit_combo->addItem(QString::fromLatin1(text));
    }
    for (const char * text : {"r0", "r1", "r2"}) {
      list->addItem(QString::fromLatin1(text));
    }
    for (QWidget * widget : std::vector<QWidget *>{label, button, check, group, combo, edit_combo,
        line, dspin, spin, slider, list, log, frame, tabs})
    {
      layout->addWidget(widget);
    }
  }

  std::unique_ptr<QWidget> root;
  QLabel * label = nullptr;
  QPushButton * button = nullptr;
  QCheckBox * check = nullptr;
  QGroupBox * group = nullptr;
  QComboBox * combo = nullptr;
  QComboBox * edit_combo = nullptr;
  QLineEdit * line = nullptr;
  QDoubleSpinBox * dspin = nullptr;
  QSpinBox * spin = nullptr;
  QSlider * slider = nullptr;
  QListWidget * list = nullptr;
  QPlainTextEdit * log = nullptr;
  QFrame * frame = nullptr;
  QTabWidget * tabs = nullptr;
  QPushButton * page_button = nullptr;
};

/// The original and the twin, with the pairs bound in one order.
struct Rig
{
  /// @param active the twin window is a window of the (offscreen) platform that can be the
  ///        active window and so hold the focus; otherwise it is drawn off-screen only.
  explicit Rig(bool active = false)
  {
    if (!active) {
      b.root->setAttribute(Qt::WA_DontShowOnScreen);
    }
    b.root->resize(480, 1100);
    b.root->show();
    if (active) {
      b.root->activateWindow();
      for (int i = 0; i < 50 && !b.root->isActiveWindow(); ++i) {
        QCoreApplication::processEvents();
      }
    }
    QCoreApplication::processEvents();
    bound = 0;
    bound += binder.bind(a.label, b.label) ? 1 : 0;
    bound += binder.bind(a.button, b.button) ? 1 : 0;
    bound += binder.bind(a.check, b.check) ? 1 : 0;
    bound += binder.bind(a.group, b.group) ? 1 : 0;
    bound += binder.bind(a.combo, b.combo) ? 1 : 0;
    bound += binder.bind(a.edit_combo, b.edit_combo) ? 1 : 0;
    bound += binder.bind(a.line, b.line) ? 1 : 0;
    bound += binder.bind(a.dspin, b.dspin) ? 1 : 0;
    bound += binder.bind(a.spin, b.spin) ? 1 : 0;
    bound += binder.bind(a.slider, b.slider) ? 1 : 0;
    bound += binder.bind(a.list, b.list) ? 1 : 0;
    bound += binder.bind(a.log, b.log) ? 1 : 0;
    bound += binder.bind(a.page_button, b.page_button) ? 1 : 0;
    clock = 1000;
    binder.set_time_source([this]() {return clock;});
  }

  static constexpr int kPairs = 13;

  Tree a;
  Tree b;
  WidgetTwinBinder binder;
  int bound = 0;
  std::int64_t clock = 0;
};

/// A press and a release at @p local of @p widget, sent to @p widget itself.
void
click_at(QWidget * widget, const QPoint & local)
{
  qt_compat::InputDevices devices;
  const QPoint global = widget->mapToGlobal(local);
  auto press = qt_compat::make_mouse_event(
    devices, QEvent::MouseButtonPress, local, local, global, Qt::LeftButton, Qt::LeftButton);
  QCoreApplication::sendEvent(widget, press.get());
  auto release = qt_compat::make_mouse_event(
    devices, QEvent::MouseButtonRelease, local, local, global, Qt::LeftButton, Qt::NoButton);
  QCoreApplication::sendEvent(widget, release.get());
}

/// Clicks the point @p local of @p widget the way the injector does: the deepest widget of the
/// tree at that point gets the events.
void
click_through_root(QWidget * root, QWidget * widget, const QPoint & local)
{
  const QPoint in_root = widget->mapTo(root, local);
  QWidget * target = root->childAt(in_root);
  if (target == nullptr) {
    target = root;
  }
  click_at(target, target->mapFrom(root, in_root));
}

QPoint
indicator_centre(QCheckBox * box)
{
  QStyleOptionButton option;
  option.initFrom(box);
  return box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, box).center();
}

void
click_centre(QWidget * root, QWidget * widget)
{
  click_through_root(root, widget, widget->rect().center());
}

void
key(QWidget * widget, int code, const QString & text)
{
  QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier, text);
  QCoreApplication::sendEvent(widget, &press);
  QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier, text);
  QCoreApplication::sendEvent(widget, &release);
}

QPoint
spin_up_centre(QDoubleSpinBox * spin)
{
  QStyleOptionSpinBox option;
  option.initFrom(spin);
  option.subControls = QStyle::SC_All;
  option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
  return spin->style()
         ->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin)
         .center();
}

/// Counts the emissions of a signal of an object.
struct Count
{
  int n = 0;
};

}  // namespace

TEST(WidgetTwin, BindRefusesNullsDifferentClassesAndASecondBind)
{
  Tree a;
  Tree b;
  WidgetTwinBinder binder;
  EXPECT_FALSE(binder.bind(nullptr, b.button));
  EXPECT_FALSE(binder.bind(a.button, nullptr));
  EXPECT_FALSE(binder.bind(a.button, b.label));
  EXPECT_TRUE(binder.bind(a.button, b.button));
  EXPECT_FALSE(binder.bind(a.button, b.button));
  EXPECT_FALSE(binder.bind(a.page_button, b.button)) << "the twin is bound already";
  EXPECT_FALSE(binder.bind(a.button, b.page_button)) << "the original is bound already";
  EXPECT_FALSE(binder.bind(a.label, a.label));
  EXPECT_FALSE(binder.bind_custom(a.frame, b.frame, nullptr));
  EXPECT_FALSE(binder.bind_custom(nullptr, b.frame, [](QWidget &, QWidget &) {}));
  EXPECT_EQ(binder.counters().pairs, 1u);
}

TEST(WidgetTwin, ASyncCopiesTheCommonState)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  rig.a.button->setEnabled(false);
  rig.a.button->setToolTip(QStringLiteral("tip"));
  rig.a.line->hide();
  EXPECT_EQ(rig.binder.sync(), 3);
  EXPECT_FALSE(rig.b.button->isEnabled());
  EXPECT_EQ(rig.b.button->toolTip(), QStringLiteral("tip"));
  EXPECT_TRUE(rig.b.line->isHidden());
  EXPECT_FALSE(rig.b.line->isVisible());
  EXPECT_TRUE(rig.b.check->isVisible()) << "untouched widgets stay as they are";

  rig.a.button->setEnabled(true);
  rig.a.button->setToolTip(QString());
  rig.a.line->show();
  EXPECT_EQ(rig.binder.sync(), 3);
  EXPECT_TRUE(rig.b.button->isEnabled());
  EXPECT_TRUE(rig.b.button->toolTip().isEmpty());
  EXPECT_FALSE(rig.b.line->isHidden());
  EXPECT_TRUE(rig.b.line->isVisible());
}

TEST(WidgetTwin, APageTheTabWidgetDoesNotShowIsNotHiddenOnTheTwin)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  // The original shows its first page, the twin its second: the button of the second page is
  // not explicitly hidden on the original, so the twin keeps showing it.
  rig.a.tabs->setCurrentIndex(0);
  rig.b.tabs->setCurrentIndex(1);
  QCoreApplication::processEvents();
  ASSERT_TRUE(rig.b.page_button->isVisible());
  ASSERT_FALSE(rig.a.page_button->isVisible());
  rig.binder.sync();
  EXPECT_EQ(rig.b.tabs->currentIndex(), 1);
  EXPECT_TRUE(rig.b.page_button->isVisible());
  EXPECT_FALSE(rig.b.page_button->isHidden());

  rig.a.page_button->hide();
  rig.binder.sync();
  EXPECT_TRUE(rig.b.page_button->isHidden());
}

TEST(WidgetTwin, ASyncCopiesButtonLabelAndGroupState)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  rig.a.button->setText(QStringLiteral("Stop"));
  rig.a.check->setText(QStringLiteral("Show"));
  rig.a.check->setChecked(true);
  rig.a.label->setText(QStringLiteral("Runtime: running"));
  rig.a.group->setTitle(QStringLiteral("Advanced"));
  rig.a.group->setChecked(true);
  EXPECT_EQ(rig.binder.sync(), 6);
  EXPECT_EQ(rig.b.button->text(), QStringLiteral("Stop"));
  EXPECT_EQ(rig.b.check->text(), QStringLiteral("Show"));
  EXPECT_TRUE(rig.b.check->isChecked());
  EXPECT_EQ(rig.b.label->text(), QStringLiteral("Runtime: running"));
  EXPECT_EQ(rig.b.group->title(), QStringLiteral("Advanced"));
  EXPECT_TRUE(rig.b.group->isChecked());

  rig.a.button->setCheckable(true);
  rig.a.button->setChecked(true);
  rig.binder.sync();
  EXPECT_TRUE(rig.b.button->isCheckable());
  EXPECT_TRUE(rig.b.button->isChecked());
}

TEST(WidgetTwin, ASyncCopiesComboItemsRolesAndIndex)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  rig.a.combo->clear();
  rig.a.combo->addItem(QStringLiteral("640 x 480"), 11);
  rig.a.combo->addItem(QStringLiteral("1280 x 720"), 22);
  rig.a.combo->addItem(QStringLiteral("1920 x 1080"), 33);
  rig.a.combo->setItemData(1, QStringLiteral("the default"), Qt::ToolTipRole);
  rig.a.combo->setCurrentIndex(2);
  rig.binder.sync();
  ASSERT_EQ(rig.b.combo->count(), 3);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(rig.b.combo->itemText(i), rig.a.combo->itemText(i));
    EXPECT_EQ(rig.b.combo->itemData(i, Qt::UserRole), rig.a.combo->itemData(i, Qt::UserRole));
    EXPECT_EQ(
      rig.b.combo->itemData(i, Qt::ToolTipRole), rig.a.combo->itemData(i, Qt::ToolTipRole));
  }
  EXPECT_EQ(rig.b.combo->currentIndex(), 2);

  // Only the index changes: one write; only an item text changes: the items are made again.
  rig.a.combo->setCurrentIndex(0);
  EXPECT_EQ(rig.binder.sync(), 1);
  EXPECT_EQ(rig.b.combo->currentIndex(), 0);
  rig.a.combo->setItemText(1, QStringLiteral("1280 x 800"));
  rig.binder.sync();
  EXPECT_EQ(rig.b.combo->itemText(1), QStringLiteral("1280 x 800"));
  EXPECT_EQ(rig.b.combo->currentIndex(), 0);

  // A change made with the signals of the original blocked is followed as well.
  {
    const QSignalBlocker blocker(rig.a.combo);
    rig.a.combo->setCurrentIndex(1);
  }
  rig.binder.sync();
  EXPECT_EQ(rig.b.combo->currentIndex(), 1);
}

TEST(WidgetTwin, ASyncCopiesLineEditTextUnlessTheTwinHasFocus)
{
  Rig rig(true);
  ASSERT_EQ(rig.bound, Rig::kPairs);
  rig.a.line->setText(QStringLiteral("192.168.0.7"));
  rig.a.line->setPlaceholderText(QStringLiteral("host"));
  rig.a.line->setReadOnly(true);
  rig.binder.sync();
  EXPECT_EQ(rig.b.line->text(), QStringLiteral("192.168.0.7"));
  EXPECT_EQ(rig.b.line->placeholderText(), QStringLiteral("host"));
  EXPECT_TRUE(rig.b.line->isReadOnly());

  rig.b.line->setFocus();
  ASSERT_TRUE(rig.b.line->hasFocus());
  rig.a.line->setText(QStringLiteral("10.0.0.1"));
  EXPECT_EQ(rig.binder.sync(), 0) << "the twin is being edited";
  EXPECT_EQ(rig.b.line->text(), QStringLiteral("192.168.0.7"));
  rig.b.line->clearFocus();
  EXPECT_EQ(rig.binder.sync(), 1);
  EXPECT_EQ(rig.b.line->text(), QStringLiteral("10.0.0.1"));
}

TEST(WidgetTwin, ASyncCopiesSpinRangeAffixesAndValue)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  rig.a.dspin->setDecimals(3);
  rig.a.dspin->setRange(0.25, 4.0);
  rig.a.dspin->setSingleStep(0.125);
  rig.a.dspin->setPrefix(QStringLiteral("size x"));
  rig.a.dspin->setSuffix(QStringLiteral(" m"));
  rig.a.dspin->setValue(1.625);
  rig.a.spin->setRange(10, 50);
  rig.a.spin->setSingleStep(5);
  rig.a.spin->setPrefix(QStringLiteral("n="));
  rig.a.spin->setSuffix(QStringLiteral("!"));
  rig.a.spin->setValue(35);
  rig.a.slider->setRange(5, 25);
  rig.a.slider->setValue(17);
  rig.binder.sync();
  EXPECT_EQ(rig.b.dspin->decimals(), 3);
  EXPECT_DOUBLE_EQ(rig.b.dspin->minimum(), 0.25);
  EXPECT_DOUBLE_EQ(rig.b.dspin->maximum(), 4.0);
  EXPECT_DOUBLE_EQ(rig.b.dspin->singleStep(), 0.125);
  EXPECT_EQ(rig.b.dspin->prefix(), QStringLiteral("size x"));
  EXPECT_EQ(rig.b.dspin->suffix(), QStringLiteral(" m"));
  EXPECT_DOUBLE_EQ(rig.b.dspin->value(), 1.625);
  EXPECT_EQ(rig.b.spin->minimum(), 10);
  EXPECT_EQ(rig.b.spin->maximum(), 50);
  EXPECT_EQ(rig.b.spin->singleStep(), 5);
  EXPECT_EQ(rig.b.spin->prefix(), QStringLiteral("n="));
  EXPECT_EQ(rig.b.spin->suffix(), QStringLiteral("!"));
  EXPECT_EQ(rig.b.spin->value(), 35);
  EXPECT_EQ(rig.b.slider->minimum(), 5);
  EXPECT_EQ(rig.b.slider->maximum(), 25);
  EXPECT_EQ(rig.b.slider->value(), 17);
  EXPECT_EQ(rig.binder.sync(), 0);
}

TEST(WidgetTwin, ASyncCopiesListRowsRolesAndCurrentRow)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  rig.a.list->clear();
  for (int i = 0; i < 4; ++i) {
    auto * item = new QListWidgetItem(QStringLiteral("row %1").arg(i));
    item->setData(Qt::UserRole, 100 + i);
    item->setToolTip(QStringLiteral("tip %1").arg(i));
    item->setForeground(i == 2 ? QBrush(Qt::red) : QBrush(Qt::blue));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(i % 2 == 0 ? Qt::Checked : Qt::Unchecked);
    rig.a.list->addItem(item);
  }
  rig.a.list->setCurrentRow(2);
  rig.binder.sync();
  ASSERT_EQ(rig.b.list->count(), 4);
  for (int i = 0; i < 4; ++i) {
    const QListWidgetItem * mine = rig.b.list->item(i);
    const QListWidgetItem * theirs = rig.a.list->item(i);
    EXPECT_EQ(mine->text(), theirs->text());
    EXPECT_EQ(mine->data(Qt::UserRole), theirs->data(Qt::UserRole));
    EXPECT_EQ(mine->toolTip(), theirs->toolTip());
    EXPECT_EQ(mine->foreground(), theirs->foreground());
    EXPECT_EQ(mine->checkState(), theirs->checkState());
    EXPECT_EQ(mine->flags(), theirs->flags());
  }
  EXPECT_EQ(rig.b.list->currentRow(), 2);
  EXPECT_NE(rig.b.list->item(1), rig.a.list->item(1)) << "the rows are copies";

  // Only the current row changes: one write. A role of one row changes: the rows are made again.
  rig.a.list->setCurrentRow(3);
  EXPECT_EQ(rig.binder.sync(), 1);
  EXPECT_EQ(rig.b.list->currentRow(), 3);
  rig.a.list->item(1)->setForeground(QBrush(Qt::green));
  EXPECT_EQ(rig.binder.sync(), 2) << "the rows, then the current row of the new rows";
  EXPECT_EQ(rig.b.list->item(1)->foreground(), QBrush(Qt::green));
  EXPECT_EQ(rig.b.list->currentRow(), 3);

  rig.a.list->clear();
  rig.binder.sync();
  EXPECT_EQ(rig.b.list->count(), 0);
  EXPECT_EQ(rig.b.list->currentRow(), -1);
}

TEST(WidgetTwin, AnEditableComboKeepsTheTwinTextWhileItHasFocus)
{
  Rig rig(true);
  ASSERT_EQ(rig.bound, Rig::kPairs);
  rig.a.edit_combo->setEditText(QStringLiteral("/clicked_point"));
  rig.binder.sync();
  EXPECT_EQ(rig.b.edit_combo->currentText(), QStringLiteral("/clicked_point"));

  rig.b.edit_combo->lineEdit()->setFocus();
  ASSERT_TRUE(rig.b.edit_combo->lineEdit()->hasFocus());
  rig.a.edit_combo->setEditText(QStringLiteral("/goal_pose"));
  rig.binder.sync();
  EXPECT_EQ(rig.b.edit_combo->currentText(), QStringLiteral("/clicked_point"));
  rig.b.edit_combo->lineEdit()->clearFocus();
  rig.binder.sync();
  EXPECT_EQ(rig.b.edit_combo->currentText(), QStringLiteral("/goal_pose"));
}

TEST(WidgetTwin, AnUnchangedPassWritesNothing)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  rig.a.label->setText(QStringLiteral("x"));
  rig.a.combo->setCurrentIndex(2);
  rig.a.list->setCurrentRow(1);
  rig.a.dspin->setValue(3.5);
  rig.a.log->setPlainText(QStringLiteral("one\ntwo"));
  EXPECT_GT(rig.binder.sync(), 0);
  const auto before = rig.binder.counters();
  EXPECT_EQ(rig.binder.sync(), 0);
  EXPECT_EQ(rig.binder.sync(), 0);
  const auto after = rig.binder.counters();
  EXPECT_EQ(after.writes, before.writes);
  EXPECT_EQ(after.passes, before.passes + 2);
  EXPECT_EQ(after.pairs, static_cast<std::size_t>(Rig::kPairs));
  EXPECT_EQ(after.plain_pairs, 0u);
}

TEST(WidgetTwin, NothingIsForwardedDuringASync)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  // Every pair changes on the original; the twin signals that fire during the pass are counted
  // on the twin, the original's slots must stay silent.
  int twin_signals = 0;
  int original_slots = 0;
  const auto twin_hit = [&twin_signals]() {++twin_signals;};
  const auto original_hit = [&original_slots]() {++original_slots;};
  QObject::connect(rig.b.dspin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), twin_hit);
  QObject::connect(rig.b.spin, QOverload<int>::of(&QSpinBox::valueChanged), twin_hit);
  QObject::connect(rig.b.slider, &QSlider::valueChanged, twin_hit);
  QObject::connect(rig.b.list, &QListWidget::currentRowChanged, twin_hit);
  QObject::connect(rig.b.combo, QOverload<int>::of(&QComboBox::currentIndexChanged), twin_hit);
  QObject::connect(rig.b.check, &QCheckBox::toggled, twin_hit);
  QObject::connect(rig.b.line, &QLineEdit::textChanged, twin_hit);
  rig.a.dspin->setValue(4.5);
  rig.a.spin->setValue(9);
  rig.a.slider->setValue(70);
  rig.a.list->setCurrentRow(2);
  rig.a.combo->setCurrentIndex(1);
  rig.a.check->setChecked(true);
  rig.a.line->setText(QStringLiteral("abc"));
  // Count the original's own signals only from here on.
  QObject::connect(rig.a.dspin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), original_hit);
  QObject::connect(rig.a.spin, QOverload<int>::of(&QSpinBox::valueChanged), original_hit);
  QObject::connect(rig.a.slider, &QSlider::valueChanged, original_hit);
  QObject::connect(rig.a.list, &QListWidget::currentRowChanged, original_hit);
  QObject::connect(rig.a.combo, QOverload<int>::of(&QComboBox::activated), original_hit);
  QObject::connect(rig.a.combo, QOverload<int>::of(&QComboBox::currentIndexChanged), original_hit);
  QObject::connect(rig.a.check, &QCheckBox::clicked, original_hit);
  QObject::connect(rig.a.check, &QCheckBox::toggled, original_hit);
  QObject::connect(rig.a.line, &QLineEdit::textChanged, original_hit);
  QObject::connect(rig.a.line, &QLineEdit::textEdited, original_hit);
  QObject::connect(rig.a.line, &QLineEdit::editingFinished, original_hit);
  EXPECT_GT(rig.binder.sync(), 0);
  EXPECT_GE(twin_signals, 7);
  EXPECT_EQ(original_slots, 0);
  EXPECT_EQ(rig.binder.counters().forwarded, 0u);
  EXPECT_FALSE(rig.binder.forwarding());
}

TEST(WidgetTwin, AClickOnTheTwinButtonClicksTheOriginalOnce)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count clicks;
  QObject::connect(rig.a.button, &QPushButton::clicked, [&clicks]() {++clicks.n;});
  bool inside = false;
  QObject::connect(
    rig.a.button, &QPushButton::clicked, [&inside, &rig]() {inside = rig.binder.forwarding();});
  click_centre(rig.b.root.get(), rig.b.button);
  EXPECT_EQ(clicks.n, 1);
  EXPECT_TRUE(inside) << "forwarding() is true while the original's slot runs";
  EXPECT_FALSE(rig.binder.forwarding());
  EXPECT_EQ(rig.binder.counters().forwarded, 1u);
  EXPECT_EQ(rig.binder.counters().refused, 0u);
  rig.binder.sync();
  EXPECT_EQ(clicks.n, 1) << "a pass does not click again";
}

TEST(WidgetTwin, AClickOnTheTwinCheckBoxTogglesTheOriginalOnceAndTheNextPassDoesNotEcho)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count toggled;
  Count clicked;
  QObject::connect(rig.a.check, &QCheckBox::toggled, [&toggled]() {++toggled.n;});
  QObject::connect(rig.a.check, &QCheckBox::clicked, [&clicked]() {++clicked.n;});
  ASSERT_FALSE(rig.a.check->isChecked());
  click_through_root(rig.b.root.get(), rig.b.check, indicator_centre(rig.b.check));
  EXPECT_TRUE(rig.a.check->isChecked());
  EXPECT_EQ(toggled.n, 1);
  EXPECT_EQ(clicked.n, 1);
  EXPECT_EQ(rig.binder.sync(), 0) << "the twin is already checked";
  EXPECT_TRUE(rig.b.check->isChecked());
  EXPECT_EQ(toggled.n, 1);
  EXPECT_EQ(clicked.n, 1);

  // The original changes it back: the twin follows and nothing is forwarded.
  rig.a.check->setChecked(false);
  EXPECT_EQ(rig.binder.sync(), 1);
  EXPECT_FALSE(rig.b.check->isChecked());
  EXPECT_EQ(clicked.n, 1);
  EXPECT_EQ(rig.binder.counters().forwarded, 1u);
}

TEST(WidgetTwin, AClickWhileTheOriginalIsDisabledIsRefused)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count clicks;
  QObject::connect(rig.a.button, &QPushButton::clicked, [&clicks]() {++clicks.n;});
  rig.a.button->setEnabled(false);  // the twin has not been synced: it is still enabled
  ASSERT_TRUE(rig.b.button->isEnabled());
  click_centre(rig.b.root.get(), rig.b.button);
  EXPECT_EQ(clicks.n, 0);
  EXPECT_EQ(rig.binder.counters().refused, 1u);
  EXPECT_EQ(rig.binder.counters().forwarded, 0u);
  rig.binder.sync();
  EXPECT_FALSE(rig.b.button->isEnabled());
  click_centre(rig.b.root.get(), rig.b.button);
  EXPECT_EQ(clicks.n, 0);
}

TEST(WidgetTwin, AGroupBoxClickIsForwarded)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count clicked;
  Count toggled;
  QObject::connect(rig.a.group, &QGroupBox::clicked, [&clicked]() {++clicked.n;});
  QObject::connect(rig.a.group, &QGroupBox::toggled, [&toggled]() {++toggled.n;});
  rig.b.group->setChecked(true);
  QMetaObject::invokeMethod(rig.b.group, "clicked", Qt::DirectConnection, Q_ARG(bool, true));
  EXPECT_TRUE(rig.a.group->isChecked());
  EXPECT_EQ(clicked.n, 1);
  EXPECT_EQ(toggled.n, 1);
}

TEST(WidgetTwin, AComboChoiceSetsTheIndexAndReEmitsActivatedAndTextActivated)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count activated;
  Count text_activated;
  Count changed;
  QString text;
  QObject::connect(
    rig.a.combo, QOverload<int>::of(&QComboBox::activated), [&activated]() {++activated.n;});
  QObject::connect(
    rig.a.combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&changed]() {++changed.n;});
  QObject::connect(
    rig.a.combo, &QComboBox::textActivated, [&text_activated, &text](const QString & t) {
      ++text_activated.n;
      text = t;
    });
  // What a choice in the popup of the twin does: the index moves and activated is emitted.
  rig.b.combo->setCurrentIndex(1);
  QMetaObject::invokeMethod(rig.b.combo, "activated", Qt::DirectConnection, Q_ARG(int, 1));
  EXPECT_EQ(rig.a.combo->currentIndex(), 1);
  EXPECT_EQ(activated.n, 1);
  EXPECT_EQ(text_activated.n, 1);
  EXPECT_EQ(text, QStringLiteral("two"));
  EXPECT_EQ(changed.n, 1);
  EXPECT_EQ(rig.binder.sync(), 0);
  EXPECT_EQ(activated.n, 1);

  // A choice out of range is not carried out as a crash.
  QMetaObject::invokeMethod(rig.b.combo, "activated", Qt::DirectConnection, Q_ARG(int, 9));
  EXPECT_EQ(rig.a.combo->currentIndex(), 1);
}

TEST(WidgetTwin, ReturnInAnEditableComboReachesTheOriginalLineEdit)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count returned;
  QObject::connect(
    rig.a.edit_combo->lineEdit(), &QLineEdit::returnPressed, [&returned]() {++returned.n;});
  QLineEdit * twin_edit = rig.b.edit_combo->lineEdit();
  // Qt hands a Return that the line edit leaves unaccepted to the combo box, which gives it to
  // the line edit again, so the twin's line edit emits returnPressed twice for one key press.
  int twin_ret = 0;
  QObject::connect(twin_edit, &QLineEdit::returnPressed, [&twin_ret]() {++twin_ret;});
  twin_edit->setText(QString());
  key(twin_edit, Qt::Key_T, QStringLiteral("t"));
  key(twin_edit, Qt::Key_F, QStringLiteral("f"));
  EXPECT_EQ(twin_edit->text(), QStringLiteral("tf"));
  EXPECT_EQ(returned.n, 0);
  key(twin_edit, Qt::Key_Return, QString());
  EXPECT_EQ(rig.a.edit_combo->currentText(), QStringLiteral("tf"));
  EXPECT_GE(twin_ret, 1);
  EXPECT_EQ(returned.n, 1) << "one Return runs the original's slot once (the twin emitted "
                           << twin_ret << ")";
  EXPECT_EQ(rig.binder.counters().forwarded, 3u) << "two typed characters and one Return";
  // The next Return is a new key press and runs the slot again.
  key(twin_edit, Qt::Key_Return, QString());
  EXPECT_EQ(returned.n, 2);
  key(twin_edit, Qt::Key_Enter, QString());
  EXPECT_EQ(returned.n, 3);
}

TEST(WidgetTwin, AReturnSentToTheComboBoxItselfAlsoRunsTheSlotOnce)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count returned;
  QObject::connect(
    rig.a.edit_combo->lineEdit(), &QLineEdit::returnPressed, [&returned]() {++returned.n;});
  key(rig.b.edit_combo, Qt::Key_Return, QString());
  EXPECT_EQ(returned.n, 1);
  key(rig.b.edit_combo, Qt::Key_Return, QString());
  EXPECT_EQ(returned.n, 2);
}

TEST(WidgetTwin, AReturnSignalOutsideAKeyPressIsForwardedEachTime)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count returned;
  QObject::connect(
    rig.a.edit_combo->lineEdit(), &QLineEdit::returnPressed, [&returned]() {++returned.n;});
  QLineEdit * twin_edit = rig.b.edit_combo->lineEdit();
  QMetaObject::invokeMethod(twin_edit, "returnPressed", Qt::DirectConnection);
  QMetaObject::invokeMethod(twin_edit, "returnPressed", Qt::DirectConnection);
  EXPECT_EQ(returned.n, 2);
}

TEST(WidgetTwin, AComboMadeEditableAfterTheBindIsFollowed)
{
  WidgetTwinBinder binder;
  QComboBox original;
  QComboBox twin;
  for (QComboBox * box : {&original, &twin}) {
    box->addItems({QStringLiteral("one"), QStringLiteral("two")});
    box->setInsertPolicy(QComboBox::NoInsert);
  }
  ASSERT_TRUE(binder.bind(&original, &twin));
  twin.show();
  Count returned;
  original.setEditable(true);
  twin.setEditable(true);
  QObject::connect(original.lineEdit(), &QLineEdit::returnPressed, [&returned]() {++returned.n;});
  binder.sync();
  key(twin.lineEdit(), Qt::Key_X, QStringLiteral("x"));
  EXPECT_EQ(original.lineEdit()->text(), QStringLiteral("onex"));
  key(twin.lineEdit(), Qt::Key_Return, QString());
  EXPECT_EQ(returned.n, 1);
  EXPECT_EQ(binder.counters().forwarded, 2u);
  binder.sync();
  EXPECT_EQ(returned.n, 1) << "a pass does not forward a second time";
  // A line edit that is made again is followed as well.
  twin.setEditable(false);
  twin.setEditable(true);
  binder.sync();
  key(twin.lineEdit(), Qt::Key_Return, QString());
  EXPECT_EQ(returned.n, 2);
}

TEST(WidgetTwin, AForwardThatReachesNothingIsNotCounted)
{
  QComboBox original;
  QComboBox twin;
  for (QComboBox * box : {&original, &twin}) {
    box->addItems({QStringLiteral("one"), QStringLiteral("two")});
  }
  WidgetTwinBinder binder;
  ASSERT_TRUE(binder.bind(&original, &twin));
  twin.setEditable(true);  // the original has no line edit to give the text to
  twin.show();
  binder.sync();
  key(twin.lineEdit(), Qt::Key_X, QStringLiteral("x"));
  key(twin.lineEdit(), Qt::Key_Return, QString());
  EXPECT_EQ(binder.counters().forwarded, 0u);
  EXPECT_EQ(binder.counters().refused, 0u);
}

TEST(WidgetTwin, ASlotThatDeletesTheComboBoxEndsTheForwardWithoutACrash)
{
  QWidget host_a;
  QWidget host_b;
  auto * original = new QComboBox(&host_a);
  auto * twin = new QComboBox(&host_b);
  for (QComboBox * box : {original, twin}) {
    box->addItems({QStringLiteral("one"), QStringLiteral("two")});
  }
  WidgetTwinBinder binder;
  ASSERT_TRUE(binder.bind(original, twin));
  QPointer<QComboBox> guard(original);
  QObject::connect(
    original, QOverload<int>::of(&QComboBox::activated), [original]() {delete original;});
  twin->setCurrentIndex(1);
  QMetaObject::invokeMethod(twin, "activated", Qt::DirectConnection, Q_ARG(int, 1));
  EXPECT_TRUE(guard.isNull());
  EXPECT_EQ(binder.counters().forwarded, 1u);
  EXPECT_EQ(binder.sync(), 0);
}

TEST(WidgetTwin, ASpinStepSetsTheOriginalValue)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count changed;
  QObject::connect(
    rig.a.dspin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    [&changed]() {++changed.n;});
  ASSERT_DOUBLE_EQ(rig.a.dspin->value(), 1.0);
  click_through_root(rig.b.root.get(), rig.b.dspin, spin_up_centre(rig.b.dspin));
  EXPECT_DOUBLE_EQ(rig.a.dspin->value(), 1.5);
  EXPECT_EQ(changed.n, 1);
  EXPECT_EQ(rig.binder.sync(), 0);
  EXPECT_EQ(changed.n, 1);

  Count spun;
  QObject::connect(rig.a.spin, QOverload<int>::of(&QSpinBox::valueChanged), [&spun]() {++spun.n;});
  rig.b.spin->setValue(40);
  EXPECT_EQ(rig.a.spin->value(), 40);
  EXPECT_EQ(spun.n, 1);
  rig.b.slider->setValue(80);
  EXPECT_EQ(rig.a.slider->value(), 80);
}

TEST(WidgetTwin, ARowClickSetsTheOriginalCurrentRow)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count rows;
  QObject::connect(rig.a.list, &QListWidget::currentRowChanged, [&rows]() {++rows.n;});
  ASSERT_EQ(rig.a.list->currentRow(), -1);
  QListWidget * list = rig.b.list;
  const QPoint at = list->visualItemRect(list->item(2)).center();
  click_at(list->viewport(), at);
  EXPECT_EQ(rig.b.list->currentRow(), 2);
  EXPECT_EQ(rig.a.list->currentRow(), 2);
  EXPECT_EQ(rows.n, 1);
  EXPECT_EQ(rig.binder.sync(), 0);
  EXPECT_EQ(rows.n, 1);
}

TEST(WidgetTwin, TextEditedEditingFinishedAndReturnReachTheOriginal)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count edited;
  Count finished;
  Count returned;
  QObject::connect(rig.a.line, &QLineEdit::textEdited, [&edited]() {++edited.n;});
  QObject::connect(rig.a.line, &QLineEdit::editingFinished, [&finished]() {++finished.n;});
  QObject::connect(rig.a.line, &QLineEdit::returnPressed, [&returned]() {++returned.n;});
  key(rig.b.line, Qt::Key_A, QStringLiteral("a"));
  key(rig.b.line, Qt::Key_B, QStringLiteral("b"));
  EXPECT_EQ(rig.b.line->text(), QStringLiteral("ab"));
  EXPECT_EQ(rig.a.line->text(), QStringLiteral("ab"));
  EXPECT_EQ(edited.n, 2);
  EXPECT_EQ(finished.n, 0);
  key(rig.b.line, Qt::Key_Return, QString());
  EXPECT_EQ(returned.n, 1);
  EXPECT_EQ(finished.n, 1);
  EXPECT_EQ(rig.binder.counters().forwarded, 4u);
  EXPECT_EQ(rig.binder.sync(), 0);
  EXPECT_EQ(rig.a.line->text(), QStringLiteral("ab"));
}

TEST(WidgetTwin, APlainTextTwinHoldsTheLastBlocksAndCopiesOnlyWhenVisible)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  QStringList lines;
  for (int i = 1; i <= 2001; ++i) {
    lines.append(QStringLiteral("line %1").arg(i));
  }
  rig.a.log->setPlainText(lines.join(QLatin1Char('\n')));

  // A twin that is not visible (its window is hidden) is not written.
  rig.b.root->hide();
  rig.binder.sync();
  EXPECT_TRUE(rig.b.log->toPlainText().isEmpty());
  rig.b.root->show();
  rig.clock = 2000;
  EXPECT_EQ(rig.binder.sync(), 1);
  EXPECT_EQ(rig.b.log->document()->blockCount(), kTwinTextBlocks);
  EXPECT_EQ(rig.b.log->document()->lastBlock().text(), QStringLiteral("line 2001"));
  EXPECT_EQ(rig.b.log->document()->firstBlock().text(), QStringLiteral("line 1802"));
  QScrollBar * bar = rig.b.log->verticalScrollBar();
  EXPECT_GT(bar->maximum(), 0);
  EXPECT_EQ(bar->value(), bar->maximum());

  // The same revision is not copied again; a change inside the period waits for it to pass.
  rig.clock = 2100;
  EXPECT_EQ(rig.binder.sync(), 0);
  rig.a.log->appendPlainText(QStringLiteral("line 2002"));
  rig.clock = 2000 + kTwinTextPeriodMs - 1;
  EXPECT_EQ(rig.binder.sync(), 0);
  EXPECT_EQ(rig.b.log->document()->lastBlock().text(), QStringLiteral("line 2001"));
  rig.clock = 2000 + kTwinTextPeriodMs;
  EXPECT_EQ(rig.binder.sync(), 1);
  EXPECT_EQ(rig.b.log->document()->lastBlock().text(), QStringLiteral("line 2002"));
  EXPECT_EQ(rig.b.log->document()->blockCount(), kTwinTextBlocks);
}

TEST(WidgetTwin, ACustomPairGetsTheCommonStateAndItsCopy)
{
  Tree a;
  Tree b;
  WidgetTwinBinder binder;
  int copies = 0;
  ASSERT_TRUE(binder.bind_custom(a.frame, b.frame, [&copies](QWidget & o, QWidget & t) {
      ++copies;
      t.setProperty("marker", o.property("marker"));
    }));
  EXPECT_EQ(binder.counters().plain_pairs, 0u);
  a.frame->setProperty("marker", QStringLiteral("m1"));
  a.frame->setToolTip(QStringLiteral("tip"));
  a.frame->setEnabled(false);
  binder.sync();
  binder.sync();
  EXPECT_EQ(copies, 2);
  EXPECT_EQ(b.frame->property("marker").toString(), QStringLiteral("m1"));
  EXPECT_EQ(b.frame->toolTip(), QStringLiteral("tip"));
  EXPECT_FALSE(b.frame->isEnabled());

  // A class with no kind keeps the common state only and is counted.
  ASSERT_TRUE(binder.bind(a.root.get(), b.root.get()));
  EXPECT_EQ(binder.counters().plain_pairs, 1u);
}

TEST(WidgetTwin, AGoneWidgetIsSkippedAndReleaseStopsForwarding)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  Count toggled;
  QObject::connect(rig.a.check, &QCheckBox::toggled, [&toggled]() {++toggled.n;});
  delete rig.b.button;
  rig.a.label->setText(QStringLiteral("still synced"));
  EXPECT_EQ(rig.binder.sync(), 1);
  EXPECT_EQ(rig.b.label->text(), QStringLiteral("still synced"));
  delete rig.a.line;
  EXPECT_EQ(rig.binder.sync(), 0);

  rig.binder.release();
  EXPECT_EQ(rig.binder.counters().pairs, 0u);
  click_through_root(rig.b.root.get(), rig.b.check, indicator_centre(rig.b.check));
  EXPECT_TRUE(rig.b.check->isChecked());
  EXPECT_FALSE(rig.a.check->isChecked());
  EXPECT_EQ(toggled.n, 0);
  EXPECT_EQ(rig.binder.counters().forwarded, 0u);
  EXPECT_EQ(rig.binder.sync(), 0);
}

TEST(WidgetTwin, TheObserverNamesTheOriginalAndTheAction)
{
  Rig rig;
  ASSERT_EQ(rig.bound, Rig::kPairs);
  std::vector<std::pair<QWidget *, std::string>> seen;
  rig.binder.set_forward_observer([&seen](QWidget & original, const char * what) {
      seen.emplace_back(&original, what);
    });
  click_centre(rig.b.root.get(), rig.b.button);
  rig.b.combo->setCurrentIndex(2);
  QMetaObject::invokeMethod(rig.b.combo, "activated", Qt::DirectConnection, Q_ARG(int, 2));
  rig.b.spin->setValue(12);
  const QPoint at = rig.b.list->visualItemRect(rig.b.list->item(1)).center();
  click_at(rig.b.list->viewport(), at);
  key(rig.b.line, Qt::Key_X, QStringLiteral("x"));
  key(rig.b.line, Qt::Key_Return, QString());
  ASSERT_GE(seen.size(), 6u);
  EXPECT_EQ(seen[0].first, rig.a.button);
  EXPECT_EQ(seen[0].second, "clicked");
  EXPECT_EQ(seen[1].first, rig.a.combo);
  EXPECT_EQ(seen[1].second, "chosen");
  EXPECT_EQ(seen[2].first, rig.a.spin);
  EXPECT_EQ(seen[2].second, "stepped");
  EXPECT_EQ(seen[3].first, rig.a.list);
  EXPECT_EQ(seen[3].second, "row chosen");
  // A typed character is carried out but not reported; the end of the edit is.
  EXPECT_EQ(seen[4].first, rig.a.line);
  EXPECT_EQ(seen[4].second, "edited");
  EXPECT_EQ(seen.size(), 6u);
}
