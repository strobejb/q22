#include "menucombobox.h"
#include "theme.h"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizeGrip>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QTimer>

static int kPad() { return qMax(1, qRound(qApp->devicePixelRatio() * 2.0)); }

namespace {
class FileDialogComboPopupFilter : public QObject
{
public:
    using QObject::QObject;

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        auto *combo = qobject_cast<QComboBox *>(obj);
        if (!combo)
            return false;

        if (event->type() == QEvent::Paint) {
            QStylePainter p(combo);
            QStyleOptionComboBox opt;
            opt.initFrom(combo);
            opt.currentText       = combo->currentText();
            opt.currentIcon       = combo->itemIcon(combo->currentIndex());
            opt.iconSize          = combo->iconSize();
            opt.editable          = combo->isEditable();
            opt.frame             = true;
            opt.subControls       = QStyle::SC_ComboBoxFrame | QStyle::SC_ComboBoxArrow
                                  | QStyle::SC_ComboBoxEditField;
            opt.activeSubControls = QStyle::SC_None;
            p.drawComplexControl(QStyle::CC_ComboBox, opt);
            p.drawControl(QStyle::CE_ComboBoxLabel, opt);
            // Explicitly draw the arrow: QComboBox::drop-down { border: none } suppresses it.
            QStyleOptionComboBox arrowOpt = opt;
            arrowOpt.rect = combo->style()->subControlRect(
                QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, combo);
            p.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                showMenu(combo);
                return true;
            }
        }

        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            const bool opensPopup = key->key() == Qt::Key_Space
                                 || key->key() == Qt::Key_Return
                                 || key->key() == Qt::Key_Enter
                                 || key->key() == Qt::Key_F4
                                 || (key->key() == Qt::Key_Down
                                     && (key->modifiers() & Qt::AltModifier));
            if (opensPopup) {
                showMenu(combo);
                return true;
            }
        }

        return false;
    }

private:
    static void setPopupOpen(QComboBox *combo, bool open)
    {
        combo->setProperty("popupOpen", open);
        combo->style()->unpolish(combo);
        combo->style()->polish(combo);
        combo->update();
    }

    static void showMenu(QComboBox *combo)
    {
        if (!combo->isEnabled() || combo->count() <= 0)
            return;

        auto *menu = new QMenu(combo);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setMinimumWidth(combo->width());
        themeMenu(menu);

        for (int i = 0; i < combo->count(); ++i) {
            QAction *action = combo->itemText(i).isEmpty()
                            ? menu->addSeparator()
                            : menu->addAction(combo->itemIcon(i), combo->itemText(i));
            action->setData(i);
            action->setCheckable(!combo->itemText(i).isEmpty());
            action->setChecked(i == combo->currentIndex());
            action->setEnabled(combo->model()->index(i, combo->modelColumn()).flags()
                               & Qt::ItemIsEnabled);
        }

        QObject::connect(menu, &QMenu::triggered, combo, [combo](QAction *action) {
            const int idx = action->data().toInt();
            if (idx < 0 || idx >= combo->count() || action->isSeparator())
                return;

            combo->setCurrentIndex(idx);
            QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection, Q_ARG(int, idx));
            QMetaObject::invokeMethod(combo, "textActivated", Qt::DirectConnection,
                                      Q_ARG(QString, combo->itemText(idx)));
        });
        QObject::connect(menu, &QMenu::aboutToHide, combo, [combo]() {
            setPopupOpen(combo, false);
        });

        setPopupOpen(combo, true);
        menu->popup(smartMenuPos(combo, menu, false));
    }
};

static int fileDialogInputHeight(QFileDialog *dialog)
{
    int height = 0;
    if (auto *edit = dialog->findChild<QWidget *>(QStringLiteral("fileNameEdit")))
        height = qMax(height, edit->sizeHint().height());
    if (auto *combo = dialog->findChild<QWidget *>(QStringLiteral("fileTypeCombo")))
        height = qMax(height, combo->sizeHint().height());
    return height;
}

static int fileDialogRowSpacing(QFileDialog *dialog)
{
    if (auto *grid = qobject_cast<QGridLayout *>(dialog->layout()))
        return grid->verticalSpacing();
    if (QLayout *layout = dialog->layout())
        return layout->spacing();
    return -1;
}

static void alignFileDialogButtons(QFileDialog *dialog)
{
    if (!dialog)
        return;

    dialog->ensurePolished();
    if (QLayout *layout = dialog->layout())
        layout->activate();

    const int height = fileDialogInputHeight(dialog);
    if (height <= 0)
        return;

    const int rowSpacing = fileDialogRowSpacing(dialog);
    for (auto *box : dialog->findChildren<QDialogButtonBox *>()) {
        box->setContentsMargins(0, 0, 0, 0);
        if (QLayout *boxLayout = box->layout()) {
            boxLayout->setContentsMargins(0, 0, 0, 0);
            if (rowSpacing >= 0)
                boxLayout->setSpacing(rowSpacing);
        }

        for (QAbstractButton *button : box->buttons()) {
            if (auto *push = qobject_cast<QPushButton *>(button)) {
                push->setMinimumHeight(height);
                push->setFixedHeight(height);
            }
        }
    }
}
} // namespace

MenuComboBox::MenuComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_menu = new QMenu(this);
    themeMenu(m_menu);

    connect(m_menu, &QMenu::aboutToHide, this,
            [this]() { recordMenuClose(); setPopupOpen(false); });
}

