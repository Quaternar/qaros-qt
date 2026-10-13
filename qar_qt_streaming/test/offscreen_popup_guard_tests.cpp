// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The off-screen popup guard (offscreen_popup_guard.hpp): every popup an OFF-SCREEN panel
// opens - a combo box list, a menu, a completer list - has to stay off the desktop, show up
// only in the panel's AR image and be operable from AR, while a visible panel's popups
// behave exactly as without the guard.
//
// Headless under the offscreen platform plugin, which has no desktop. "Not a desktop window"
// is therefore asserted through the platform-independent witness of a mapped native window:
// the popup's QWindow::isVisible(), which Qt sets only when it asks the platform to show the
// window and never for a widget carrying Qt::WA_DontShowOnScreen. Nothing here asserts on
// rendered text: the plugin rasterizes no glyphs, so popups are given known colours.
//
// The guard is installed once per process and its counters only grow, so counters are
// compared as deltas and no test assumes the guard was, or was not, installed before it.

#include <gtest/gtest.h>

// Project headers before any Qt header: panel_pointer_state.hpp declares a member named
// emit(), which Qt's `emit` keyword macro breaks if it is seen first. The guard header comes
// before the manager's on purpose: it must be usable in that position.
#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_qt_streaming/offscreen_popup_guard.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/qt_widget_capture.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPalette>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

using qar_ar_core::PanelPointerSample;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_qt_streaming::install_offscreen_popup_guard;
using qar_qt_streaming::is_offscreen_popup;
using qar_qt_streaming::offscreen_popup_guard_installed;
using qar_qt_streaming::offscreen_popup_guard_stats;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::popup_global_origin;
using qar_qt_streaming::popup_owner_widget;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::QtWidgetCapture;
using qar_qt_streaming::show_offscreen;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;
using qar_qt_streaming::test_support::RecordedPanelFrame;

namespace
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

PanelStreamOptions
options_at()
{
  PanelStreamOptions options;
  options.max_fps = 0;
  options.keepalive_seconds = 0.0;
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  options.placement.pose.position = QarVector3{0.0f, 0.0f, 0.0f};
  return options;
}

QtPanelStreamingConfig
make_config(std::shared_ptr<MockQarPanelSenderFactory> factory)
{
  QtPanelStreamingConfig config;
  config.panel_sender_factory = std::move(factory);
  config.clock = std::make_shared<ManualClock>();
  return config;
}

PanelPointerSample
tap(const std::string & key, float u, float v)
{
  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_panel_uv = true;
  sample.panel_key = key;
  sample.u = u;
  sample.v = v;
  return sample;
}

/// Process events for at least @p ms milliseconds (0: just drain what is queued).
void
pump(int ms = 0)
{
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  } while (timer.elapsed() < ms);
  QCoreApplication::processEvents();
}

/// QComboBox ignores a release on its list for this long after opening it, so a selection
/// tap has to come later than that.
void
wait_for_combo_release_block()
{
  pump(QApplication::doubleClickInterval() + 100);
}

bool
pixel_is(const RecordedPanelFrame & frame, int x, int y, int r, int g, int b)
{
  if (x < 0 || y < 0 || x >= static_cast<int>(frame.width) || y >= static_cast<int>(frame.height)) {
    return false;
  }
  const std::size_t i =
    (static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)) * 4u;
  return frame.rgba[i] == r && frame.rgba[i + 1] == g && frame.rgba[i + 2] == b &&
         frame.rgba[i + 3] == 255;
}

bool
is_black(const RecordedPanelFrame & frame, int x, int y)
{
  return pixel_is(frame, x, y, 0, 0, 0);
}

bool
is_magenta(const RecordedPanelFrame & frame, int x, int y)
{
  return pixel_is(frame, x, y, 255, 0, 255);
}

void
paint_magenta(QWidget * widget)
{
  widget->setAutoFillBackground(true);
  QPalette palette = widget->palette();
  palette.setColor(QPalette::Window, Qt::magenta);
  palette.setColor(QPalette::Base, Qt::magenta);
  widget->setPalette(palette);
}

/// A black, parentless widget: the colour of a popup composited over it is the only thing
/// that can be non-black.
QWidget *
make_black_panel()
{
  auto * panel = new QWidget();
  panel->setAutoFillBackground(true);
  QPalette palette = panel->palette();
  palette.setColor(QPalette::Window, Qt::black);
  panel->setPalette(palette);
  return panel;
}

