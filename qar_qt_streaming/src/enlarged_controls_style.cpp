// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/enlarged_controls_style.hpp"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QChildEvent>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QHash>
#include <QHelpEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <QPointer>
#include <QRect>
#include <QScrollBar>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTransform>
#include <QTreeView>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <vector>

namespace qar_qt_streaming
{

namespace
{

/// The key QStyleFactory knows that matches @p wanted (case does not matter), else the default.
QString
resolved_key(const QString & wanted)
{
  const QStringList keys = QStyleFactory::keys();
  for (const QString & key : keys) {
    if (key.compare(wanted, Qt::CaseInsensitive) == 0) {
      return key.toLower();
    }
  }
  return QString::fromLatin1(kEnlargedControlsDefaultBase);
}

QStyle *
make_base(const QString & key)
{
  return QStyleFactory::create(key);
}

int
scaled(int value, double factor)
{
  return static_cast<int>(std::lround(value * factor));
}

/// The option with its rectangle divided by @p factor and moved to the origin; the subtype and
/// every other field are kept, because the base styles draw nothing for an option of the wrong
/// subtype.
template<class Option>
Option
shrunk(const Option & option, double factor)
{
  Option copy(option);
  copy.rect = QRect(
    0, 0, static_cast<int>(std::ceil(option.rect.width() / factor)),
    static_cast<int>(std::ceil(option.rect.height() / factor)));
  return copy;
}

/// How many pixels the buttons of a spin box, laid out by @p plain_style in @p option, reach past
/// its right edge. The Fusion style does so by two pixels when the box has no frame: its buttons
/// are 14 wide but start 12 from the edge. Zero for a layout from the right.
int
spin_button_overhang(
  const QStyle & plain_style, const QStyleOptionSpinBox & option, const QWidget * widget)
{
  if (option.direction == Qt::RightToLeft) {
    return 0;
  }
  const QRect up =
    plain_style.subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, widget);
  const QRect down =
    plain_style.subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, widget);
  return std::max({0, up.right() - option.rect.right(), down.right() - option.rect.right()});
}

/// Calls @p fn with the shrunk copy of @p option in its own subtype. False when the option has
/// none of the subtypes the complex control needs (the caller then falls back to the base).
/// A spin box is shrunk by the overhang of its buttons as well: the base style would draw the
/// part of a button that lies past the edge outside the widget, and the arrow, centred on the
/// whole button, would sit off the middle of what is visible.
template<class Fn>
bool
with_shrunk_complex(
  QStyle::ComplexControl control, const QStyleOptionComplex * option, double factor,
  const QStyle & plain_style, const QWidget * widget, Fn && fn)
{
  switch (control) {
    case QStyle::CC_SpinBox:
      if (const auto * spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
        QStyleOptionSpinBox copy = shrunk(*spin, factor);
        const int overhang = spin_button_overhang(plain_style, copy, widget);
        if (overhang > 0) {
          copy.rect.setWidth(std::max(1, copy.rect.width() - overhang));
        }
        fn(&copy);
        return true;
      }
      return false;
    case QStyle::CC_ComboBox:
      if (const auto * combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
        const QStyleOptionComboBox copy = shrunk(*combo, factor);
        fn(&copy);
        return true;
      }
      return false;
    case QStyle::CC_ScrollBar:
    case QStyle::CC_Slider:
      if (const auto * slider = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
        const QStyleOptionSlider copy = shrunk(*slider, factor);
        fn(&copy);
        return true;
      }
      return false;
    default:
      return false;
  }
}


