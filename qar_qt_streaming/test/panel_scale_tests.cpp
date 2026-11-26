// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The scale of a stream: how many metres one logical pixel covers and how sharp the capture is
// (QtPanelStream::set_scale, QtPanelStreamManager::set_panel_scale). A change of the metres alone
// reaches QarOS as a placement and rebuilds nothing; a change of the buffer goes through the resize
// hysteresis and the configured resize mode; a contact is mapped through the pixels of the
// content, so it lands on the same widget pixel at every scale; a dialog panel reads the scale of
// its owner and is stepped away from it by the owner's scale.

#include <gtest/gtest.h>

#include "qar_qt_streaming/enlarged_controls_style.hpp"
#include "qar_qt_streaming/panel_geometry.hpp"
#include "qar_qt_streaming/panel_resize_mode.hpp"
#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/test_support/mock_panel_sender.hpp"

#include <QColorDialog>
#include <QCoreApplication>
#include <QDialog>
#include <QInputDialog>
#include <QPalette>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace seam = qar_qt_streaming::seam;

using qar_qt_streaming::kDefaultMetersPerLogicalPx;
using qar_qt_streaming::PanelCloseMode;
using qar_qt_streaming::PanelFrame;
using qar_qt_streaming::PanelOverlayAttribution;
using qar_qt_streaming::PanelResizeMode;
using qar_qt_streaming::PanelStreamOptions;
using qar_qt_streaming::QtPanelStream;
using qar_qt_streaming::QtPanelStreamingConfig;
using qar_qt_streaming::QtPanelStreamManager;
using qar_qt_streaming::seam::PanelPointerSample;
using qar_qt_streaming::seam::PointerGesture;
using qar_qt_streaming::seam::PointerPhase;
using qar_qt_streaming::test_support::ManualClock;
using qar_qt_streaming::test_support::MockQarPanelSenderFactory;

namespace
{

/// A streamable top-level widget of one blue block, shown so that render() draws it.
class BlockWidget
{
public:
  BlockWidget(int width, int height)
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    layout->setContentsMargins(0, 0, 0, 0);
    auto * block = new QWidget(widget_);
    block->setAutoFillBackground(true);
    QPalette palette = block->palette();
    palette.setColor(QPalette::Window, Qt::blue);
    block->setPalette(palette);
    layout->addWidget(block);
    widget_->resize(width, height);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~BlockWidget() {delete widget_;}

  BlockWidget(const BlockWidget &) = delete;
  BlockWidget & operator=(const BlockWidget &) = delete;

  QWidget * get() const {return widget_;}

private:
  QWidget * widget_ = nullptr;
};

/// A 200 x 120 panel with one spin box at a known place, so that a contact can aim at its arrow.
class SpinPanel
{
public:
  SpinPanel()
  {
    widget_ = new QWidget();
    spin_ = new QSpinBox(widget_);
    spin_->setRange(0, 100);
    spin_->setValue(0);
    spin_->setGeometry(60, 40, 100, 32);
    widget_->resize(kWidth, kHeight);
    widget_->show();
    widget_->ensurePolished();
    QCoreApplication::processEvents();
  }

  ~SpinPanel() {delete widget_;}

  SpinPanel(const SpinPanel &) = delete;
  SpinPanel & operator=(const SpinPanel &) = delete;

  QWidget * widget() const {return widget_;}
  QSpinBox * spin() const {return spin_;}

  /// The centre of the up arrow of the spin box, in the coordinates of the panel.
  QPoint up_arrow_centre() const
  {
    QStyleOptionSpinBox option;
    option.initFrom(spin_);
    option.rect = spin_->rect();
    option.subControls = QStyle::SC_All;
    option.buttonSymbols = QAbstractSpinBox::UpDownArrows;
    const QRect arrow =
      spin_->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, spin_);
    return spin_->mapTo(widget_, arrow.center());
  }

  static constexpr int kWidth = 200;
  static constexpr int kHeight = 120;

private:
  QWidget * widget_ = nullptr;
  QSpinBox * spin_ = nullptr;
};

/// A 200 x 120 panel whose whole area is one button, so that a dialog can be parented to it.
class ButtonPanel
{
public:
  ButtonPanel()
  {
    widget_ = new QWidget();
    auto * layout = new QVBoxLayout(widget_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QPushButton("Go", widget_));
    widget_->resize(200, 120);
    widget_->show();
    widget_->ensurePolished();
    widget_->layout()->activate();
    QCoreApplication::processEvents();
  }

  ~ButtonPanel() {delete widget_;}

  ButtonPanel(const ButtonPanel &) = delete;
  ButtonPanel & operator=(const ButtonPanel &) = delete;

  QWidget * widget() const {return widget_;}

private:
  QWidget * widget_ = nullptr;
};