/// Did Qt ask the platform to make a native window for @p widget visible? False when the
/// widget has no QWindow at all.
bool
native_window_visible(const QWidget * widget)
{
  const QWindow * window = widget->windowHandle();
  return window != nullptr && window->isVisible();
}

/// The visible top-level window whose Qt class is, or derives from, @p class_name.
QWidget *
visible_top_level_inheriting(const char * class_name)
{
  const QWidgetList tops = QApplication::topLevelWidgets();
  for (QWidget * top : tops) {
    if (top != nullptr && top->isVisible() && top->inherits(class_name)) {
      return top;
    }
  }
  return nullptr;
}

/// The menu animation switched on or off for one scope; the process-wide setting is
/// restored afterwards, so one test cannot change what the next one sees.
class MenuAnimation
{
public:
  explicit MenuAnimation(bool enabled)
  : general_(QApplication::isEffectEnabled(Qt::UI_General)),
    animate_(QApplication::isEffectEnabled(Qt::UI_AnimateMenu)),
    fade_(QApplication::isEffectEnabled(Qt::UI_FadeMenu))
  {
    if (enabled) {
      QApplication::setEffectEnabled(Qt::UI_General, true);
    }
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, enabled);
    QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
  }

  ~MenuAnimation()
  {
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, animate_);
    QApplication::setEffectEnabled(Qt::UI_FadeMenu, fade_);
    QApplication::setEffectEnabled(Qt::UI_General, general_);
  }

  MenuAnimation(const MenuAnimation &) = delete;
  MenuAnimation & operator=(const MenuAnimation &) = delete;

private:
  bool general_;
  bool animate_;
  bool fade_;
};

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

/// A black panel holding one combo box. Shown off-screen by default; shown the ordinary
/// way (the control) when @p offscreen is false.
class ComboPanel
{
public:
  explicit ComboPanel(bool offscreen = true, const QRect & combo_rect = QRect(20, 20, 160, 28))
  {
    widget_ = make_black_panel();
    combo_ = new QComboBox(widget_);
    combo_->addItems({"zero", "one", "two", "three"});
    combo_->setGeometry(combo_rect);
    if (offscreen) {
      EXPECT_TRUE(show_offscreen(widget_, kSide, kSide));
    } else {
      widget_->resize(kSide, kSide);
      widget_->show();
    }
    widget_->ensurePolished();
    pump();
  }

  ~ComboPanel()
  {
    combo_->hidePopup();
    delete widget_;
  }

  ComboPanel(const ComboPanel &) = delete;
  ComboPanel & operator=(const ComboPanel &) = delete;

  QWidget * widget() const { return widget_; }
  QComboBox * combo() const { return combo_; }
  QPoint combo_center() const { return combo_->geometry().center(); }

  static constexpr int kSide = 300;

private:
  QWidget * widget_ = nullptr;
  QComboBox * combo_ = nullptr;
};

/// A panel streamed through the real manager, so an AR tap takes the production path (hit
/// test, input injector, Qt). Declare it AFTER the panel it streams: the manager must be
/// gone before the panel is.
class ArStream
{
public:
  ArStream(QWidget * panel, const std::string & key)
  : key_(key), panel_(panel), manager_(make_config(factory_))
  {
    EXPECT_TRUE(manager_.add_panel(key_, panel_, options_at()).ok());
    // The first tick derives the panel's logical size, which a tap is normalised against,
    // and sends the closed frame.
    manager_.tick();
  }

  ArStream(const ArStream &) = delete;
  ArStream & operator=(const ArStream &) = delete;

  QtPanelStreamManager & manager() { return manager_; }

  /// Every frame the sender was given, oldest first.
  const std::vector<RecordedPanelFrame> & frames() const { return factory_->latest()->frames(); }

  /// One AR tap at panel-local logical pixel @p p.
  bool tap_at(const QPoint & p)
  {
    const float u = (static_cast<float>(p.x()) + 0.5f) / static_cast<float>(panel_->width());
    const float v = (static_cast<float>(p.y()) + 0.5f) / static_cast<float>(panel_->height());
    const bool hit = manager_.handle_sample(tap(key_, u, v));
    pump();
    return hit;
  }

private:
  std::string key_;
  QWidget * panel_;
  std::shared_ptr<MockQarPanelSenderFactory> factory_ =
    std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStreamManager manager_;
};

