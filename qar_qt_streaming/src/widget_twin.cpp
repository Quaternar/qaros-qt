// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_qt_streaming/widget_twin.hpp"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QSpinBox>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <utility>
#include <vector>

namespace qar_qt_streaming
{

namespace
{

/// What sync() and the forward do for a pair; decided once, from the class of the twin.
enum class Kind
{
  kPlain,
  kCustom,
  kButton,
  kGroup,
  kLabel,
  kCombo,
  kLine,
  kDoubleSpin,
  kSpin,
  kSlider,
  kList,
  kText,
};

/// Sets a flag for the life of the object and clears it at the end of the scope.
class FlagScope
{
public:
  explicit FlagScope(bool & flag)
  : flag_(flag)
  {
    flag_ = true;
  }
  ~FlagScope() {flag_ = false;}
  FlagScope(const FlagScope &) = delete;
  FlagScope & operator=(const FlagScope &) = delete;

private:
  bool & flag_;
};

/// Counts a nesting depth for the life of the object.
class DepthScope
{
public:
  explicit DepthScope(int & depth)
  : depth_(depth)
  {
    ++depth_;
  }
  ~DepthScope() {--depth_;}
  DepthScope(const DepthScope &) = delete;
  DepthScope & operator=(const DepthScope &) = delete;

private:
  int & depth_;
};

/// Lets one Return of an editable combo box reach the original once. Qt hands a Return that the
/// line edit leaves unaccepted to the combo box, which gives it to the line edit again, so the
/// line edit emits returnPressed twice for one key press. The filter on the line edit and on the
/// combo box marks the key press in progress (it ends with the key release); admit() lets the
/// first returnPressed of a key press through and refuses the next. Outside a key press every
/// returnPressed is admitted.
class ReturnGate : public QObject
{
public:
  ReturnGate(QObject * parent, QObject * line, QObject * combo)
  : QObject(parent)
  {
    line->installEventFilter(this);
    combo->installEventFilter(this);
  }

  bool admit()
  {
    if (!pressed_) {
      return true;
    }
    if (returned_) {
      return false;
    }
    returned_ = true;
    return true;
  }

