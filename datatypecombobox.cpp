#include "datatypecombobox.h"
#include "theme.h"
#include <QActionGroup>
#include <QEvent>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QStyleOptionMenuItem>

DataTypeComboBox::DataTypeComboBox(QWidget *parent)
    : ValueComboBox(parent)
{
    m_menu = new QMenu(this);
    themeMenu(m_menu);
    m_menu->installEventFilter(this);
}

void DataTypeComboBox::buildMenu()
{
    m_menu->clear();
    m_actions.clear();

    auto *group = new QActionGroup(m_menu);
    group->setExclusive(true);

    for (int i = 0; i < count(); ++i) {
        const QString text = itemText(i);
        if (text.isEmpty()) {
            m_menu->addSeparator();
            continue;
        }
        const int actionIndex = m_actions.size();
        QAction *a = m_menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(actionIndex == m_selection);
        group->addAction(a);
        m_actions.append(a);
        connect(a, &QAction::toggled, this, [this, actionIndex](bool checked) {
            if (checked) { m_selection = actionIndex; emit selectionChanged(actionIndex); }
        });
    }
}

QString DataTypeComboBox::selectionText() const
{
    if (m_selection >= 0 && m_selection < m_actions.size())
        return m_actions[m_selection]->text();
    return {};
}

// Render a menu item twice — once with text, once without — and return the x
// of the first pixel column where they differ. This is the exact text start
// position regardless of style, theme, or DPI.
static int measureMenuItemTextX(QMenu *menu, QAction *action)
{
    const QRect item = menu->actionGeometry(action);
    if (!item.isValid()) return 0;

    const int W = item.width(), H = item.height();

    auto render = [&](const QString &text) {
        QImage img(W, H, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        QStyleOptionMenuItem o;
        o.initFrom(menu);
        o.rect               = img.rect();
        o.text               = text;
        o.menuItemType       = QStyleOptionMenuItem::Normal;
        o.checkType          = QStyleOptionMenuItem::Exclusive;
        o.menuHasCheckableItems = true;
        o.checked            = false;
        o.maxIconWidth       = 0;
        o.state              = QStyle::State_Enabled;
        menu->style()->drawControl(QStyle::CE_MenuItem, &o, &p, menu);
        return img;
    };

    const QImage withText = render("X");
    const QImage noText   = render("");

    const int midY = H / 2;
    for (int x = 0; x < W; ++x) {
        const QRgb a = withText.pixel(x, midY);
        const QRgb b = noText.pixel(x, midY);
        if (qAbs(int(qRed(a))   - int(qRed(b)))   > 8 ||
            qAbs(int(qGreen(a)) - int(qGreen(b))) > 8 ||
            qAbs(int(qBlue(a))  - int(qBlue(b)))  > 8 ||
            qAbs(int(qAlpha(a)) - int(qAlpha(b))) > 8) {
            return item.left() + x;
        }
    }
    return 0;
}

static constexpr int kPad = 3;

QSize DataTypeComboBox::sizeHint() const
{
    QSize s = ValueComboBox::sizeHint();
    return { s.width(), s.height() + 2 * kPad };
}

QSize DataTypeComboBox::minimumSizeHint() const
{
    QSize s = ValueComboBox::minimumSizeHint();
    return { s.width(), s.height() + 2 * kPad };
}

void DataTypeComboBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = selectionText();
    // Draw the frame at full widget size.
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    // Draw text inset by kPad within the edit-field subcontrol.
    const QRect textRect = style()->subControlRect(
                               QStyle::CC_ComboBox, &opt,
                               QStyle::SC_ComboBoxEditField, this)
                           .adjusted(kPad + 2, kPad, -(kPad + 2), -kPad);
    style()->drawItemText(&painter, textRect,
                          Qt::AlignLeft | Qt::AlignVCenter,
                          opt.palette, isEnabled(),
                          selectionText(), QPalette::ButtonText);
}

void DataTypeComboBox::showPopup()
{
    QStyleOptionComboBox cbOpt;
    initStyleOption(&cbOpt);
    int comboTextX = style()->subControlRect(QStyle::CC_ComboBox, &cbOpt,
                                             QStyle::SC_ComboBoxEditField, this).left();
    m_targetTextScreenX = mapToGlobal(QPoint(comboTextX, 0)).x();
    m_menu->popup(mapToGlobal(QPoint(0, height())));
}

bool DataTypeComboBox::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_menu && e->type() == QEvent::Show && !m_actions.isEmpty()) {
        const int menuTextX = measureMenuItemTextX(m_menu, m_actions.first());
        if (menuTextX > 0)
            m_menu->move(m_targetTextScreenX - menuTextX, m_menu->pos().y());
    }
    return ValueComboBox::eventFilter(obj, e);
}