/// The panel-local centre of row @p row of the open list @p list of @p combo, through the
/// origin Qt placed the list at.
QPoint
combo_row_in_panel(QComboBox * combo, QWidget * list, QWidget * panel, int row)
{
  QAbstractItemView * view = combo->view();
  const QRect cell = view->visualRect(combo->model()->index(row, 0));
  const QPoint in_list = view->viewport()->mapTo(list, cell.center());
  return panel->mapFromGlobal(popup_global_origin(list) + in_list);
}

/// A property tree whose value cell opens an EDITABLE combo box as its editor, the way
/// rviz_common's EditableComboBox does (a completer in popup mode included).
class ComboEditorDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QWidget * createEditor(
    QWidget * parent, const QStyleOptionViewItem &, const QModelIndex & index) const override
  {
    if (index.column() != 1) {
      return nullptr;
    }
    auto * box = new QComboBox(parent);
    box->setEditable(true);
    if (box->completer() != nullptr) {
      box->completer()->setCompletionMode(QCompleter::PopupCompletion);
      box->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    }
    box->addItems({"map", "odom", "base_link", "laser"});
    paint_magenta(box->view());
    return box;
  }

  void setEditorData(QWidget * editor, const QModelIndex & index) const override
  {
    static_cast<QComboBox *>(editor)->setEditText(index.data().toString());
  }

  void setModelData(
    QWidget * editor, QAbstractItemModel * model, const QModelIndex & index) const override
  {
    model->setData(index, static_cast<QComboBox *>(editor)->currentText());
  }
};

/// A black off-screen panel holding a button whose menu (three actions) is owned by the
/// button, the way a toolbar-style menu button is.
class MenuPanel
{
public:
  MenuPanel()
  {
    widget_ = make_black_panel();
    button_ = new QPushButton("Menu", widget_);
    button_->setGeometry(10, 10, 100, 30);
    menu_ = new QMenu(button_);
    menu_->addAction("first");
    second_ = menu_->addAction("second");
    menu_->addAction("third");
    QObject::connect(menu_, &QMenu::triggered, [this](QAction * action) { triggered_ = action; });
    EXPECT_TRUE(show_offscreen(widget_, 300, 300));
    widget_->ensurePolished();
    pump();
  }

  ~MenuPanel()
  {
    menu_->close();
    delete widget_;
  }

  MenuPanel(const MenuPanel &) = delete;
  MenuPanel & operator=(const MenuPanel &) = delete;

  QWidget * widget() const { return widget_; }
  QMenu * menu() const { return menu_; }
  QAction * second() const { return second_; }
  QAction * triggered() const { return triggered_; }

  /// Where the menu is asked to appear: just below the button, in global coordinates.
  QPoint requested_position() const { return button_->mapToGlobal(QPoint(0, button_->height())); }

private:
  QWidget * widget_ = nullptr;
  QPushButton * button_ = nullptr;
  QMenu * menu_ = nullptr;
  QAction * second_ = nullptr;
  QAction * triggered_ = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------
// The guard
// ---------------------------------------------------------------------------

TEST(OffscreenPopupGuard, ShowOffscreenInstallsTheGuardExactlyOnce)
{
  ComboPanel panel;  // show_offscreen installs the guard
  EXPECT_TRUE(offscreen_popup_guard_installed());
  EXPECT_TRUE(install_offscreen_popup_guard()) << "a repeated install still succeeds";
  EXPECT_TRUE(offscreen_popup_guard_installed());

  // One filter per application: installing again must not add a second object to it.
  const int objects = QCoreApplication::instance()->children().size();
  EXPECT_TRUE(install_offscreen_popup_guard());
  EXPECT_EQ(QCoreApplication::instance()->children().size(), objects);

  const auto before = offscreen_popup_guard_stats();
  panel.combo()->showPopup();
  pump();
  ASSERT_NE(QApplication::activePopupWidget(), nullptr);
  const auto after = offscreen_popup_guard_stats();
  EXPECT_EQ(after.kept_popups - before.kept_popups, 1u) << "one list is kept off once";
}

TEST(OffscreenPopupGuard, AnArTapOpensAComboListThatNeverBecomesADesktopWindow)
{
  ComboPanel panel;
  ArStream stream(panel.widget(), "off");
  const auto before = offscreen_popup_guard_stats();

  ASSERT_TRUE(stream.tap_at(panel.combo_center()));
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr) << "the AR tap did not open the combo's list";