  bool eventFilter(QObject *, QEvent * event) override
  {
    if (event->type() == QEvent::KeyPress) {
      const auto * key = static_cast<QKeyEvent *>(event);
      if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        // The same event object reaches the line edit and then the combo box; a new press is
        // another object or another time stamp, and every auto-repeat is a new press.
        if (!pressed_ || key->isAutoRepeat() || event != seen_ || stamp_ != key->timestamp()) {
          pressed_ = true;
          returned_ = false;
          seen_ = event;
          stamp_ = key->timestamp();
        }
      }
    } else if (event->type() == QEvent::KeyRelease) {
      pressed_ = false;
      returned_ = false;
      seen_ = nullptr;
    }
    return false;
  }

private:
  bool pressed_ = false;
  bool returned_ = false;
  const QEvent * seen_ = nullptr;
  quint64 stamp_ = 0;
};

bool
same_class(const QWidget & a, const QWidget & b)
{
  return qstrcmp(a.metaObject()->className(), b.metaObject()->className()) == 0;
}

/// True when the widget was hidden by a call of its own (a child of a hidden page is not).
bool
explicitly_hidden(const QWidget & widget)
{
  return widget.testAttribute(Qt::WA_WState_ExplicitShowHide) && widget.isHidden();
}

Kind
kind_of(QWidget * twin)
{
  if (qobject_cast<QAbstractButton *>(twin) != nullptr) {
    return Kind::kButton;
  }
  if (qobject_cast<QGroupBox *>(twin) != nullptr) {
    return Kind::kGroup;
  }
  if (qobject_cast<QLabel *>(twin) != nullptr) {
    return Kind::kLabel;
  }
  if (qobject_cast<QComboBox *>(twin) != nullptr) {
    return Kind::kCombo;
  }
  if (qobject_cast<QLineEdit *>(twin) != nullptr) {
    return Kind::kLine;
  }
  if (qobject_cast<QDoubleSpinBox *>(twin) != nullptr) {
    return Kind::kDoubleSpin;
  }
  if (qobject_cast<QSpinBox *>(twin) != nullptr) {
    return Kind::kSpin;
  }
  if (qobject_cast<QAbstractSlider *>(twin) != nullptr) {
    return Kind::kSlider;
  }
  if (qobject_cast<QListWidget *>(twin) != nullptr) {
    return Kind::kList;
  }
  if (qobject_cast<QPlainTextEdit *>(twin) != nullptr) {
    return Kind::kText;
  }
  return Kind::kPlain;
}

/// The enabled state, the explicit hide and the tool tip.
int
sync_common(const QWidget & original, QWidget & twin)
{
  int writes = 0;
  const bool off = original.testAttribute(Qt::WA_ForceDisabled);
  if (twin.testAttribute(Qt::WA_ForceDisabled) != off) {
    twin.setEnabled(!off);
    ++writes;
  }
  const bool hidden = explicitly_hidden(original);
  if (explicitly_hidden(twin) != hidden) {
    twin.setVisible(!hidden);
    ++writes;
  }
  if (twin.toolTip() != original.toolTip()) {
    twin.setToolTip(original.toolTip());
    ++writes;
  }
  return writes;
}

int
sync_button(const QAbstractButton & o, QAbstractButton & t)
{
  int writes = 0;
  if (t.text() != o.text()) {
    t.setText(o.text());
    ++writes;
  }
  if (t.isCheckable() != o.isCheckable()) {
    t.setCheckable(o.isCheckable());
    ++writes;
  }
  if (t.isChecked() != o.isChecked()) {
    t.setChecked(o.isChecked());
    ++writes;
  }
  return writes;
}

int
sync_group(const QGroupBox & o, QGroupBox & t)
{
  int writes = 0;
  if (t.title() != o.title()) {
    t.setTitle(o.title());
    ++writes;
  }
  if (t.isCheckable() != o.isCheckable()) {
    t.setCheckable(o.isCheckable());
    ++writes;
  }
  if (t.isChecked() != o.isChecked()) {
    t.setChecked(o.isChecked());
    ++writes;
  }
  return writes;
}

int
sync_label(const QLabel & o, QLabel & t)
{
  if (t.text() == o.text()) {
    return 0;
  }
  t.setText(o.text());
  return 1;
}

bool
combo_items_equal(const QComboBox & o, const QComboBox & t)
{
  if (t.count() != o.count()) {
    return false;
  }
  for (int i = 0; i < o.count(); ++i) {
    if (t.itemText(i) != o.itemText(i) ||
      t.itemData(i, Qt::UserRole) != o.itemData(i, Qt::UserRole) ||
      t.itemData(i, Qt::ToolTipRole) != o.itemData(i, Qt::ToolTipRole))
    {
      return false;
    }
  }
  return true;
}

int
sync_combo(const QComboBox & o, QComboBox & t)
{
  int writes = 0;
  if (!combo_items_equal(o, t)) {
    t.clear();
    for (int i = 0; i < o.count(); ++i) {
      t.addItem(o.itemText(i), o.itemData(i, Qt::UserRole));
      const QVariant tip = o.itemData(i, Qt::ToolTipRole);
      if (tip.isValid()) {
        t.setItemData(i, tip, Qt::ToolTipRole);
      }
    }
    ++writes;
  }
  if (t.currentIndex() != o.currentIndex()) {
    t.setCurrentIndex(o.currentIndex());
    ++writes;
  }
  if (t.isEditable() && o.isEditable() && t.lineEdit() != nullptr &&
    !t.lineEdit()->hasFocus() && t.currentText() != o.currentText())
  {
    t.setEditText(o.currentText());
    ++writes;
  }
  return writes;
}

int
sync_line(const QLineEdit & o, QLineEdit & t)
{
  int writes = 0;
  if (!t.hasFocus() && t.text() != o.text()) {
    t.setText(o.text());
    ++writes;
  }
  if (t.placeholderText() != o.placeholderText()) {
    t.setPlaceholderText(o.placeholderText());
    ++writes;
  }
  if (t.isReadOnly() != o.isReadOnly()) {
    t.setReadOnly(o.isReadOnly());
    ++writes;
  }
  return writes;
}

int
sync_double_spin(const QDoubleSpinBox & o, QDoubleSpinBox & t)
{
  int writes = 0;
  if (t.decimals() != o.decimals()) {
    t.setDecimals(o.decimals());
    ++writes;
  }
  if (t.minimum() != o.minimum() || t.maximum() != o.maximum()) {
    t.setRange(o.minimum(), o.maximum());
    ++writes;
  }
  if (t.singleStep() != o.singleStep()) {
    t.setSingleStep(o.singleStep());
    ++writes;
  }
  if (t.prefix() != o.prefix()) {
    t.setPrefix(o.prefix());
    ++writes;
  }
  if (t.suffix() != o.suffix()) {
    t.setSuffix(o.suffix());
    ++writes;
  }
  if (t.value() != o.value()) {
    t.setValue(o.value());
    ++writes;
  }
  return writes;
}

int
sync_spin(const QSpinBox & o, QSpinBox & t)
{
  int writes = 0;
  if (t.minimum() != o.minimum() || t.maximum() != o.maximum()) {
    t.setRange(o.minimum(), o.maximum());
    ++writes;
  }
  if (t.singleStep() != o.singleStep()) {
    t.setSingleStep(o.singleStep());
    ++writes;
  }
  if (t.prefix() != o.prefix()) {
    t.setPrefix(o.prefix());
    ++writes;
  }
  if (t.suffix() != o.suffix()) {
    t.setSuffix(o.suffix());
    ++writes;
  }
  if (t.value() != o.value()) {
    t.setValue(o.value());
    ++writes;
  }
  return writes;
}

int
sync_slider(const QAbstractSlider & o, QAbstractSlider & t)
{
  int writes = 0;
  if (t.minimum() != o.minimum() || t.maximum() != o.maximum()) {
    t.setRange(o.minimum(), o.maximum());
    ++writes;
  }
  if (t.value() != o.value()) {
    t.setValue(o.value());
    ++writes;
  }
  return writes;
}

bool
list_rows_equal(const QListWidget & o, const QListWidget & t)
{
  static const int kRoles[] = {
    Qt::DisplayRole, Qt::UserRole, Qt::ToolTipRole, Qt::ForegroundRole, Qt::CheckStateRole};
  if (t.count() != o.count()) {
    return false;
  }
  for (int i = 0; i < o.count(); ++i) {
    const QListWidgetItem * a = o.item(i);
    const QListWidgetItem * b = t.item(i);
    if (a->flags() != b->flags()) {
      return false;
    }
    for (const int role : kRoles) {
      if (a->data(role) != b->data(role)) {
        return false;
      }
    }
  }
  return true;
}

int
sync_list(const QListWidget & o, QListWidget & t)
{
  int writes = 0;
  if (!list_rows_equal(o, t)) {
    // The rows are made again, so the view would jump to its top: keep the scroll position.
    const int scroll = t.verticalScrollBar()->value();
    t.clear();
    for (int i = 0; i < o.count(); ++i) {
      t.addItem(o.item(i)->clone());
    }
    t.verticalScrollBar()->setValue(scroll);
    ++writes;
  }
  if (t.currentRow() != o.currentRow()) {
    t.setCurrentRow(o.currentRow());
    ++writes;
  }
  return writes;
}

/// The last kTwinTextBlocks blocks of the document, as plain text.
QString
last_blocks(const QTextDocument & document)
{
  QStringList lines;
  QTextBlock block = document.lastBlock();
  for (int n = 0; n < kTwinTextBlocks && block.isValid(); ++n) {
    lines.append(block.text());
    block = block.previous();
  }
  std::reverse(lines.begin(), lines.end());
  return lines.join(QLatin1Char('\n'));
}

}  // namespace