struct DialogGuard
{
  DialogGuard() {PanelOverlayAttribution::instance().reset();}
  ~DialogGuard() {PanelOverlayAttribution::instance().reset();}
};

PanelStreamOptions
options_no_pacing()
{
  PanelStreamOptions options;
  options.max_fps = 0;              // every tick captures
  options.keepalive_seconds = 0.0;  // isolate the dirty check
  options.placement.pose.orientation = seam::Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
  options.placement.pose.position = seam::Vector3{0.0f, 0.0f, 0.0f};
  return options;
}

/// A manager over the recording factory, ticked by hand on a clock that is advanced by hand.
struct Rig
{
  explicit Rig(
    PanelResizeMode resize = PanelResizeMode::Swap, bool dialogs = false,
    double dialog_offset_m = 0.5)
  {
    factory = std::make_shared<MockQarPanelSenderFactory>();
    factory->set_senders_support_detach(true);
    clock = std::make_shared<ManualClock>();
    QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.clock = clock;
    config.dialog_panels = dialogs;
    config.dialog_panel_offset_m = dialog_offset_m;
    config.close_mode = PanelCloseMode::Park;
    config.resize_mode = resize;
    config.ar_frame = PanelFrame{0.0, 0.0, 0.0, 0.0, 0.0};  // no frame
    manager = std::make_unique<QtPanelStreamManager>(config);
  }

  std::uint32_t tick(double dt = 0.1)
  {
    clock->advance(dt);
    return manager->tick();
  }

  /// Ticks long enough for a size that holds to be adopted (the settle time and the ticks).
  void settle()
  {
    for (int i = 0; i < 9; ++i) {
      tick(0.2);
    }
  }

  /// The snapshot entry of @p key (a default one when there is none).
  seam::PanelStreamDiagnostics panel(const std::string & key) const
  {
    for (const auto & entry : manager->diagnostics().panels) {
      if (entry.key == key) {
        return entry;
      }
    }
    return seam::PanelStreamDiagnostics{};
  }

  std::shared_ptr<MockQarPanelSenderFactory> factory;
  std::shared_ptr<ManualClock> clock;
  std::unique_ptr<QtPanelStreamManager> manager;
};

PanelPointerSample
click_at(const std::string & key, double u, double v, std::uint32_t pointer_id)
{
  PanelPointerSample sample;
  sample.pointer_id = pointer_id;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_panel_uv = true;
  sample.panel_key = key;
  sample.u = static_cast<float>(u);
  sample.v = static_cast<float>(v);
  return sample;
}

constexpr double kDefaultMetres = kDefaultMetersPerLogicalPx;

}  // namespace

TEST(PanelScale, MetresOnlyChangeIsAdoptedOnTheNextTickWithoutARebuild)
{
  Rig rig;
  BlockWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  const auto adapter = rig.factory->latest();
  ASSERT_NE(adapter, nullptr);
  const float width_before = adapter->desc().placement.size.width_meters;
  const float height_before = adapter->desc().placement.size.height_meters;
  ASSERT_NEAR(width_before, 200 * kDefaultMetres, 1e-4);
  const std::uint64_t rebuilds = rig.panel("p").adapter_rebuilds;
  ASSERT_EQ(adapter->placement_calls(), 0u);

  ASSERT_TRUE(rig.manager->set_panel_scale("p", 2.0 * kDefaultMetres, 1.0).ok());
  EXPECT_EQ(adapter->placement_calls(), 0u) << "set_panel_scale only stores the numbers";
  rig.tick();  // the content is unchanged, so no frame goes out

  EXPECT_EQ(rig.panel("p").adapter_rebuilds, rebuilds) << "the buffer is as it was: no rebuild";
  EXPECT_EQ(rig.factory->create_calls(), 1u);
  EXPECT_EQ(adapter->placement_calls(), 1u) << "one placement carries the size to QarOS";
  EXPECT_NEAR(adapter->placement().size.width_meters, 2.0f * width_before, 1e-4);
  EXPECT_NEAR(adapter->placement().size.height_meters, 2.0f * height_before, 1e-4);
  EXPECT_NEAR(rig.panel("p").width_meters, 2.0f * width_before, 1e-4);
  EXPECT_FALSE(rig.panel("p").resize_pending);
  // The pixels are as many as they were: only the metres changed.
  ASSERT_FALSE(adapter->frames().empty());
  EXPECT_EQ(adapter->frames().back().width, adapter->frames().front().width);
  EXPECT_EQ(adapter->frames().back().height, adapter->frames().front().height);

  // The same numbers again change nothing.
  ASSERT_TRUE(rig.manager->set_panel_scale("p", 2.0 * kDefaultMetres, 1.0).ok());
  rig.tick();
  EXPECT_EQ(adapter->placement_calls(), 1u);
}