void MenuComboBox::setPopupOpen(bool open)
{
    if (!open)
        QComboBox::hidePopup();   // clears Qt's internal State_Sunken arrow flag
    setProperty("popupOpen", open);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

QSize MenuComboBox::sizeHint() const
{
    QSize s = QComboBox::sizeHint();
    if (!m_leadingIcon.isNull())
        s.setWidth(s.width() + fontMetrics().height() + 8);
    return { s.width(), s.height() + 2 * kPad() };
}

QSize MenuComboBox::minimumSizeHint() const
{
    QSize s = QComboBox::minimumSizeHint();
    if (!m_leadingIcon.isNull())
        s.setWidth(s.width() + fontMetrics().height() + 8);
    return { s.width(), s.height() + 2 * kPad() };
}

void MenuComboBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    // Inject State_On when the menu is open so that:
    //  1. NoFocusRectStyle's sync code sees State_On matching popupOpen=true
    //     and does not queue a reset.
    //  2. Fusion treats the combo as open/pressed for the darker gradient.
    // Clear State_MouseOver so Fusion picks the sunken path, not the hover path.
    if (property("popupOpen").toBool()) {
        opt.state |= QStyle::State_On;
        opt.state &= ~QStyle::State_MouseOver;
    } else {
        opt.state &= ~(QStyle::State_On | QStyle::State_Sunken);
    }
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    // Draw label: if there's a leading icon, render it then draw the text
    // manually so we can offset the text rect (same approach as DataTypeComboBox).
    QRect textRect = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
    if (!m_leadingIcon.isNull()) {
        const int iconSz = fontMetrics().height();
        const QRect iconRect(textRect.left(),
                             textRect.top() + (textRect.height() - iconSz) / 2,
                             iconSz, iconSz);
        m_leadingIcon.paint(&painter, iconRect);
        textRect.setLeft(iconRect.right() + 8);
        style()->drawItemText(&painter, textRect,
                              Qt::AlignLeft | Qt::AlignVCenter,
                              opt.palette, isEnabled(),
                              opt.currentText, QPalette::ButtonText);
    } else {
        painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
    }

    // The ::drop-down stylesheet rule suppresses the native arrow; draw it explicitly.
    QStyleOptionComboBox arrowOpt = opt;
    arrowOpt.rect = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
    painter.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
}

void MenuComboBox::setLeadingIcon(const QIcon &icon)
{
    m_leadingIcon = icon;
    updateGeometry();
    update();
}

void MenuComboBox::buildMenu()
{
    m_menu->clear();
    const int cur = currentIndex();
    QString currentSection;
    bool hasAction = false;
    for (int i = 0; i < count(); ++i) {
        const QString text = itemText(i);
        if (text.isEmpty()) {
            m_menu->addSeparator();
            currentSection.clear();
            continue;
        }
        const QString section = itemData(i, SectionRole).toString();
        if (!section.isEmpty() && section != currentSection) {
            if (hasAction)
                m_menu->addSection(section);
            currentSection = section;
        }
        const QString detail = itemData(i, DetailRole).toString();
        QAction *a = m_menu->addAction(detail.isEmpty() ? text : text + QLatin1Char('\t') + detail);
        hasAction = true;
        a->setCheckable(true);
        a->setChecked(i == cur);
        connect(a, &QAction::triggered, this, [this, i]() {
            setCurrentIndex(i);
            emit activated(i);
            emit textActivated(itemText(i));
        });
    }
}

bool MenuComboBox::isSameClickReopen()
{
    const QPoint cur = QCursor::pos();
    const bool same = (m_closePos == cur);
    m_closePos = { -1, -1 };
    return same;
}

void MenuComboBox::showPopup()
{
    if (m_menu->isVisible()) { m_menu->hide(); return; }
    if (isSameClickReopen()) return;

    buildMenu();
    m_menu->setMinimumWidth(width());
    const QPoint pos = smartMenuPos(this, m_menu, /*rightAlign=*/false);
    m_menu->popup(pos);
    setPopupOpen(true);
}

void installThemedFileDialogComboPopups(QFileDialog *dialog)
{
    dialog->setSizeGripEnabled(false);
    // Non-native QFileDialog owns ordinary, private QComboBox controls for
    // "Look in" and the file-type filter. Their built-in QComboBox popup has
    // the same rectangular clipping / missing-shadow artefacts that led to
    // MenuComboBox, but the controls themselves cannot be replaced cleanly.
    // Intercept the popup gesture and show a themed QMenu while still updating
    // the original combo and emitting the activation signals QFileDialog uses.
    auto *filter = new FileDialogComboPopupFilter(dialog);
    const auto combos = dialog->findChildren<QComboBox *>();
    for (QComboBox *combo : combos)
        combo->installEventFilter(filter);

    // Qt's non-native QFileDialog ships with tighter layout margins than the
    // app's own dialogs.  Set an app-style outer gutter here so every themed
    // file dialog gets the same breathing room before custom rows/title chrome
    // are added.
    if (QLayout *layout = dialog->layout()) {
        const QMargins m = layout->contentsMargins();
        layout->setContentsMargins(qMax(20, m.left()), qMax(20, m.top()),
                                   qMax(20, m.right()), qMax(20, m.bottom()));
        layout->setSpacing(qMax(8, layout->spacing()));
    }

    // The QDialogButtonBox spans the filename-edit and filetype-combo rows.
    // Sync its push buttons and internal spacing to the styled grid rows so
    // Open/Save aligns with the edit row and Cancel aligns with the combo row.
    alignFileDialogButtons(dialog);
    QTimer::singleShot(0, dialog, [dialog]() {
        alignFileDialogButtons(dialog);
    });

    for (auto *grip : dialog->findChildren<QSizeGrip *>())
        grip->hide();
}
