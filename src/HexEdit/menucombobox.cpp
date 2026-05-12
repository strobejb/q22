#include "menucombobox.h"
#include "theme.h"
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QFileDialog>
#include <QLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizeGrip>
#include <QStyleOptionComboBox>
#include <QStylePainter>

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
    return { s.width(), s.height() + 2 * kPad() };
}

QSize MenuComboBox::minimumSizeHint() const
{
    QSize s = QComboBox::minimumSizeHint();
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
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
    // The ::drop-down stylesheet rule suppresses the native arrow; draw it explicitly.
    QStyleOptionComboBox arrowOpt = opt;
    arrowOpt.rect = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, this);
    painter.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
}

void MenuComboBox::buildMenu()
{
    m_menu->clear();
    const int cur = currentIndex();
    for (int i = 0; i < count(); ++i) {
        const QString text = itemText(i);
        if (text.isEmpty()) {
            m_menu->addSeparator();
            continue;
        }
        QAction *a = m_menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(i == cur);
        connect(a, &QAction::triggered, this, [this, i]() {
            setCurrentIndex(i);
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

    // The QDialogButtonBox spans the filename-edit row and the filetype-combo
    // row.  QSS padding arithmetic should make them equal, but Qt's internal
    // sizeFromContents for buttons can differ from that for input controls.
    // Measure the actual sizeHint of the filename edit (already styled by QSS)
    // and enforce that as the minimum height on every button in the dialog.
    if (auto *ref = dialog->findChild<QWidget *>("fileNameEdit")) {
        const int h = ref->sizeHint().height();
        for (auto *btn : dialog->findChildren<QPushButton *>())
            btn->setMinimumHeight(h);
    }

    for (auto *grip : dialog->findChildren<QSizeGrip *>())
        grip->hide();
}