  EXPECT_TRUE(is_offscreen_popup(list));
  EXPECT_TRUE(list->testAttribute(Qt::WA_DontShowOnScreen));
  EXPECT_TRUE(list->isVisible()) << "Qt still treats the list as shown: input and render work";
  EXPECT_FALSE(native_window_visible(list)) << "Qt mapped the list as a desktop window";
  EXPECT_FALSE(
    list->windowHandle() != nullptr && QGuiApplication::focusWindow() == list->windowHandle())
    << "the list must not take the application's focus window";
  EXPECT_GE(offscreen_popup_guard_stats().kept_popups, before.kept_popups + 1);
}

TEST(OffscreenPopupGuard, TheListIsCompositedWhereQtPlacedItAndAnArTapSelectsARow)
{
  ComboPanel panel;
  ArStream stream(panel.widget(), "off");
  const std::size_t closed_frames = stream.frames().size();
  ASSERT_EQ(closed_frames, 1u) << "the closed combo, sent once";

  ASSERT_TRUE(stream.tap_at(panel.combo_center()));
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr);
  ASSERT_TRUE(is_offscreen_popup(list));
  // This host rasterizes no glyphs, so the list is given a known colour to be found.
  paint_magenta(list);
  paint_magenta(panel.combo()->view());
  pump();

  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(300, 300, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.composited_popup_count(), 1u);

  stream.manager().tick();
  ASSERT_EQ(stream.frames().size(), closed_frames + 1) << "the open list never reached the stream";
  const RecordedPanelFrame & open = stream.frames().back();
  const QPoint local = panel.widget()->mapFromGlobal(popup_global_origin(list));
  EXPECT_TRUE(is_magenta(open, local.x() + list->width() - 6, local.y() + list->height() - 4))
    << "the list is not drawn where Qt placed it, local origin (" << local.x() << ", " << local.y()
    << ")";
  EXPECT_TRUE(is_black(open, local.x() - 1, local.y() + 4)) << "the list bled past its left edge";

  // Select row 2 from AR.
  wait_for_combo_release_block();
  const QPoint row2 = combo_row_in_panel(panel.combo(), list, panel.widget(), 2);
  ASSERT_TRUE(stream.tap_at(row2));
  pump(150);  // hidePopup's flash runs a short nested loop
  EXPECT_EQ(panel.combo()->currentIndex(), 2) << "the AR tap on row 2 did not select it";
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr) << "a selection closes the list";
  EXPECT_FALSE(list->isVisible());
}

TEST(OffscreenPopupGuard, AListReopenedElsewhereIsDrawnThereAndATapBesideItClosesIt)
{
  ComboPanel panel;
  ArStream stream(panel.widget(), "off");

  ASSERT_TRUE(stream.tap_at(panel.combo_center()));
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr);
  const QPoint first_origin = popup_global_origin(list);
  panel.combo()->hidePopup();
  pump(100);
  ASSERT_EQ(QApplication::activePopupWidget(), nullptr);

  panel.combo()->move(100, 150);
  pump();
  ASSERT_TRUE(stream.tap_at(panel.combo_center()));
  list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr) << "the list did not open again";
  ASSERT_TRUE(is_offscreen_popup(list));

  // Where the list lives now is its widget geometry, not wherever it opened first.
  const QPoint origin = popup_global_origin(list);
  EXPECT_EQ(origin, list->geometry().topLeft());
  EXPECT_NE(origin, first_origin);
  paint_magenta(list);
  paint_magenta(panel.combo()->view());
  pump();
  stream.manager().tick();
  const RecordedPanelFrame & open = stream.frames().back();
  const QPoint local = panel.widget()->mapFromGlobal(origin);
  EXPECT_TRUE(is_magenta(open, local.x() + list->width() - 6, local.y() + list->height() - 4))
    << "the reopened list is not drawn at its new place, local origin (" << local.x() << ", "
    << local.y() << ")";
  const QPoint first_local = panel.widget()->mapFromGlobal(first_origin);
  EXPECT_TRUE(is_black(open, first_local.x() + 2, first_local.y() + 2))
    << "the list is still drawn where it opened first";

  // A tap on the panel background, beside the list, closes it without selecting anything.
  wait_for_combo_release_block();
  ASSERT_TRUE(stream.tap_at(QPoint(5, 295)));
  pump(50);
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
  EXPECT_EQ(panel.combo()->currentIndex(), 0) << "a tap beside the list selected a row";
}