struct WidgetTwinBinder::Impl
{
  struct Pair
  {
    QPointer<QWidget> original;
    QPointer<QWidget> twin;
    Kind kind = Kind::kPlain;
    std::function<void(QWidget &, QWidget &)> copy;
    int revision = -1;            ///< the document revision of the last text copy
    bool copied = false;          ///< a text copy was made
    std::int64_t copied_ms = 0;   ///< the time of the last text copy
    QPointer<QLineEdit> bound_line;   ///< the twin combo's line edit that is forwarded
    QPointer<ReturnGate> gate;        ///< the Return de-duplication of that line edit
  };

  QObject * owner = nullptr;
  std::vector<Pair> pairs;
  std::vector<QMetaObject::Connection> connections;
  std::function<void(QWidget &, const char *)> observer;
  std::function<std::int64_t()> time_source;
  Counters counters;
  bool syncing = false;
  int forward_depth = 0;

  std::int64_t now_ms() const
  {
    if (time_source) {
      return time_source();
    }
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    using std::chrono::steady_clock;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  bool is_bound(const QWidget * widget) const
  {
    for (const Pair & pair : pairs) {
      if (pair.original.data() == widget || pair.twin.data() == widget) {
        return true;
      }
    }
    return false;
  }

  /// Carries @p action out on @p original, unless it is disabled; @p report says whether the
  /// observer hears of it. An action that returns false did nothing and is not counted.
  template<class Action>
  void act(QWidget * original, const char * what, bool report, Action && action)
  {
    if (syncing || original == nullptr) {
      return;
    }
    if (!original->isEnabled()) {
      ++counters.refused;
      return;
    }
    QPointer<QWidget> guard(original);
    bool did = true;
    {
      DepthScope scope(forward_depth);
      if constexpr (std::is_void_v<decltype(action())>) {
        action();
      } else {
        did = action();
      }
    }
    if (!did) {
      return;
    }
    ++counters.forwarded;
    if (report && observer && !guard.isNull()) {
      observer(*guard, what);
    }
  }

  void connect_forward(Pair & pair);
  void connect_combo_line(Pair & pair);
  int sync_text(Pair & pair, const QPlainTextEdit & o, QPlainTextEdit & t);
};

void
WidgetTwinBinder::Impl::connect_forward(Pair & pair)
{
  QWidget * original = pair.original;
  QWidget * twin = pair.twin;
  switch (pair.kind) {
    case Kind::kButton: {
        auto * t = static_cast<QAbstractButton *>(twin);
        QPointer<QAbstractButton> o(static_cast<QAbstractButton *>(original));
        connections.push_back(
          QObject::connect(
            t, &QAbstractButton::clicked, owner, [this, o]() {
              act(o.data(), "clicked", true, [&o]() {o->click();});
            }));
        break;
      }
    case Kind::kGroup: {
        auto * t = static_cast<QGroupBox *>(twin);
        QPointer<QGroupBox> o(static_cast<QGroupBox *>(original));
        connections.push_back(
          QObject::connect(
            t, &QGroupBox::clicked, owner, [this, o](bool checked) {
              act(o.data(), "clicked", true, [&o, checked]() {
                o->setChecked(checked);
                if (o.isNull()) {return false;}
                QMetaObject::invokeMethod(
                  o.data(), "clicked", Qt::DirectConnection, Q_ARG(bool, checked));
                return true;
              });
            }));
        break;
      }
    case Kind::kCombo: {
        auto * t = static_cast<QComboBox *>(twin);
        QPointer<QComboBox> o(static_cast<QComboBox *>(original));
        connections.push_back(
          QObject::connect(
            t, QOverload<int>::of(&QComboBox::activated), owner, [this, o](int index) {
              if (o.isNull() || index < 0 || index >= o->count()) {
                return;
              }
              act(o.data(), "chosen", true, [&o, index]() {
                // The text is taken first: a slot may delete the combo box.
                const QString text = o->itemText(index);
                o->setCurrentIndex(index);
                if (o.isNull()) {return false;}
                QMetaObject::invokeMethod(
                  o.data(), "activated", Qt::DirectConnection, Q_ARG(int, index));
                if (o.isNull()) {return true;}
                QMetaObject::invokeMethod(
                  o.data(), "textActivated", Qt::DirectConnection, Q_ARG(QString, text));
                return true;
              });
            }));
        connect_combo_line(pair);
        break;
      }
    case Kind::kLine: {
        auto * t = static_cast<QLineEdit *>(twin);
        QPointer<QLineEdit> o(static_cast<QLineEdit *>(original));
        connections.push_back(
          QObject::connect(
            t, &QLineEdit::textEdited, owner, [this, o](const QString & text) {
              act(o.data(), "edited", false, [&o, &text]() {
                o->setText(text);
                if (o.isNull()) {return false;}
                QMetaObject::invokeMethod(
                  o.data(), "textEdited", Qt::DirectConnection, Q_ARG(QString, text));
                return true;
              });
            }));
        connections.push_back(
          QObject::connect(
            t, &QLineEdit::editingFinished, owner, [this, o]() {
              act(o.data(), "edited", true, [&o]() {
                QMetaObject::invokeMethod(o.data(), "editingFinished", Qt::DirectConnection);
              });
            }));
        connections.push_back(
          QObject::connect(
            t, &QLineEdit::returnPressed, owner, [this, o]() {
              act(o.data(), "edited", true, [&o]() {
                QMetaObject::invokeMethod(o.data(), "returnPressed", Qt::DirectConnection);
              });
            }));
        break;
      }
    case Kind::kDoubleSpin: {
        auto * t = static_cast<QDoubleSpinBox *>(twin);
        QPointer<QDoubleSpinBox> o(static_cast<QDoubleSpinBox *>(original));
        connections.push_back(
          QObject::connect(
            t, QOverload<double>::of(&QDoubleSpinBox::valueChanged), owner,
            [this, o](double value) {
              act(o.data(), "stepped", true, [&o, value]() {o->setValue(value);});
            }));
        break;
      }
    case Kind::kSpin: {
        auto * t = static_cast<QSpinBox *>(twin);
        QPointer<QSpinBox> o(static_cast<QSpinBox *>(original));
        connections.push_back(
          QObject::connect(
            t, QOverload<int>::of(&QSpinBox::valueChanged), owner, [this, o](int value) {
              act(o.data(), "stepped", true, [&o, value]() {o->setValue(value);});
            }));
        break;
      }
    case Kind::kSlider: {
        auto * t = static_cast<QAbstractSlider *>(twin);
        QPointer<QAbstractSlider> o(static_cast<QAbstractSlider *>(original));
        connections.push_back(
          QObject::connect(
            t, &QAbstractSlider::valueChanged, owner, [this, o](int value) {
              act(o.data(), "stepped", true, [&o, value]() {o->setValue(value);});
            }));
        break;
      }
    case Kind::kList: {
        auto * t = static_cast<QListWidget *>(twin);
        QPointer<QListWidget> o(static_cast<QListWidget *>(original));
        connections.push_back(
          QObject::connect(
            t, &QListWidget::currentRowChanged, owner, [this, o](int row) {
              act(o.data(), "row chosen", true, [&o, row]() {o->setCurrentRow(row);});
            }));
        break;
      }
    default:
      break;
  }
}

/// Forwards the typing and the Return of the twin combo box's line edit to the original's. The
/// line edit exists only while the combo box is editable, so sync() calls this again for a combo
/// box that was made editable (or editable again) after the bind.
void
WidgetTwinBinder::Impl::connect_combo_line(Pair & pair)
{
  auto * t = static_cast<QComboBox *>(pair.twin.data());
  QLineEdit * te = t->lineEdit();
  if (te == nullptr || te == pair.bound_line.data()) {
    return;
  }
  delete pair.gate.data();
  pair.bound_line = te;
  auto * gate = new ReturnGate(owner, te, t);
  pair.gate = gate;
  QPointer<QComboBox> o(static_cast<QComboBox *>(pair.original.data()));
  QPointer<QLineEdit> line(te);
  QPointer<ReturnGate> guard(gate);
  connections.push_back(
    QObject::connect(
      te, &QLineEdit::textEdited, owner, [this, o](const QString & text) {
        act(o.data(), "edited", false, [&o, &text]() {
          QLineEdit * oe = o->lineEdit();
          if (oe == nullptr) {return false;}
          oe->setText(text);
          return true;
        });
      }));
  connections.push_back(
    QObject::connect(
      te, &QLineEdit::returnPressed, owner, [this, o, line, guard]() {
        if (guard && !guard->admit()) {
          return;
        }
        act(o.data(), "edited", true, [&o, &line]() {
          QPointer<QLineEdit> oe(o->lineEdit());
          if (line.isNull() || oe.isNull()) {return false;}
          oe->setText(line->text());
          QMetaObject::invokeMethod(oe.data(), "returnPressed", Qt::DirectConnection);
          return true;
        });
      }));
}

int
WidgetTwinBinder::Impl::sync_text(Pair & pair, const QPlainTextEdit & o, QPlainTextEdit & t)
{
  if (!t.isVisible()) {
    return 0;
  }
  const int revision = o.document()->revision();
  if (!pair.copied && o.document()->isEmpty() && t.document()->isEmpty()) {
    // Nothing to show yet: remember the revision so that the first change is copied.
    pair.revision = revision;
    pair.copied = true;
    pair.copied_ms = now_ms() - kTwinTextPeriodMs;
    return 0;
  }
  if (pair.copied && revision == pair.revision) {
    return 0;
  }
  const std::int64_t now = now_ms();
  if (pair.copied && now - pair.copied_ms < kTwinTextPeriodMs) {
    return 0;
  }
  t.setPlainText(last_blocks(*o.document()));
  t.moveCursor(QTextCursor::End);
  t.verticalScrollBar()->setValue(t.verticalScrollBar()->maximum());
  pair.revision = revision;
  pair.copied = true;
  pair.copied_ms = now;
  return 1;
}

WidgetTwinBinder::WidgetTwinBinder(QObject * parent)
: QObject(parent), impl_(std::make_unique<Impl>())
{
  impl_->owner = this;
}

WidgetTwinBinder::~WidgetTwinBinder()
{
  release();
}

bool
WidgetTwinBinder::bind(QWidget * original, QWidget * twin)
{
  if (original == nullptr || twin == nullptr || original == twin || !same_class(*original, *twin) ||
    impl_->is_bound(original) || impl_->is_bound(twin))
  {
    return false;
  }
  Impl::Pair pair;
  pair.original = original;
  pair.twin = twin;
  pair.kind = kind_of(twin);
  impl_->pairs.push_back(std::move(pair));
  if (impl_->pairs.back().kind == Kind::kPlain) {
    ++impl_->counters.plain_pairs;
  }
  impl_->connect_forward(impl_->pairs.back());
  return true;
}

bool
WidgetTwinBinder::bind_custom(
  QWidget * original, QWidget * twin, std::function<void(QWidget &, QWidget &)> copy)
{
  if (original == nullptr || twin == nullptr || original == twin || !copy ||
    impl_->is_bound(original) || impl_->is_bound(twin))
  {
    return false;
  }
  Impl::Pair pair;
  pair.original = original;
  pair.twin = twin;
  pair.kind = Kind::kCustom;
  pair.copy = std::move(copy);
  impl_->pairs.push_back(std::move(pair));
  return true;
}

int
WidgetTwinBinder::sync()
{
  Impl & d = *impl_;
  if (d.syncing) {
    return 0;
  }
  FlagScope scope(d.syncing);
  int writes = 0;
  for (Impl::Pair & pair : d.pairs) {
    QWidget * o = pair.original;
    QWidget * t = pair.twin;
    if (o == nullptr || t == nullptr) {
      continue;
    }
    writes += sync_common(*o, *t);
    switch (pair.kind) {
      case Kind::kCustom:
        pair.copy(*o, *t);
        break;
      case Kind::kButton:
        writes += sync_button(
          *static_cast<QAbstractButton *>(o), *static_cast<QAbstractButton *>(t));
        break;
      case Kind::kGroup:
        writes += sync_group(*static_cast<QGroupBox *>(o), *static_cast<QGroupBox *>(t));
        break;
      case Kind::kLabel:
        writes += sync_label(*static_cast<QLabel *>(o), *static_cast<QLabel *>(t));
        break;
      case Kind::kCombo:
        d.connect_combo_line(pair);
        writes += sync_combo(*static_cast<QComboBox *>(o), *static_cast<QComboBox *>(t));
        break;
      case Kind::kLine:
        writes += sync_line(*static_cast<QLineEdit *>(o), *static_cast<QLineEdit *>(t));
        break;
      case Kind::kDoubleSpin:
        writes += sync_double_spin(
          *static_cast<QDoubleSpinBox *>(o), *static_cast<QDoubleSpinBox *>(t));
        break;
      case Kind::kSpin:
        writes += sync_spin(*static_cast<QSpinBox *>(o), *static_cast<QSpinBox *>(t));
        break;
      case Kind::kSlider:
        writes += sync_slider(
          *static_cast<QAbstractSlider *>(o), *static_cast<QAbstractSlider *>(t));
        break;
      case Kind::kList:
        writes += sync_list(*static_cast<QListWidget *>(o), *static_cast<QListWidget *>(t));
        break;
      case Kind::kText:
        writes += d.sync_text(
          pair, *static_cast<QPlainTextEdit *>(o), *static_cast<QPlainTextEdit *>(t));
        break;
      case Kind::kPlain:
        break;
    }
  }
  ++d.counters.passes;
  d.counters.writes += static_cast<std::uint64_t>(writes);
  return writes;
}

void
WidgetTwinBinder::release()
{
  for (const QMetaObject::Connection & connection : impl_->connections) {
    QObject::disconnect(connection);
  }
  impl_->connections.clear();
  for (Impl::Pair & pair : impl_->pairs) {
    delete pair.gate.data();
  }
  impl_->pairs.clear();
  impl_->counters.plain_pairs = 0;
}

void
WidgetTwinBinder::set_forward_observer(std::function<void(QWidget &, const char *)> observer)
{
  impl_->observer = std::move(observer);
}

void
WidgetTwinBinder::set_time_source(std::function<std::int64_t()> now_ms)
{
  impl_->time_source = std::move(now_ms);
}

WidgetTwinBinder::Counters
WidgetTwinBinder::counters() const
{
  Counters counters = impl_->counters;
  counters.pairs = impl_->pairs.size();
  return counters;
}

bool
WidgetTwinBinder::forwarding() const
{
  return impl_->forward_depth > 0;
}

}  // namespace qar_qt_streaming