TEST(PanelScale, RatioChangeGoesThroughTheResizeHysteresis)
{
  Rig rig;  // the default resize mode: the sender is swapped
  BlockWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  ASSERT_EQ(rig.panel("p").adapter_rebuilds, 1u);

  ASSERT_TRUE(rig.manager->set_panel_scale("p", kDefaultMetres, 2.0).ok());
  rig.tick(0.2);
  EXPECT_TRUE(rig.panel("p").resize_pending) << "a changed buffer has to hold first";
  EXPECT_EQ(rig.panel("p").pending_buffer_width, 400u);
  EXPECT_EQ(rig.panel("p").pending_buffer_height, 240u);
  EXPECT_EQ(rig.factory->create_calls(), 1u);
  EXPECT_EQ(rig.panel("p").buffer_width, 200u) << "the panel keeps streaming at the old layout";

  rig.settle();
  EXPECT_EQ(rig.factory->create_calls(), 2u) << "one rebuild after the settle";
  EXPECT_EQ(rig.panel("p").adapter_rebuilds, 2u);
  EXPECT_FALSE(rig.panel("p").resize_pending);
  EXPECT_EQ(rig.panel("p").buffer_width, 400u);
  EXPECT_EQ(rig.panel("p").buffer_height, 240u);
  // The metres did not change: the panel is as large in the room as it was.
  EXPECT_NEAR(rig.panel("p").width_meters, 200 * kDefaultMetres, 1e-4);
  EXPECT_EQ(rig.factory->latest()->desc().buffer_width, 400u);
  EXPECT_EQ(rig.factory->live_name_collisions(), 0u);
}

TEST(PanelScale, ARatioChangeInNewPanelModeHidesTheOldPanelAndKeepsIt)
{
  Rig rig(PanelResizeMode::NewPanel);
  BlockWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  ASSERT_EQ(rig.tick(), 1u);
  const auto old_panel = rig.factory->latest();
  ASSERT_NE(old_panel, nullptr);

  ASSERT_TRUE(rig.manager->set_panel_scale("p", 2.0 * kDefaultMetres, 2.0).ok());
  rig.settle();

  ASSERT_EQ(rig.factory->create_calls(), 2u);
  EXPECT_EQ(rig.manager->replaced_panel_count(), 1u);
  EXPECT_EQ(old_panel->last_state(), seam::PanelState::Hidden) << "the old panel is hidden";
  EXPECT_EQ(old_panel->close_calls(), 0u) << "and kept: nothing is closed";
  EXPECT_EQ(old_panel->detach_calls(), 0u);
  EXPECT_TRUE(old_panel->live());
  const auto fresh = rig.factory->latest();
  ASSERT_NE(fresh, old_panel);
  EXPECT_EQ(fresh->desc().buffer_width, 400u);
  EXPECT_EQ(fresh->desc().buffer_height, 240u);
  EXPECT_NEAR(fresh->desc().placement.size.width_meters, 400 * kDefaultMetres, 1e-4)
    << "the panel that shows the sharper video is as large as the scale says";
  EXPECT_NE(fresh->last_state(), seam::PanelState::Hidden);
  EXPECT_EQ(rig.manager->diagnostics().panels_closed, 0u);
  EXPECT_EQ(rig.factory->live_name_collisions(), 0u);
}

TEST(PanelScale, AContactLandsOnTheSameWidgetPixelAtEveryScale)
{
  // Factor 1 is the native panel; 1.5 and 2 cover that many times the metres per pixel and
  // capture at that many times the ratio. The contact is a fraction of the panel, and the widget
  // pixel it names does not depend on either.
  for (const double factor : {1.0, 1.5, 2.0}) {
    Rig rig;
    SpinPanel panel;
    ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
    ASSERT_EQ(rig.tick(), 1u);
    ASSERT_TRUE(rig.manager->set_panel_scale("p", factor * kDefaultMetres, factor).ok());
    rig.settle();
    ASSERT_EQ(rig.panel("p").buffer_width, static_cast<std::uint32_t>(std::lround(200 * factor)))
      << "factor " << factor;
    ASSERT_NEAR(rig.panel("p").width_meters, 200 * factor * kDefaultMetres, 1e-4);

    const QPoint arrow = panel.up_arrow_centre();
    ASSERT_GT(arrow.x(), 0);
    ASSERT_LT(arrow.x(), SpinPanel::kWidth);
    const double u = (arrow.x() + 0.5) / SpinPanel::kWidth;
    const double v = (arrow.y() + 0.5) / SpinPanel::kHeight;

    // Beside the spin box nothing is pressed; the centre of its arrow steps it.
    ASSERT_EQ(panel.spin()->value(), 0);
    rig.manager->handle_sample(click_at("p", 10.5 / SpinPanel::kWidth, v, 1));
    EXPECT_EQ(panel.spin()->value(), 0) << "factor " << factor;
    rig.manager->handle_sample(click_at("p", u, v, 2));
    EXPECT_EQ(panel.spin()->value(), 1) << "factor " << factor;
    rig.manager->handle_sample(click_at("p", u, v, 3));
    EXPECT_EQ(panel.spin()->value(), 2) << "factor " << factor;
  }
}