TEST(OffscreenPopupGuard, TheRvizCaseAPropertyTreeComboEditorOpensFromItsArrowAndSelects)
{
  std::unique_ptr<QWidget> panel(make_black_panel());
  auto * layout = new QVBoxLayout(panel.get());
  layout->setContentsMargins(0, 0, 0, 0);
  auto * tree = new QTreeWidget(panel.get());
  tree->setColumnCount(2);
  tree->setHeaderHidden(true);
  tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
  tree->setItemDelegateForColumn(1, new ComboEditorDelegate(tree));
  auto * global = new QTreeWidgetItem(tree, QStringList({"Global Options", ""}));
  auto * fixed = new QTreeWidgetItem(global, QStringList({"Fixed Frame", "map"}));
  fixed->setFlags(fixed->flags() | Qt::ItemIsEditable);
  layout->addWidget(tree);
  tree->expandAll();
  ASSERT_TRUE(show_offscreen(panel.get(), 480, 720));
  pump();
  tree->setColumnWidth(0, 230);
  pump();
  EXPECT_FALSE(panel->isActiveWindow()) << "an off-screen panel is never the active window";

  ArStream stream(panel.get(), "tree");

  // Tap 1: the value cell turns into the editable combo editor.
  const QModelIndex value = tree->model()->index(0, 1, tree->model()->index(0, 0));
  const QPoint cell_in_panel =
    tree->viewport()->mapTo(panel.get(), tree->visualRect(value).center());
  ASSERT_TRUE(stream.tap_at(cell_in_panel));
  auto * editor = qobject_cast<QComboBox *>(tree->indexWidget(value));
  if (editor == nullptr) {
    editor = tree->findChild<QComboBox *>();
  }
  ASSERT_TRUE(editor != nullptr && editor->isVisible())
    << "the first AR tap did not open the editor";

  // Tap 2: its arrow - a tap on the text of an editable combo only places a text cursor.
  QStyleOptionComboBox option;
  option.initFrom(editor);
  option.editable = true;
  option.subControls = QStyle::SC_All;
  const QRect arrow =
    editor->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, editor);
  ASSERT_TRUE(stream.tap_at(editor->mapTo(panel.get(), arrow.center())));
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr) << "the AR tap on the arrow did not open the drop-down";
  EXPECT_TRUE(is_offscreen_popup(list));
  EXPECT_FALSE(native_window_visible(list)) << "the drop-down became a desktop window";

  stream.manager().tick();
  const RecordedPanelFrame & open = stream.frames().back();
  const QPoint local = panel->mapFromGlobal(popup_global_origin(list));
  EXPECT_TRUE(is_magenta(open, local.x() + list->width() - 6, local.y() + list->height() - 4))
    << "the drop-down is not composited where Qt placed it, local origin (" << local.x() << ", "
    << local.y() << ")";

  // Pick 'odom' from AR.
  wait_for_combo_release_block();
  ASSERT_TRUE(stream.tap_at(combo_row_in_panel(editor, list, panel.get(), 1)));
  pump(150);
  EXPECT_EQ(editor->currentText(), QStringLiteral("odom"));
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
}

TEST(OffscreenPopupGuard, AMenuIsKeptOffNotClampedToTheDesktopAndTriggeredFromAr)
{
  const MenuAnimation animation(false);
  MenuPanel panel;
  ArStream stream(panel.widget(), "menu");

  const QPoint requested = panel.requested_position();
  panel.menu()->popup(requested);
  pump();
  ASSERT_EQ(QApplication::activePopupWidget(), panel.menu()) << "the menu did not open";
  EXPECT_TRUE(is_offscreen_popup(panel.menu()));
  EXPECT_FALSE(native_window_visible(panel.menu())) << "the menu became a desktop window";
  EXPECT_EQ(panel.menu()->geometry().topLeft(), requested) << "the menu was clamped to the desktop";

  QtWidgetCapture capture(panel.widget());
  ASSERT_TRUE(capture.capture(300, 300, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.composited_popup_count(), 1u);

  const QPoint action_in_panel = panel.widget()->mapFromGlobal(
    popup_global_origin(panel.menu()) + panel.menu()->actionGeometry(panel.second()).center());
  ASSERT_TRUE(stream.tap_at(action_in_panel));
  pump(100);
  EXPECT_EQ(panel.triggered(), panel.second()) << "the AR tap did not trigger the second action";
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr) << "triggering an action closes the menu";
}