/// The colour group a primitive is drawn in.
QPalette::ColorGroup
color_group_of(const QStyleOption & option)
{
  if (!(option.state & QStyle::State_Enabled)) {
    return QPalette::Disabled;
  }
  return (option.state & QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
}

/// Whether @p element is one of the arrows and signs that are drawn as vectors.
bool
is_vector_arrow(QStyle::PrimitiveElement element, const QStyleOption & option)
{
  switch (element) {
    case QStyle::PE_IndicatorBranch:
      // Only the expand arrow; the base style decides about branch lines.
      return (option.state & QStyle::State_Children) != 0;
    case QStyle::PE_IndicatorArrowUp:
    case QStyle::PE_IndicatorArrowDown:
    case QStyle::PE_IndicatorArrowLeft:
    case QStyle::PE_IndicatorArrowRight:
    case QStyle::PE_IndicatorSpinUp:
    case QStyle::PE_IndicatorSpinDown:
    case QStyle::PE_IndicatorSpinPlus:
    case QStyle::PE_IndicatorSpinMinus:
    case QStyle::PE_IndicatorHeaderArrow:
      return true;
    default:
      return false;
  }
}

/// An antialiased triangle (or a plus / minus sign) in the rectangle of @p option, in the colour
/// the text of that widget uses. Drawing the arrow as a path keeps its edges sharp at any
/// enlargement; the base styles paint it as a small pixmap that a scaled painter would blur.
void
draw_vector_arrow(
  QStyle::PrimitiveElement element, const QStyleOption & option, QPainter * painter)
{
  const QRectF rect(option.rect);
  if (rect.isEmpty()) {
    return;
  }
  const QPalette::ColorRole role =
    element == QStyle::PE_IndicatorBranch ? QPalette::Text : QPalette::ButtonText;
  QColor color = option.palette.color(color_group_of(option), role);
  if (element == QStyle::PE_IndicatorBranch && (option.state & QStyle::State_Selected)) {
    color = option.palette.color(color_group_of(option), QPalette::HighlightedText);
  }
  const QPointF center = rect.center();
  const double extent = std::min(rect.width(), rect.height());
  const double edge = extent * (element == QStyle::PE_IndicatorBranch ? 0.34 : 0.5);

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing, true);
  if (element == QStyle::PE_IndicatorSpinPlus || element == QStyle::PE_IndicatorSpinMinus) {
    QPen pen(color);
    pen.setWidthF(std::max(1.0, extent * 0.12));
    pen.setCapStyle(Qt::FlatCap);
    painter->setPen(pen);
    painter->drawLine(
      QPointF(center.x() - edge / 2, center.y()), QPointF(center.x() + edge / 2, center.y()));
    if (element == QStyle::PE_IndicatorSpinPlus) {
      painter->drawLine(
        QPointF(center.x(), center.y() - edge / 2), QPointF(center.x(), center.y() + edge / 2));
    }
    painter->restore();
    return;
  }
  // The triangle points right; the others are turned about the centre.
  double degrees = 0.0;
  switch (element) {
    case QStyle::PE_IndicatorArrowUp:
    case QStyle::PE_IndicatorSpinUp:
      degrees = -90.0;
      break;
    case QStyle::PE_IndicatorArrowDown:
    case QStyle::PE_IndicatorSpinDown:
      degrees = 90.0;
      break;
    case QStyle::PE_IndicatorArrowLeft:
      degrees = 180.0;
      break;
    case QStyle::PE_IndicatorBranch:
      degrees = (option.state & QStyle::State_Open) ? 90.0 : 0.0;
      break;
    case QStyle::PE_IndicatorHeaderArrow: {
      const auto * header = qstyleoption_cast<const QStyleOptionHeader *>(&option);
      degrees = (header != nullptr && header->sortIndicator == QStyleOptionHeader::SortDown) ?
        90.0 : -90.0;
      break;
    }
    default:
      break;
  }
  const double half_depth = edge * 0.4;
  QPainterPath path;
  path.moveTo(-half_depth, -edge * 0.5);
  path.lineTo(-half_depth, edge * 0.5);
  path.lineTo(half_depth, 0.0);
  path.closeSubpath();
  painter->translate(center);
  painter->rotate(degrees);
  painter->setPen(Qt::NoPen);
  painter->setBrush(color);
  painter->drawPath(path);
  painter->restore();
}

/// The cache key of one complex control: everything the base style looks at to draw it, and the
/// base style itself, so that two enlarged styles over different base styles never share an image.
QString
complex_key(
  const QStyle & base, QStyle::ComplexControl control, const QStyleOptionComplex & option,
  const QSize & plain, double ratio)
{
  QString key = QStringLiteral("qar-ecs|%1/%2|%3|%4|%5|%6|%7|%8x%9|%10|%11")
    .arg(QString::fromLatin1(base.metaObject()->className()))
    .arg(base.objectName())
    .arg(static_cast<int>(control))
    .arg(static_cast<int>(option.state))
    .arg(static_cast<int>(option.activeSubControls))
    .arg(static_cast<int>(option.subControls))
    .arg(option.palette.cacheKey())
    .arg(plain.width())
    .arg(plain.height())
    .arg(ratio, 0, 'f', 3)
    .arg(static_cast<int>(option.direction));
  if (const auto * spin = qstyleoption_cast<const QStyleOptionSpinBox *>(&option)) {
    key += QStringLiteral("|s%1,%2,%3")
      .arg(static_cast<int>(spin->buttonSymbols))
      .arg(static_cast<int>(spin->stepEnabled))
      .arg(spin->frame ? 1 : 0);
  } else if (const auto * combo = qstyleoption_cast<const QStyleOptionComboBox *>(&option)) {
    key += QStringLiteral("|c%1,%2").arg(combo->editable ? 1 : 0).arg(combo->frame ? 1 : 0);
  } else if (const auto * slider = qstyleoption_cast<const QStyleOptionSlider *>(&option)) {
    key += QStringLiteral("|l%1,%2,%3,%4,%5,%6,%7,%8,%9,%10")
      .arg(static_cast<int>(slider->orientation))
      .arg(slider->minimum)
      .arg(slider->maximum)
      .arg(slider->sliderPosition)
      .arg(slider->sliderValue)
      .arg(slider->singleStep)
      .arg(slider->pageStep)
      .arg(static_cast<int>(slider->tickPosition))
      .arg(slider->tickInterval)
      .arg(slider->upsideDown ? 1 : 0);
  }
  return key;
}

/// The scale from the painter's coordinates to the pixels of its target (the device pixel ratio
/// of the target when the painter is not scaled), never below 1 and capped so that a mistaken
/// value cannot make the cache allocate a huge pixmap.
double
device_ratio_of(const QPainter & painter)
{
  const QTransform device = painter.deviceTransform();
  const double ratio = std::hypot(device.m11(), device.m12());
  return std::isfinite(ratio) ? std::clamp(ratio, 1.0, 4.0) : 1.0;
}

// ---- form-row labels ------------------------------------------------------------------------

constexpr const char * kFormLabelMinHeight = "qar_form_label_min_height";
constexpr const char * kFormLabelAlignment = "qar_form_label_alignment";