TEST(PanelScale, ADialogOpenedFromAScaledPanelInheritsItsScale)
{
  DialogGuard guard;
  Rig rig(PanelResizeMode::Swap, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  rig.tick();

  // A dialog that is open keeps the scale it was made with.
  auto * first = new QDialog(panel.widget());
  first->setWindowTitle("First");
  first->resize(240, 100);
  first->show();
  first->ensurePolished();
  QCoreApplication::processEvents();
  rig.tick();
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 1u);
  EXPECT_NEAR(rig.panel("dialog:First").width_meters, 240 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(rig.panel("dialog:First").device_pixel_ratio, 1.0, 1e-9);

  ASSERT_TRUE(rig.manager->set_panel_scale("p", 2.0 * kDefaultMetres, 2.0).ok());
  rig.settle();
  EXPECT_NEAR(rig.panel("dialog:First").width_meters, 240 * kDefaultMetres, 1e-4)
    << "an open dialog keeps its scale";
  EXPECT_NEAR(rig.panel("dialog:First").device_pixel_ratio, 1.0, 1e-9);

  // A dialog opened after the change reads the scale of its owner.
  auto * second = new QDialog(panel.widget());
  second->setWindowTitle("Second");
  second->resize(240, 100);
  second->show();
  second->ensurePolished();
  QCoreApplication::processEvents();
  rig.tick();
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 2u);
  EXPECT_NEAR(rig.panel("dialog:Second").width_meters, 240 * 2.0 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(rig.panel("dialog:Second").device_pixel_ratio, 2.0, 1e-9);
  EXPECT_EQ(rig.panel("dialog:Second").buffer_width, 480u);

  first->hide();
  second->hide();
  QCoreApplication::processEvents();
  rig.tick();
  delete first;
  delete second;
  QCoreApplication::processEvents();
}

TEST(PanelScale, TheDialogOffsetGrowsWithTheOwnersScale)
{
  DialogGuard guard;
  struct Case
  {
    double factor;
    double expected_step_m;
  };
  // The step is 0.5 m at the default metres per pixel and grows with the owner's metres; an owner
  // that covers fewer metres per pixel than the default keeps the step.
  for (const Case c : {Case{1.0, 0.5}, Case{1.5, 0.75}, Case{2.0, 1.0}, Case{0.5, 0.5}}) {
    Rig rig(PanelResizeMode::Swap, /*dialogs=*/true, /*dialog_offset_m=*/0.5);
    ButtonPanel panel;
    ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
    rig.tick();
    ASSERT_TRUE(rig.manager->set_panel_scale("p", c.factor * kDefaultMetres, 1.0).ok());

    auto * first = new QDialog(panel.widget());
    first->setWindowTitle("First");
    first->resize(240, 100);
    first->show();
    first->ensurePolished();
    QCoreApplication::processEvents();
    rig.tick();
    ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 1u) << "factor " << c.factor;
    EXPECT_NEAR(
      rig.factory->latest()->desc().placement.pose.position.x, c.expected_step_m, 1e-4)
      << "factor " << c.factor;

    // The second dialog of the same owner is one more step on.
    auto * second = new QDialog(panel.widget());
    second->setWindowTitle("Second");
    second->resize(240, 100);
    second->show();
    second->ensurePolished();
    QCoreApplication::processEvents();
    rig.tick();
    ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 2u) << "factor " << c.factor;
    EXPECT_NEAR(
      rig.factory->latest()->desc().placement.pose.position.x, 2.0 * c.expected_step_m, 1e-4)
      << "factor " << c.factor;

    first->hide();
    second->hide();
    QCoreApplication::processEvents();
    rig.tick();
    delete first;
    delete second;
    QCoreApplication::processEvents();
  }
}

TEST(PanelScale, OutOfRangeValuesAreRefused)
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  struct Case
  {
    double metres;
    double render;
  };
  const std::vector<Case> refused = {
    {0.0, 1.0}, {-0.001, 1.0}, {0.0101, 1.0}, {nan, 1.0}, {inf, 1.0},
    {0.0012, 0.99}, {0.0012, 4.01}, {0.0012, 0.0}, {0.0012, nan}, {0.0012, inf},
  };
  for (const Case & c : refused) {
    BlockWidget source(200, 120);
    auto factory = std::make_shared<MockQarPanelSenderFactory>();
    QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
    stream.set_scale(c.metres, c.render);
    EXPECT_EQ(stream.diagnostics().last_error, "scale out of range")
      << c.metres << " / " << c.render;
    EXPECT_EQ(stream.options().meters_per_logical_px, kDefaultMetres)
      << "a refused value changes nothing: " << c.metres << " / " << c.render;
    EXPECT_EQ(stream.options().render_scale, 1.0) << c.metres << " / " << c.render;
  }

  // The bounds themselves are in range, and the numbers are stored.
  BlockWidget source(200, 120);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  stream.set_scale(0.01, 4.0);
  EXPECT_TRUE(stream.diagnostics().last_error.empty());
  EXPECT_EQ(stream.options().meters_per_logical_px, 0.01);
  EXPECT_EQ(stream.options().render_scale, 4.0);
  stream.set_scale(0.0005, 1.0);
  EXPECT_TRUE(stream.diagnostics().last_error.empty());
  EXPECT_EQ(stream.options().meters_per_logical_px, 0.0005);
  EXPECT_EQ(stream.options().render_scale, 1.0);
}