TEST(OffscreenPopupGuard, AMenuPolishedBeforeItWasOwnedIsKeptOffWhenShown)
{
  const MenuAnimation animation(false);
  std::unique_ptr<QWidget> panel(make_black_panel());
  auto * anchor = new QWidget(panel.get());
  anchor->setGeometry(10, 10, 50, 20);
  ASSERT_TRUE(show_offscreen(panel.get(), 300, 300));
  pump();

  // Polished while it belongs to nobody: the guard has nothing to go on yet.
  auto * menu = new QMenu();
  menu->addAction("one");
  menu->addAction("two");
  menu->ensurePolished();
  EXPECT_FALSE(is_offscreen_popup(menu)) << "a parentless menu is nobody's";

  // Parented into the panel afterwards, it is caught when it is shown.
  menu->setParent(anchor, Qt::Popup);
  menu->popup(anchor->mapToGlobal(QPoint(0, 20)));
  pump();
  ASSERT_EQ(QApplication::activePopupWidget(), menu);
  EXPECT_TRUE(is_offscreen_popup(menu));
  ASSERT_NE(menu->windowHandle(), nullptr);
  EXPECT_FALSE(menu->windowHandle()->isVisible()) << "the menu was mapped before it was kept off";

  menu->close();
  pump();
}

TEST(OffscreenPopupGuard, TheMenuAnimationWindowIsKeptOffToo)
{
  const MenuAnimation animation(true);
  ASSERT_TRUE(QApplication::isEffectEnabled(Qt::UI_AnimateMenu)) << "the menu animation is not on";
  MenuPanel panel;
  const auto before = offscreen_popup_guard_stats();

  panel.menu()->popup(panel.requested_position());

  // Qt shows a separate window that rolls the menu out; it must stay off the desktop too.
  QWidget * effect = visible_top_level_inheriting("QRollEffect");
  ASSERT_NE(effect, nullptr) << "no menu animation window";
  EXPECT_TRUE(effect->testAttribute(Qt::WA_DontShowOnScreen));
  EXPECT_FALSE(native_window_visible(effect)) << "the animation window reached the desktop";
  EXPECT_EQ(offscreen_popup_guard_stats().kept_effects, before.kept_effects + 1);

  // When the animation ends the real menu is shown, and it is kept off as well.
  QElapsedTimer timer;
  timer.start();
  while (QApplication::activePopupWidget() != panel.menu() && timer.elapsed() < 2000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  pump(50);
  ASSERT_EQ(QApplication::activePopupWidget(), panel.menu()) << "the menu never opened";
  EXPECT_TRUE(is_offscreen_popup(panel.menu()));
  EXPECT_FALSE(native_window_visible(panel.menu()));
}

TEST(OffscreenPopupGuard, ACompleterListIsOwnedThroughItsFocusProxyAndComposited)
{
  std::unique_ptr<QWidget> panel(make_black_panel());
  auto * edit = new QLineEdit(panel.get());
  edit->setGeometry(10, 10, 200, 26);
  auto * completer = new QCompleter(QStringList({"alpha", "alpine", "beta"}), edit);
  completer->setCompletionMode(QCompleter::PopupCompletion);
  edit->setCompleter(completer);
  ASSERT_TRUE(show_offscreen(panel.get(), 300, 300));
  pump();

  completer->setCompletionPrefix("al");
  completer->complete();
  pump();
  QWidget * list = completer->popup();
  ASSERT_TRUE(list != nullptr && list->isVisible()) << "the completer list did not open";

  // Qt re-parents the list to nothing and points it at the line edit.
  EXPECT_EQ(list->parentWidget(), nullptr);
  EXPECT_EQ(list->focusProxy(), edit);
  EXPECT_EQ(popup_owner_widget(list), edit);
  EXPECT_TRUE(is_offscreen_popup(list));
  EXPECT_FALSE(native_window_visible(list)) << "the completer list became a desktop window";

  QtWidgetCapture capture(panel.get());
  ASSERT_TRUE(capture.capture(300, 300, 1.0)) << capture.last_error();
  EXPECT_EQ(capture.composited_popup_count(), 1u) << "a parentless list was not attributed";

  list->hide();
  pump();
}

TEST(OffscreenPopupGuard, StoppingTheStreamClosesItsOffscreenPopups)
{
  ComboPanel panel;
  ArStream stream(panel.widget(), "off");

  ASSERT_TRUE(stream.tap_at(panel.combo_center()));
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr);
  ASSERT_TRUE(is_offscreen_popup(list));

  ASSERT_TRUE(stream.manager().close_panel("off", "stopped").ok());
  pump();
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr)
    << "an invisible list must not keep the application in popup mode";
  EXPECT_FALSE(list->isVisible());
}