/// The form layout below the parent of @p label that holds it as the label of a row; its row is
/// set. Null when the label is no row label.
QFormLayout *
form_of_label(QLabel * label, int * row)
{
  QWidget * parent = label->parentWidget();
  if (parent == nullptr) {
    return nullptr;
  }
  const QList<QFormLayout *> forms = parent->findChildren<QFormLayout *>();
  for (QFormLayout * form : forms) {
    QFormLayout::ItemRole role = QFormLayout::FieldRole;
    int found = -1;
    form->getWidgetPosition(label, &found, &role);
    if (found >= 0 && role == QFormLayout::LabelRole) {
      *row = found;
      return form;
    }
  }
  return nullptr;
}

/// Gives the label of a form row the height of the row and centres its text vertically.
/// QFormLayout puts a label at the top of a row that a taller field made higher. A minimum
/// height (not an expanding policy) keeps the rows from taking a share of free space. Safe to
/// call again: the values the label had are kept the first time only.
void
centre_form_label(QLabel * label)
{
  int row = -1;
  QFormLayout * form = form_of_label(label, &row);
  if (form == nullptr) {
    return;
  }
  const QLayoutItem * field = form->itemAt(row, QFormLayout::FieldRole);
  if (field == nullptr) {
    return;
  }
  if (!label->property(kFormLabelMinHeight).isValid()) {
    label->setProperty(kFormLabelMinHeight, label->minimumHeight());
    label->setProperty(kFormLabelAlignment, static_cast<int>(label->alignment()));
  }
  const int wanted = std::max(
    label->property(kFormLabelMinHeight).toInt(), field->sizeHint().height());
  if (label->minimumHeight() != wanted) {
    label->setMinimumHeight(wanted);
  }
  const Qt::Alignment aligned =
    (label->alignment() & ~Qt::AlignVertical_Mask) | Qt::AlignVCenter;
  if (label->alignment() != aligned) {
    label->setAlignment(aligned);
  }
}

void
restore_form_label(QLabel * label)
{
  if (!label->property(kFormLabelMinHeight).isValid()) {
    return;
  }
  label->setMinimumHeight(label->property(kFormLabelMinHeight).toInt());
  label->setAlignment(static_cast<Qt::Alignment>(label->property(kFormLabelAlignment).toInt()));
  label->setProperty(kFormLabelMinHeight, QVariant());
  label->setProperty(kFormLabelAlignment, QVariant());
}

// ---- combo popup rows -----------------------------------------------------------------------

constexpr const char * kComboDelegateName = "qar_combo_row_height";

/// Wraps the delegate of a combo box popup: it paints with it and asks it for the size, but the
/// row is at least as tall as the combo box. The popup of a combo box sizes its rows from the
/// font, so with enlarged controls the rows would be half as tall as the combo that opens them.
/// Everything else goes to the wrapped delegate unchanged (its mouse and key handling of items, its
/// tool tips, its editors), and its signals are passed on.
class ComboRowDelegate : public QAbstractItemDelegate
{
public:
  ComboRowDelegate(QComboBox * combo, QAbstractItemDelegate * previous)
  : QAbstractItemDelegate(combo), combo_(combo), previous_(previous)
  {
    setObjectName(QString::fromLatin1(kComboDelegateName));
    if (combo != nullptr) {
      combo->installEventFilter(this);
    }
    if (previous != nullptr) {
      connect(
        previous, &QAbstractItemDelegate::commitData, this, &QAbstractItemDelegate::commitData);
      connect(
        previous, &QAbstractItemDelegate::closeEditor, this, &QAbstractItemDelegate::closeEditor);
      connect(
        previous, &QAbstractItemDelegate::sizeHintChanged, this,
        &QAbstractItemDelegate::sizeHintChanged);
    }
  }

  QAbstractItemDelegate * previous() const {return previous_;}

  /// A popup that was laid out before lays its rows out again when the combo box changes height.
  bool eventFilter(QObject * watched, QEvent * event) override
  {
    if (watched == combo_ && event->type() == QEvent::Resize) {
      emit sizeHintChanged(QModelIndex());
    }
    return QObject::eventFilter(watched, event);
  }

  void paint(
    QPainter * painter, const QStyleOptionViewItem & option,
    const QModelIndex & index) const override
  {
    if (previous_ != nullptr) {
      previous_->paint(painter, option, index);
    }
  }

  QSize sizeHint(const QStyleOptionViewItem & option, const QModelIndex & index) const override
  {
    QSize size = previous_ != nullptr ? previous_->sizeHint(option, index) : QSize(0, 0);
    if (combo_ != nullptr) {
      // A combo box placed in a cell is as tall as the row, below its own size hint; the popup
      // that it opens takes the height that is drawn.
      const int combo_height = combo_->isVisible() ? combo_->height() : combo_->sizeHint().height();
      size.setHeight(std::max(size.height(), combo_height));
    }
    return size;
  }

  QWidget * createEditor(
    QWidget * parent, const QStyleOptionViewItem & option,
    const QModelIndex & index) const override
  {
    return previous_ != nullptr ? previous_->createEditor(parent, option, index) : nullptr;
  }

  void destroyEditor(QWidget * editor, const QModelIndex & index) const override
  {
    if (previous_ != nullptr) {
      previous_->destroyEditor(editor, index);
    } else {
      QAbstractItemDelegate::destroyEditor(editor, index);
    }
  }

  void setEditorData(QWidget * editor, const QModelIndex & index) const override
  {
    if (previous_ != nullptr) {
      previous_->setEditorData(editor, index);
    }
  }

  void setModelData(
    QWidget * editor, QAbstractItemModel * model, const QModelIndex & index) const override
  {
    if (previous_ != nullptr) {
      previous_->setModelData(editor, model, index);
    }
  }