TEST(PanelScale, UnknownKeyIsNotFound)
{
  Rig rig;
  BlockWidget source(200, 120);
  ASSERT_TRUE(rig.manager->add_panel("p", source.get(), options_no_pacing()).ok());
  rig.tick();

  const seam::Result missing = rig.manager->set_panel_scale("nobody", 0.002, 1.0);
  EXPECT_FALSE(missing.ok());
  EXPECT_EQ(missing.status, seam::Status::NotFound);
  EXPECT_NE(missing.message.find("nobody"), std::string::npos) << missing.message;
  EXPECT_TRUE(rig.manager->set_panel_scale("p", 0.002, 1.0).ok());
}

namespace
{

/// Opens a dialog of @p width x @p height on @p panel and ticks until its panel is made.
QDialog *
open_dialog(Rig & rig, ButtonPanel & panel, const char * title, int width = 240, int height = 100)
{
  auto * dialog = new QDialog(panel.widget());
  dialog->setWindowTitle(title);
  dialog->resize(width, height);
  dialog->show();
  dialog->ensurePolished();
  QCoreApplication::processEvents();
  rig.tick();
  return dialog;
}

void
close_dialogs(Rig & rig, std::vector<QDialog *> dialogs)
{
  for (QDialog * dialog : dialogs) {
    dialog->hide();
  }
  QCoreApplication::processEvents();
  rig.tick();
  for (QDialog * dialog : dialogs) {
    delete dialog;
  }
  QCoreApplication::processEvents();
}

using DialogScaleAnswer = std::optional<QtPanelStreamManager::DialogScale>;

}  // namespace