// ---------------------------------------------------------------------------
// Controls: what must NOT change
// ---------------------------------------------------------------------------

TEST(OffscreenPopupGuard, ControlAVisiblePanelsListIsADesktopWindowAsBefore)
{
  ASSERT_TRUE(install_offscreen_popup_guard());
  ComboPanel panel(false);
  ArStream stream(panel.widget(), "visible");

  ASSERT_TRUE(stream.tap_at(panel.combo_center()));
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr) << "the AR tap did not open the combo's list";
  EXPECT_FALSE(is_offscreen_popup(list));
  EXPECT_FALSE(list->testAttribute(Qt::WA_DontShowOnScreen));
  EXPECT_TRUE(native_window_visible(list)) << "a visible panel's list is an ordinary window";
}

TEST(OffscreenPopupGuard, ControlAListInADialogOfTheOffscreenPanelIsLeftAlone)
{
  ASSERT_TRUE(install_offscreen_popup_guard());
  std::unique_ptr<QWidget> panel(make_black_panel());
  auto * inner = new QWidget(panel.get());
  inner->setGeometry(0, 0, 100, 100);
  ASSERT_TRUE(show_offscreen(panel.get(), 300, 300));
  pump();

  // Parented into the panel's tree, as RViz dialogs can be: the dialog is a visible window
  // of its own, so what it opens is not the off-screen panel's popup.
  auto * dialog = new QDialog(inner);
  auto * combo = new QComboBox(dialog);
  combo->addItems({"x", "y"});
  combo->setGeometry(10, 10, 120, 26);
  dialog->resize(200, 100);
  dialog->show();
  pump();

  combo->showPopup();
  pump();
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr) << "the dialog's list did not open";
  EXPECT_FALSE(is_offscreen_popup(list));
  EXPECT_FALSE(list->testAttribute(Qt::WA_DontShowOnScreen));

  combo->hidePopup();
  pump(100);
  dialog->close();
}

TEST(OffscreenPopupGuard, ControlAParentlessMenuIsLeftAlone)
{
  ASSERT_TRUE(install_offscreen_popup_guard());
  const MenuAnimation animation(false);

  // No parent and no focus proxy: nobody owns it, so it is not any panel's popup.
  QMenu menu;
  menu.addAction("one");
  menu.popup(QPoint(50, 50));
  pump();
  ASSERT_EQ(QApplication::activePopupWidget(), &menu) << "the menu did not open";
  EXPECT_FALSE(is_offscreen_popup(&menu));
  EXPECT_FALSE(menu.testAttribute(Qt::WA_DontShowOnScreen));

  menu.close();
  pump();
}

TEST(OffscreenPopupGuard, ControlStoppingAVisiblePanelsStreamLeavesItsListOpen)
{
  ASSERT_TRUE(install_offscreen_popup_guard());
  ComboPanel panel(false);
  ArStream stream(panel.widget(), "visible");

  ASSERT_TRUE(stream.tap_at(panel.combo_center()));
  QWidget * list = QApplication::activePopupWidget();
  ASSERT_NE(list, nullptr);

  ASSERT_TRUE(stream.manager().close_panel("visible", "stopped").ok());
  pump();
  EXPECT_EQ(QApplication::activePopupWidget(), list)
    << "a desktop popup is not this stream's to close";
  EXPECT_TRUE(list->isVisible());

  panel.combo()->hidePopup();
  pump(100);
}