  void updateEditorGeometry(
    QWidget * editor, const QStyleOptionViewItem & option,
    const QModelIndex & index) const override
  {
    if (previous_ != nullptr) {
      previous_->updateEditorGeometry(editor, option, index);
    }
  }

  bool editorEvent(
    QEvent * event, QAbstractItemModel * model, const QStyleOptionViewItem & option,
    const QModelIndex & index) override
  {
    return previous_ != nullptr && previous_->editorEvent(event, model, option, index);
  }

  bool helpEvent(
    QHelpEvent * event, QAbstractItemView * view, const QStyleOptionViewItem & option,
    const QModelIndex & index) override
  {
    return previous_ != nullptr && previous_->helpEvent(event, view, option, index);
  }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  QList<int> paintingRoles() const override
  {
    return previous_ != nullptr ? previous_->paintingRoles() : QList<int>();
  }
#endif

private:
  QPointer<QComboBox> combo_;
  QPointer<QAbstractItemDelegate> previous_;
};

void
install_combo_rows(QComboBox * combo)
{
  QAbstractItemView * view = combo->view();
  if (view == nullptr || dynamic_cast<ComboRowDelegate *>(view->itemDelegate()) != nullptr) {
    return;
  }
  view->setItemDelegate(new ComboRowDelegate(combo, view->itemDelegate()));
}

void
restore_combo_rows(QComboBox * combo)
{
  auto * wrapper = dynamic_cast<ComboRowDelegate *>(
    combo->findChild<QObject *>(
      QString::fromLatin1(kComboDelegateName), Qt::FindDirectChildrenOnly));
  if (wrapper == nullptr) {
    return;
  }
  QAbstractItemView * view = combo->view();
  if (view != nullptr && view->itemDelegate() == wrapper && wrapper->previous() != nullptr) {
    view->setItemDelegate(wrapper->previous());
  }
  delete wrapper;
}

// ---- horizontal scroll of item views --------------------------------------------------------

constexpr const char * kScrollPinName = "qar_horizontal_scroll_pin";

/// Keeps an item view from scrolling sideways while it offers no horizontal scroll bar, and fits
/// the last column of a tree to its viewport then.
///
/// A tree whose columns are wider than its viewport scrolls sideways when a cell is selected,
/// which moves the whole content although nothing offers that scroll to the person: the scroll
/// value stays at 0. Whoever sizes the columns (RViz's property tree splits the width of the whole
/// frame, which includes the vertical scroll bar) can make the last column reach under that bar,
/// so the right part of an editor in its cells would be cut off. With the horizontal bar switched
/// off the last column ends at the right edge of the viewport instead. The width that the owner
/// of each column set last is kept; a column that stops being the last one, a view that may
/// scroll and the removal of the pin give it back. The columns are fitted when the viewport is
/// resized, when a column is resized, moved, shown, hidden or added, and when the frame is
/// painted after the scroll bar policy changed (the policy has no signal of its own; changing it
/// repaints the frame).
class HorizontalScrollPin : public QObject
{
public:
  explicit HorizontalScrollPin(QAbstractItemView * view)
  : QObject(view)
  {
    setObjectName(QString::fromLatin1(kScrollPinName));
    QScrollBar * bar = view->horizontalScrollBar();
    QObject::connect(bar, &QScrollBar::valueChanged, this, [view, bar](int value) {
        if (value != 0 &&
        (view->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff || !bar->isVisibleTo(view)))
        {
          bar->setValue(0);
        }
      });
    tree_ = qobject_cast<QTreeView *>(view);
    if (tree_ != nullptr) {
      QHeaderView * header = tree_->header();
      QObject::connect(
        header, &QHeaderView::sectionResized, this,
        [this](int logical, int old_size, int new_size) {
          on_resized(logical, old_size, new_size);
        });
      QObject::connect(header, &QHeaderView::sectionMoved, this, [this](int, int, int) {fit();});
      QObject::connect(
        header, &QHeaderView::sectionCountChanged, this, [this](int, int) {fit();});
      viewport_ = tree_->viewport();
      viewport_->installEventFilter(this);
      tree_->installEventFilter(this);
      fit();
    }
  }

  /// Every fitted column gets back the width its owner gave it, and the pin stops fitting.
  /// Called before the pin is deleted by hand; a pin that goes with its view leaves the columns
  /// alone (the view is half destroyed by then).
  void give_back()
  {
    released_ = true;
    if (tree_ == nullptr) {
      return;
    }
    QHeaderView * header = tree_->header();
    const int last = last_section();
    fitting_ = true;
    give_back_others(last);
    // A stretching header keeps changing the width of its last column, so that one is always set.
    if (last >= 0 && (fitted_sizes_.contains(last) || header->stretchLastSection())) {
      header->resizeSection(last, owner_width(last));
    }
    fitting_ = false;
    fitted_sizes_.clear();
  }

  bool eventFilter(QObject * watched, QEvent * event) override
  {
    if (event->type() == QEvent::Resize && watched == viewport_) {
      fit();
    } else if (event->type() == QEvent::Paint && watched == tree_ &&
      tree_->horizontalScrollBarPolicy() != policy_)
    {
      fit();
    }
    return QObject::eventFilter(watched, event);
  }

private:
  /// The logical index of the last column that is shown, -1 without one.
  int last_section() const
  {
    const QHeaderView * header = tree_->header();
    for (int visual = header->count() - 1; visual >= 0; --visual) {
      const int logical = header->logicalIndex(visual);
      if (!header->isSectionHidden(logical)) {
        return logical;
      }
    }
    return -1;
  }