TEST(PanelScale, ADialogScaleProviderGivesTheDialogItsOwnScaleAndLeavesTheDialogAlone)
{
  DialogGuard guard;
  Rig rig(PanelResizeMode::Swap, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  rig.tick();
  // The owner is enlarged by 1.5, as the copies of the panels are.
  ASSERT_TRUE(rig.manager->set_panel_scale("p", 1.5 * kDefaultMetres, 1.5).ok());
  rig.settle();

  std::vector<std::pair<std::string, std::string>> asked;
  rig.manager->set_dialog_scale_provider(
    [&asked](const std::string & owner_key, const QWidget & dialog) -> DialogScaleAnswer {
      asked.emplace_back(owner_key, dialog.windowTitle().toStdString());
      return QtPanelStreamManager::DialogScale{2.0 * kDefaultMetres, 2.0};
    });

  QDialog * dialog = open_dialog(rig, panel, "Add");
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 1u);
  ASSERT_EQ(asked.size(), 1u);
  EXPECT_EQ(asked[0].first, "p");
  EXPECT_EQ(asked[0].second, "Add");

  // The dialog streams at 2x its desktop size, not at the 1.5x of its owner.
  const auto streamed = rig.panel("dialog:Add");
  EXPECT_NEAR(streamed.width_meters, 240 * 2.0 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(streamed.device_pixel_ratio, 2.0, 1e-9);
  EXPECT_EQ(streamed.buffer_width, 480u);
  EXPECT_NEAR(streamed.height_meters, 100 * 2.0 * kDefaultMetres, 1e-4);
  // The owner is untouched, and so is the dialog on the desktop.
  EXPECT_NEAR(rig.panel("p").width_meters, 200 * 1.5 * kDefaultMetres, 1e-4);
  EXPECT_EQ(dialog->size(), QSize(240, 100));

  close_dialogs(rig, {dialog});
}

TEST(PanelScale, ANativeAnswerGivesOneWhateverTheOwnerHas)
{
  DialogGuard guard;
  Rig rig(PanelResizeMode::Swap, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  rig.tick();
  ASSERT_TRUE(rig.manager->set_panel_scale("p", 2.0 * kDefaultMetres, 2.0).ok());
  rig.settle();
  rig.manager->set_dialog_scale_provider(
    [](const std::string &, const QWidget &) -> DialogScaleAnswer {
      return QtPanelStreamManager::DialogScale{kDefaultMetres, 1.0};
    });

  QDialog * dialog = open_dialog(rig, panel, "Native");
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 1u);
  EXPECT_NEAR(rig.panel("dialog:Native").width_meters, 240 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(rig.panel("dialog:Native").device_pixel_ratio, 1.0, 1e-9);
  EXPECT_EQ(rig.panel("dialog:Native").buffer_width, 240u);
  EXPECT_EQ(dialog->size(), QSize(240, 100));

  close_dialogs(rig, {dialog});
}

TEST(PanelScale, NoAnswerOrAnUnusableAnswerLeavesTheScaleOfTheOwner)
{
  DialogGuard guard;
  Rig rig(PanelResizeMode::Swap, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  rig.tick();
  ASSERT_TRUE(rig.manager->set_panel_scale("p", 1.5 * kDefaultMetres, 1.5).ok());
  rig.settle();

  int mode = 0;
  rig.manager->set_dialog_scale_provider(
    [&mode](const std::string &, const QWidget &) -> DialogScaleAnswer {
      switch (mode) {
        case 0:
          return std::nullopt;
        case 1:
          return QtPanelStreamManager::DialogScale{2.0 * kDefaultMetres, 0.5};  // render below 1
        case 2:
          return QtPanelStreamManager::DialogScale{std::numeric_limits<double>::quiet_NaN(), 2.0};
        case 3:
          return QtPanelStreamManager::DialogScale{0.02, 2.0};  // beyond a hundredth of a metre
        default:
          return QtPanelStreamManager::DialogScale{kDefaultMetres, 5.0};  // render above 4
      }
    });

  std::vector<QDialog *> dialogs;
  for (mode = 0; mode < 5; ++mode) {
    const std::string title = "Odd" + std::to_string(mode);
    dialogs.push_back(open_dialog(rig, panel, title.c_str()));
    ASSERT_EQ(rig.manager->dialog_panel_keys().size(), static_cast<std::size_t>(mode + 1));
    EXPECT_NEAR(rig.panel("dialog:" + title).width_meters, 240 * 1.5 * kDefaultMetres, 1e-4)
      << "answer " << mode;
    EXPECT_NEAR(rig.panel("dialog:" + title).device_pixel_ratio, 1.5, 1e-9) << "answer " << mode;
  }

  close_dialogs(rig, dialogs);
}

TEST(PanelScale, TheStepOfADialogUsesTheLargerOfTheScaleOfTheOwnerAndOfTheDialog)
{
  DialogGuard guard;
  struct Case
  {
    double owner_factor;
    double dialog_factor;
    double expected_step_m;
  };
  // 0.5 m at the default metres per pixel, grown by the larger factor of the two.
  for (const Case c : {Case{1.0, 2.0, 1.0}, Case{1.5, 2.0, 1.0}, Case{2.0, 1.0, 1.0},
      Case{1.0, 1.0, 0.5}, Case{0.5, 0.5, 0.5}})
  {
    Rig rig(PanelResizeMode::Swap, /*dialogs=*/true, /*dialog_offset_m=*/0.5);
    ButtonPanel panel;
    ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
    rig.tick();
    ASSERT_TRUE(rig.manager->set_panel_scale("p", c.owner_factor * kDefaultMetres, 1.0).ok());
    const double dialog_factor = c.dialog_factor;
    rig.manager->set_dialog_scale_provider(
      [dialog_factor](const std::string &, const QWidget &) -> DialogScaleAnswer {
        return QtPanelStreamManager::DialogScale{dialog_factor * kDefaultMetres, 1.0};
      });

    QDialog * first = open_dialog(rig, panel, "First");
    ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 1u);
    EXPECT_NEAR(
      rig.factory->latest()->desc().placement.pose.position.x, c.expected_step_m, 1e-4)
      << c.owner_factor << " / " << c.dialog_factor;
    QDialog * second = open_dialog(rig, panel, "Second");
    ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 2u);
    EXPECT_NEAR(
      rig.factory->latest()->desc().placement.pose.position.x, 2.0 * c.expected_step_m, 1e-4)
      << c.owner_factor << " / " << c.dialog_factor;

    close_dialogs(rig, {first, second});
  }
}

TEST(PanelScale, WithoutAProviderTheDialogReadsTheScaleOfItsOwnerAsBefore)
{
  DialogGuard guard;
  Rig rig(PanelResizeMode::Swap, /*dialogs=*/true);
  ButtonPanel panel;
  ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  rig.tick();
  ASSERT_TRUE(rig.manager->set_panel_scale("p", 1.5 * kDefaultMetres, 1.5).ok());
  rig.settle();

  QDialog * dialog = open_dialog(rig, panel, "Plain");
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 1u);
  EXPECT_NEAR(rig.panel("dialog:Plain").width_meters, 240 * 1.5 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(rig.panel("dialog:Plain").device_pixel_ratio, 1.5, 1e-9);

  // A provider that is taken away again leaves the next dialog with its owner's scale.
  rig.manager->set_dialog_scale_provider(
    [](const std::string &, const QWidget &) -> DialogScaleAnswer {
      return QtPanelStreamManager::DialogScale{2.0 * kDefaultMetres, 2.0};
    });
  rig.manager->set_dialog_scale_provider(nullptr);
  QDialog * later = open_dialog(rig, panel, "Later");
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 2u);
  EXPECT_NEAR(rig.panel("dialog:Later").device_pixel_ratio, 1.5, 1e-9);

  close_dialogs(rig, {dialog, later});
}