  /// A width that is not the pin's own is the owner's wish for that column. Showing a hidden
  /// column brings back the width it had when it was hidden, which is the fitted one when the
  /// pin had fitted it, so that is no wish; neither is a width of 0, which hides a column.
  void on_resized(int logical, int old_size, int new_size)
  {
    if (!fitting_ && new_size > 0) {
      const auto fitted = fitted_sizes_.constFind(logical);
      const bool restored =
        old_size == 0 && fitted != fitted_sizes_.constEnd() && fitted.value() == new_size;
      if (!restored) {
        owner_sizes_.insert(logical, new_size);
        fitted_sizes_.remove(logical);
      }
    }
    fit();
  }

  /// The width the owner gave the last column. A header that stretches its last column sizes it
  /// to the viewport again whenever the viewport or another column changes, so the widths that
  /// the header reports for it are its own and no wish of the owner (they also follow every
  /// width that the pin fits). The owner of such a header gets the frame of the view for the
  /// last column: the widest that the column can be meant to be.
  int owner_width(int last) const
  {
    const QHeaderView * header = tree_->header();
    if (!header->stretchLastSection()) {
      return owner_sizes_.value(last, header->sectionSize(last));
    }
    const int others = header->length() - header->sectionSize(last);
    return std::max(header->minimumSectionSize(), tree_->contentsRect().width() - others);
  }

  /// The columns that were fitted as the last one and are not any more get their owner width.
  /// A hidden column keeps its fitted width until it is shown again.
  void give_back_others(int last)
  {
    QHeaderView * header = tree_->header();
    const QList<int> fitted = fitted_sizes_.keys();
    for (const int logical : fitted) {
      if (logical == last) {
        continue;
      }
      if (logical >= header->count()) {
        fitted_sizes_.remove(logical);
        owner_sizes_.remove(logical);
      } else if (!header->isSectionHidden(logical)) {
        header->resizeSection(logical, owner_sizes_.value(logical));
        fitted_sizes_.remove(logical);
      }
    }
  }

  void fit()
  {
    if (tree_ == nullptr || fitting_ || released_) {
      return;
    }
    QHeaderView * header = tree_->header();
    const int last = last_section();
    if (last < 0 || tree_->viewport()->width() <= 0) {
      return;
    }
    policy_ = tree_->horizontalScrollBarPolicy();
    fitting_ = true;
    give_back_others(last);
    if (!owner_sizes_.contains(last)) {
      owner_sizes_.insert(last, header->sectionSize(last));
    }
    int wanted = owner_width(last);
    if (tree_->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff) {
      const int others = header->length() - header->sectionSize(last);
      const int room = tree_->viewport()->width() - others;
      wanted = std::max(header->minimumSectionSize(), std::min(wanted, room));
    }
    if (header->sectionSize(last) != wanted) {
      header->resizeSection(last, wanted);
    }
    fitting_ = false;
    if (header->sectionSize(last) != owner_sizes_.value(last)) {
      fitted_sizes_.insert(last, header->sectionSize(last));
    } else {
      fitted_sizes_.remove(last);
    }
  }

  QPointer<QTreeView> tree_;
  QPointer<QWidget> viewport_;
  QHash<int, int> owner_sizes_;   // by logical index: the width the owner set last
  QHash<int, int> fitted_sizes_;  // by logical index: the width of a column the pin changed
  Qt::ScrollBarPolicy policy_ = Qt::ScrollBarAsNeeded;  // the policy of the last fit
  bool fitting_ = false;
  bool released_ = false;
};

void
install_scroll_pin(QAbstractItemView * view)
{
  const QString name = QString::fromLatin1(kScrollPinName);
  if (view->findChild<QObject *>(name, Qt::FindDirectChildrenOnly) == nullptr) {
    new HorizontalScrollPin(view);
  }
}

void
remove_scroll_pin(QAbstractItemView * view)
{
  QObject * pin = view->findChild<QObject *>(
    QString::fromLatin1(kScrollPinName), Qt::FindDirectChildrenOnly);
  if (auto * scroll_pin = dynamic_cast<HorizontalScrollPin *>(pin)) {
    scroll_pin->give_back();
  }
  delete pin;
}

}  // namespace

double
enlarged_controls_factor(double wanted)
{
  if (!std::isfinite(wanted)) {
    return kEnlargedControlsDefaultFactor;
  }
  return std::clamp(wanted, kEnlargedControlsMinFactor, kEnlargedControlsMaxFactor);
}

EnlargedControlsStyle::EnlargedControlsStyle(double factor, const QString & base_key)
: QProxyStyle(make_base(resolved_key(base_key))),
  plain_(make_base(resolved_key(base_key))),
  factor_(enlarged_controls_factor(factor)),
  base_key_(resolved_key(base_key))
{
}

EnlargedControlsStyle::~EnlargedControlsStyle() = default;

double
EnlargedControlsStyle::factor() const
{
  return factor_;
}

QString
EnlargedControlsStyle::base_key() const
{
  return base_key_;
}

bool
EnlargedControlsStyle::scales_metric(PixelMetric metric)
{
  switch (metric) {
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
    case PM_ScrollBarExtent:
    case PM_ScrollBarSliderMin:
    case PM_SliderThickness:
    case PM_SliderLength:
    case PM_SliderControlThickness:
    case PM_ButtonMargin:
    case PM_TreeViewIndentation:
    case PM_MenuButtonIndicator:
    case PM_HeaderMarkSize:
    case PM_TabBarScrollButtonWidth:
    case PM_ToolBarExtensionExtent:
      return true;
    default:
      return false;
  }
}

bool
EnlargedControlsStyle::scales_primitive(PrimitiveElement element)
{
  switch (element) {
    case PE_IndicatorCheckBox:
    case PE_IndicatorRadioButton:
    case PE_IndicatorItemViewItemCheck:
    case PE_IndicatorBranch:
    case PE_IndicatorArrowUp:
    case PE_IndicatorArrowDown:
    case PE_IndicatorArrowLeft:
    case PE_IndicatorArrowRight:
    case PE_IndicatorSpinUp:
    case PE_IndicatorSpinDown:
    case PE_IndicatorSpinPlus:
    case PE_IndicatorSpinMinus:
    case PE_IndicatorHeaderArrow:
      return true;
    default:
      return false;
  }
}

bool
EnlargedControlsStyle::scales_complex_control(ComplexControl control)
{
  return control == CC_SpinBox || control == CC_ComboBox || control == CC_ScrollBar ||
         control == CC_Slider;
}

int
EnlargedControlsStyle::pixelMetric(
  PixelMetric metric, const QStyleOption * option, const QWidget * widget) const
{
  const int base = QProxyStyle::pixelMetric(metric, option, widget);
  return scales_metric(metric) ? scaled(base, factor_) : base;
}

void
EnlargedControlsStyle::drawPrimitive(
  PrimitiveElement element, const QStyleOption * option, QPainter * painter,
  const QWidget * widget) const
{
  if (!scales_primitive(element) || option == nullptr || painter == nullptr) {
    QProxyStyle::drawPrimitive(element, option, painter, widget);
    return;
  }
  if (is_vector_arrow(element, *option)) {
    draw_vector_arrow(element, *option, painter);
    return;
  }
  painter->save();
  // The shrunk rectangle is rounded up, so at a factor that is not whole the drawing can reach
  // a pixel past the rectangle: the clip keeps it inside.
  painter->setClipRect(option->rect, Qt::IntersectClip);
  painter->translate(option->rect.topLeft());
  painter->scale(factor_, factor_);
  if (const auto * button = qstyleoption_cast<const QStyleOptionButton *>(option)) {
    const QStyleOptionButton copy = shrunk(*button, factor_);
    plain_->drawPrimitive(element, &copy, painter, widget);
  } else if (const auto * item = qstyleoption_cast<const QStyleOptionViewItem *>(option)) {
    const QStyleOptionViewItem copy = shrunk(*item, factor_);
    plain_->drawPrimitive(element, &copy, painter, widget);
  } else if (const auto * spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
    const QStyleOptionSpinBox copy = shrunk(*spin, factor_);
    plain_->drawPrimitive(element, &copy, painter, widget);
  } else if (const auto * header = qstyleoption_cast<const QStyleOptionHeader *>(option)) {
    const QStyleOptionHeader copy = shrunk(*header, factor_);
    plain_->drawPrimitive(element, &copy, painter, widget);
  } else {
    const QStyleOption copy = shrunk(*option, factor_);
    plain_->drawPrimitive(element, &copy, painter, widget);
  }
  painter->restore();
}

void
EnlargedControlsStyle::drawComplexControl(
  ComplexControl control, const QStyleOptionComplex * option, QPainter * painter,
  const QWidget * widget) const
{
  if (!scales_complex_control(control) || option == nullptr || painter == nullptr) {
    QProxyStyle::drawComplexControl(control, option, painter, widget);
    return;
  }
  // The base style paints a spin box, combo box, scroll bar or slider through cached pixmaps
  // of its own, made at the pixel ratio of the application. A scaled painter would magnify them
  // in blocks, so the control is painted at the plain size into a pixmap that has the ratio of
  // the factor times the target's, with a smooth filter for those cached pixmaps, and that
  // pixmap is drawn 1:1. The pixmap is cached: a repaint of an unchanged control costs a lookup.
  const double device_ratio = device_ratio_of(*painter);
  const double ratio = std::clamp(factor_ * device_ratio, 1.0, 8.0);
  const QSize plain(
    static_cast<int>(std::ceil(option->rect.width() / factor_)),
    static_cast<int>(std::ceil(option->rect.height() / factor_)));
  const QString key = complex_key(*plain_, control, *option, plain, ratio);
  QPixmap pixmap;
  if (!QPixmapCache::find(key, &pixmap)) {
    pixmap = QPixmap(
      static_cast<int>(std::ceil(plain.width() * ratio)),
      static_cast<int>(std::ceil(plain.height() * ratio)));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter inner(&pixmap);
    inner.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const bool done = with_shrunk_complex(
      control, option, factor_, *plain_, widget, [&](const QStyleOptionComplex * copy) {
        plain_->drawComplexControl(control, copy, &inner, widget);
      });
    inner.end();
    if (!done) {
      QProxyStyle::drawComplexControl(control, option, painter, widget);
      return;
    }
    QPixmapCache::insert(key, pixmap);
  }
  painter->save();
  painter->setClipRect(option->rect, Qt::IntersectClip);
  painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter->drawPixmap(
    QRectF(
      option->rect.topLeft(),
      QSizeF(pixmap.width() / device_ratio, pixmap.height() / device_ratio)),
    pixmap, QRectF(pixmap.rect()));
  painter->restore();
  paint_active_part(control, option, painter, widget);
}