TEST(PanelScale, AWindowDrawnWithTheStyleOfACopyIsAnsweredByTheWindowNotByItsOwnersKey)
{
  DialogGuard guard;
  Rig rig(PanelResizeMode::Swap, /*dialogs=*/true);
  qar_qt_streaming::EnlargedControlsStyle style(2.0);
  // Two owners that look alike to a key-based rule: one is an AR copy (its tree has the style),
  // the other is not. RViz parents its colour picker and input boxes into the panel they came
  // from, and the spreader of the style gives them the style.
  ButtonPanel copy;
  ButtonPanel plain;
  qar_qt_streaming::apply_style_to_tree(copy.widget(), &style);
  ASSERT_TRUE(rig.manager->add_panel("copy", copy.widget(), options_no_pacing()).ok());
  ASSERT_TRUE(rig.manager->add_panel("plain", plain.widget(), options_no_pacing()).ok());
  rig.tick();

  std::vector<std::string> styled_titles;
  rig.manager->set_dialog_scale_provider(
    [&styled_titles](const std::string &, const QWidget & dialog) -> DialogScaleAnswer {
      if (qar_qt_streaming::enlarged_controls_style_of(&dialog) != nullptr) {
        styled_titles.push_back(dialog.windowTitle().toStdString());
        return QtPanelStreamManager::DialogScale{1.5 * kDefaultMetres, 1.5};  // the copy's size
      }
      return QtPanelStreamManager::DialogScale{2.0 * kDefaultMetres, 2.0};  // the whole panel
    });

  auto * picker = new QColorDialog(Qt::white, copy.widget());
  picker->setWindowTitle("Select Color");
  auto * asker = new QInputDialog(copy.widget());
  asker->setWindowTitle("Rename");
  auto * other = new QColorDialog(Qt::white, plain.widget());
  other->setWindowTitle("Other Color");
  const std::vector<QDialog *> dialogs{picker, asker, other};
  for (QDialog * dialog : dialogs) {
    dialog->show();
    dialog->ensurePolished();
  }
  QCoreApplication::processEvents();
  for (int i = 0; i < 4; ++i) {
    rig.tick();
  }
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 3u);
  ASSERT_EQ(styled_titles.size(), 2u);

  // The windows below the copy are streamed at the copy's size, once: x1.5, not x2.
  const auto picked = rig.panel("dialog:Select Color");
  EXPECT_NEAR(picked.width_meters, picker->width() * 1.5 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(picked.device_pixel_ratio, 1.5, 1e-9);
  const auto asked = rig.panel("dialog:Rename");
  EXPECT_NEAR(asked.width_meters, asker->width() * 1.5 * kDefaultMetres, 1e-4);
  // The same picker below a panel that is not an AR copy is enlarged as a whole.
  const auto unstyled = rig.panel("dialog:Other Color");
  EXPECT_NEAR(unstyled.width_meters, other->width() * 2.0 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(unstyled.device_pixel_ratio, 2.0, 1e-9);

  for (QDialog * dialog : dialogs) {
    dialog->hide();
  }
  QCoreApplication::processEvents();
  rig.tick();
  for (QDialog * dialog : dialogs) {
    delete dialog;
  }
  QCoreApplication::processEvents();
}

TEST(PanelScale, AParkedDialogShownAgainTakesTheNewAnswerAndTheReportSaysWhatItGot)
{
  DialogGuard guard;
  Rig rig(PanelResizeMode::Swap, /*dialogs=*/true, /*dialog_offset_m=*/0.5);
  ButtonPanel panel;
  ASSERT_TRUE(rig.manager->add_panel("p", panel.widget(), options_no_pacing()).ok());
  rig.tick();

  double factor = 1.0;  // native at first
  bool answer = true;
  rig.manager->set_dialog_scale_provider(
    [&factor, &answer](const std::string &, const QWidget &) -> DialogScaleAnswer {
      if (!answer) {
        return std::nullopt;
      }
      return QtPanelStreamManager::DialogScale{factor * kDefaultMetres, factor};
    });
  struct Report
  {
    std::string owner;
    std::string key;
    double metres;
    double render;
    bool revived;
  };
  std::vector<Report> reports;
  rig.manager->set_dialog_scale_reporter(
    [&reports](
      const std::string & owner_key, const QWidget &, const std::string & key,
      const QtPanelStreamManager::DialogScale & applied, bool revived) {
      reports.push_back(
        Report{owner_key, key, applied.meters_per_logical_px, applied.render_scale, revived});
    });

  auto * dialog = new QDialog(panel.widget());
  dialog->setWindowTitle("Add");
  dialog->resize(240, 100);
  dialog->show();
  dialog->ensurePolished();
  QCoreApplication::processEvents();
  rig.tick();
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 1u);
  ASSERT_EQ(reports.size(), 1u);
  EXPECT_EQ(reports[0].owner, "p");
  EXPECT_EQ(reports[0].key, "dialog:Add");
  EXPECT_FALSE(reports[0].revived);
  EXPECT_NEAR(reports[0].metres, kDefaultMetres, 1e-12);
  EXPECT_NEAR(reports[0].render, 1.0, 1e-12);
  EXPECT_NEAR(rig.panel("dialog:Add").width_meters, 240 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(rig.factory->latest()->desc().placement.pose.position.x, 0.5, 1e-4);

  // Dismissed: the slot is parked, nothing is closed.
  dialog->hide();
  QCoreApplication::processEvents();
  rig.tick();
  EXPECT_TRUE(rig.manager->dialog_panel_keys().empty());

  // The setting changed meanwhile; the same dialog is shown again and its slot is revived.
  factor = 2.0;
  dialog->show();
  QCoreApplication::processEvents();
  rig.tick();
  rig.settle();
  ASSERT_EQ(rig.manager->dialog_panel_keys().size(), 1u);
  ASSERT_EQ(reports.size(), 2u);
  EXPECT_TRUE(reports[1].revived);
  EXPECT_EQ(reports[1].key, "dialog:Add");
  EXPECT_NEAR(reports[1].metres, 2.0 * kDefaultMetres, 1e-12) << "the report is the stream's own";
  EXPECT_NEAR(reports[1].render, 2.0, 1e-12);
  EXPECT_NEAR(rig.panel("dialog:Add").width_meters, 240 * 2.0 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(rig.panel("dialog:Add").device_pixel_ratio, 2.0, 1e-9);
  EXPECT_NEAR(rig.factory->latest()->desc().placement.pose.position.x, 1.0, 1e-4)
    << "the step is worked out with the scale the panel takes";

  // No answer for a slot that is shown again: it keeps the scale it has, and so do the step and
  // the report.
  dialog->hide();
  QCoreApplication::processEvents();
  rig.tick();
  answer = false;
  dialog->show();
  QCoreApplication::processEvents();
  rig.tick();
  rig.settle();
  ASSERT_EQ(reports.size(), 3u);
  EXPECT_TRUE(reports[2].revived);
  EXPECT_NEAR(reports[2].metres, 2.0 * kDefaultMetres, 1e-12);
  EXPECT_NEAR(rig.panel("dialog:Add").width_meters, 240 * 2.0 * kDefaultMetres, 1e-4);
  EXPECT_NEAR(rig.factory->latest()->desc().placement.pose.position.x, 1.0, 1e-4);

  dialog->hide();
  QCoreApplication::processEvents();
  rig.tick();
  delete dialog;
  QCoreApplication::processEvents();
}

TEST(PanelScale, TheScaleLimitsAreOneSharedPair)
{
  using qar_qt_streaming::scale_within_limits;
  EXPECT_TRUE(scale_within_limits(qar_qt_streaming::kMaxMetersPerLogicalPx, 4.0));
  EXPECT_TRUE(scale_within_limits(0.0005, qar_qt_streaming::kMinRenderScale));
  EXPECT_FALSE(scale_within_limits(0.0, 1.0));
  EXPECT_FALSE(scale_within_limits(0.0101, 1.0));
  EXPECT_FALSE(scale_within_limits(0.001, 0.99));
  EXPECT_FALSE(scale_within_limits(0.001, 4.01));
  EXPECT_FALSE(scale_within_limits(std::numeric_limits<double>::infinity(), 1.0));
  EXPECT_FALSE(scale_within_limits(0.001, std::numeric_limits<double>::quiet_NaN()));

  // The stream refuses what the shared check refuses, and takes the edges.
  BlockWidget source(200, 120);
  auto factory = std::make_shared<MockQarPanelSenderFactory>();
  QtPanelStream stream("p", source.get(), options_no_pacing(), factory);
  stream.set_scale(qar_qt_streaming::kMaxMetersPerLogicalPx, qar_qt_streaming::kMaxRenderScale);
  EXPECT_TRUE(stream.diagnostics().last_error.empty());
  stream.set_scale(qar_qt_streaming::kMaxMetersPerLogicalPx * 1.01, 1.0);
  EXPECT_FALSE(stream.diagnostics().last_error.empty());
}