/// The base style has no colour for the button the pointer is on or that is pressed (the Fusion
/// style changes it by two grey levels), so the part gets a light tint of the highlight colour and
/// a border in it. The tint is multiplied into what is drawn, so the light button takes the colour
/// and the dark arrow stays dark. The Active colour group is used although the window of a
/// panel copy is never the active one: the pointer that hovers a button is the person's input.
void
EnlargedControlsStyle::paint_active_part(
  ComplexControl control, const QStyleOptionComplex * option, QPainter * painter,
  const QWidget * widget) const
{
  if (!(option->state & State_Enabled)) {
    return;
  }
  const bool pressed = (option->state & State_Sunken) != 0;
  if (!pressed && !(option->state & State_MouseOver)) {
    return;
  }
  SubControl part = SC_None;
  if (control == CC_SpinBox) {
    if (option->activeSubControls & SC_SpinBoxUp) {
      part = SC_SpinBoxUp;
    } else if (option->activeSubControls & SC_SpinBoxDown) {
      part = SC_SpinBoxDown;
    }
  } else if (control == CC_ComboBox) {
    if (option->activeSubControls & SC_ComboBoxArrow) {
      part = SC_ComboBoxArrow;
    }
  }
  if (part == SC_None) {
    return;
  }
  const int inset = std::max(1, static_cast<int>(factor_));
  const QRect rect =
    subControlRect(control, option, part, widget).adjusted(inset, inset, -inset, -inset);
  if (rect.isEmpty()) {
    return;
  }
  const QColor highlight = option->palette.color(QPalette::Active, QPalette::Highlight);
  const double share = pressed ? 0.20 : 0.10;
  const auto mixed = [share](int channel) {
      return static_cast<int>(std::lround(255.0 + (channel - 255) * share));
    };
  const QColor tint(mixed(highlight.red()), mixed(highlight.green()), mixed(highlight.blue()));
  painter->save();
  painter->setClipRect(option->rect, Qt::IntersectClip);
  // Multiplying keeps the arrow as dark as it is. An engine without blend modes would lighten
  // it with any fill, so it gets the border alone.
  if (painter->paintEngine() != nullptr &&
    painter->paintEngine()->hasFeature(QPaintEngine::BlendModes))
  {
    painter->setCompositionMode(QPainter::CompositionMode_Multiply);
    painter->fillRect(rect, tint);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
  }
  QPen pen(highlight);
  pen.setWidthF(std::max(1.0, factor_ * 0.5));
  painter->setPen(pen);
  painter->setBrush(Qt::NoBrush);
  const double half = pen.widthF() / 2.0;
  painter->drawRect(QRectF(rect).adjusted(half, half, -half, -half));
  painter->restore();
}

QRect
EnlargedControlsStyle::subControlRect(
  ComplexControl control, const QStyleOptionComplex * option, SubControl sub,
  const QWidget * widget) const
{
  if (!scales_complex_control(control) || option == nullptr) {
    return QProxyStyle::subControlRect(control, option, sub, widget);
  }
  QRect plain;
  const bool done = with_shrunk_complex(
    control, option, factor_, *plain_, widget, [&](const QStyleOptionComplex * copy) {
      plain = plain_->subControlRect(control, copy, sub, widget);
    });
  if (!done) {
    return QProxyStyle::subControlRect(control, option, sub, widget);
  }
  // Both edges are rounded, so neighbouring sub-controls neither overlap nor leave a gap.
  const int left = scaled(plain.left(), factor_);
  const int top = scaled(plain.top(), factor_);
  const int right = scaled(plain.right() + 1, factor_);
  const int bottom = scaled(plain.bottom() + 1, factor_);
  // The rounded edges can lie a pixel past the widget at a factor that is not whole.
  const QRect mapped(
    option->rect.x() + left, option->rect.y() + top, std::max(0, right - left),
    std::max(0, bottom - top));
  return mapped.isEmpty() ? mapped : mapped.intersected(option->rect);
}

QStyle::SubControl
EnlargedControlsStyle::hitTestComplexControl(
  ComplexControl control, const QStyleOptionComplex * option, const QPoint & pos,
  const QWidget * widget) const
{
  if (!scales_complex_control(control) || option == nullptr) {
    return QProxyStyle::hitTestComplexControl(control, option, pos, widget);
  }
  const QPoint local(
    static_cast<int>(std::floor((pos.x() - option->rect.x()) / factor_)),
    static_cast<int>(std::floor((pos.y() - option->rect.y()) / factor_)));
  SubControl hit = SC_None;
  const bool done = with_shrunk_complex(
    control, option, factor_, *plain_, widget, [&](const QStyleOptionComplex * copy) {
      hit = plain_->hitTestComplexControl(control, copy, local, widget);
    });
  return done ? hit : QProxyStyle::hitTestComplexControl(control, option, pos, widget);
}

QSize
EnlargedControlsStyle::sizeFromContents(
  ContentsType type, const QStyleOption * option, const QSize & size, const QWidget * widget) const
{
  switch (type) {
    case CT_SpinBox:
    case CT_ComboBox: {
      // The decoration around the text (frame, buttons, arrow) grows by the factor; the room
      // for the text does not. The height is at least the factor times the plain height, so
      // the buttons and the arrow are that much taller, not only wider.
      const QSize plain = plain_->sizeFromContents(type, option, size, widget);
      return QSize(
        size.width() + scaled(plain.width() - size.width(), factor_),
        std::max(
          size.height() + scaled(plain.height() - size.height(), factor_),
          scaled(plain.height(), factor_)));
    }
    case CT_PushButton: {
      // A taller target; the width of the base already gives a wide one.
      QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
      const QSize plain = plain_->sizeFromContents(type, option, size, widget);
      result.setHeight(std::max(
          size.height() + scaled(plain.height() - size.height(), factor_),
          scaled(plain.height(), factor_)));
      return result;
    }
    case CT_ItemViewItem: {
      // A row is at least the factor times the plain row, so an editor in it has buttons the
      // factor times as large, and it holds the whole enlarged check box of a checkable item.
      QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
      const QSize plain = plain_->sizeFromContents(type, option, size, widget);
      int height = std::max(result.height(), scaled(plain.height(), factor_));
      const auto * item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
      if (item != nullptr && (item->features & QStyleOptionViewItem::HasCheckIndicator)) {
        const int margin = QProxyStyle::pixelMetric(PM_FocusFrameVMargin, option, widget);
        height = std::max(height, pixelMetric(PM_IndicatorHeight, option, widget) + 2 * margin);
      }
      result.setHeight(height);
      return result;
    }
    default:
      return QProxyStyle::sizeFromContents(type, option, size, widget);
  }
}

void
EnlargedControlsStyle::polish(QWidget * widget)
{
  QProxyStyle::polish(widget);
  if (widget == nullptr) {
    return;
  }
  if (auto * combo = qobject_cast<QComboBox *>(widget)) {
    install_combo_rows(combo);
  } else if (auto * label = qobject_cast<QLabel *>(widget)) {
    centre_form_label(label);
  } else if (auto * view = qobject_cast<QAbstractItemView *>(widget)) {
    install_scroll_pin(view);
  }
}

void
EnlargedControlsStyle::unpolish(QWidget * widget)
{
  if (widget != nullptr) {
    if (auto * combo = qobject_cast<QComboBox *>(widget)) {
      restore_combo_rows(combo);
    } else if (auto * label = qobject_cast<QLabel *>(widget)) {
      restore_form_label(label);
    } else if (auto * view = qobject_cast<QAbstractItemView *>(widget)) {
      remove_scroll_pin(view);
    }
  }
  QProxyStyle::unpolish(widget);
}

namespace
{

/// Gives a style to the widgets of a tree and to the ones made below it later.
class StyleSpreader : public QObject
{
public:
  StyleSpreader(QWidget * root, QStyle * style)
  : QObject(root), style_(style)
  {
    setObjectName(QString::fromLatin1(kStyleSpreaderObjectName));
    apply(root);
  }

  int applied() const {return applied_;}

  /// Takes the style off every widget that was given it and still exists.
  int restore()
  {
    int changed = 0;
    for (const QPointer<QWidget> & widget : styled_) {
      if (!widget.isNull()) {
        widget->setStyle(nullptr);
        ++changed;
      }
    }
    styled_.clear();
    return changed;
  }

  bool eventFilter(QObject * watched, QEvent * event) override
  {
    if (event->type() == QEvent::Show) {
      // A label is put into its form row after it was made; its style is polished before that.
      auto * label = qobject_cast<QLabel *>(watched);
      if (label != nullptr && label->style() == style_) {
        centre_form_label(label);
      }
    }
    if (event->type() == QEvent::ChildAdded) {
      QObject * child = static_cast<QChildEvent *>(event)->child();
      if (child != nullptr && child->isWidgetType()) {
        apply(static_cast<QWidget *>(child));
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void apply(QWidget * widget)
  {
    if (!watched_.contains(widget)) {
      watched_.insert(widget);
      // The pointer is a key only; the entry goes with the widget so that a new widget at the
      // same address is not taken for the old one.
      QObject::connect(widget, &QObject::destroyed, this, [this](QObject * gone) {
          watched_.remove(gone);
        });
      widget->installEventFilter(this);
      if (widget->style() != style_) {
        widget->setStyle(style_);
        styled_.push_back(widget);
        ++applied_;
      }
    }
    // Copy: a widget that is made while the loop runs is handled by its ChildAdded event.
    const QObjectList children = widget->children();
    for (QObject * child : children) {
      if (child->isWidgetType()) {
        apply(static_cast<QWidget *>(child));
      }
    }
  }

  QStyle * style_;
  int applied_ = 0;
  QSet<QObject *> watched_;
  std::vector<QPointer<QWidget>> styled_;
};

StyleSpreader *
spreader_of(QWidget * root)
{
  QObject * found = root->findChild<QObject *>(
    QString::fromLatin1(kStyleSpreaderObjectName), Qt::FindDirectChildrenOnly);
  return dynamic_cast<StyleSpreader *>(found);
}

}  // namespace

int
apply_style_to_tree(QWidget * root, QStyle * style)
{
  if (root == nullptr || style == nullptr) {
    return 0;
  }
  remove_style_from_tree(root);
  auto * spreader = new StyleSpreader(root, style);
  return spreader->applied();
}

int
remove_style_from_tree(QWidget * root)
{
  if (root == nullptr) {
    return 0;
  }
  StyleSpreader * spreader = spreader_of(root);
  if (spreader == nullptr) {
    return 0;
  }
  const int changed = spreader->restore();
  delete spreader;
  return changed;
}

const EnlargedControlsStyle *
enlarged_controls_style_of(const QWidget * widget)
{
  return widget == nullptr ? nullptr : dynamic_cast<const EnlargedControlsStyle *>(widget->style());
}

}  // namespace qar_qt_streaming
