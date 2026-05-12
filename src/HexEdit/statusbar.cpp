#include "statusbar.h"

#include "HexView/hexview.h"
#include "theme.h"

#include <algorithm>
#include <cstring>

#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLocale>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSizePolicy>

PanelStrip::PanelStrip(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(0);
}

void PanelStrip::addPanel(QWidget *w)
{
    w->setParent(this);
    m_panels.append(w);
}

void PanelStrip::setOverlapGuard(QWidget *w)
{
    m_overlapGuard = w;
    if (w)
        w->installEventFilter(this);
}

bool PanelStrip::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_overlapGuard) {
        const auto t = e->type();
        if (t == QEvent::Show || t == QEvent::Hide || t == QEvent::Resize)
            checkOverlap();
    }
    return QWidget::eventFilter(obj, e);
}

void PanelStrip::checkOverlap()
{
    // Both widgets share the status bar as parent, so subtracting positions
    // gives the guard rect in strip-local coordinates.
    QRect guardRect;
    if (m_overlapGuard && m_overlapGuard->isVisible())
        guardRect = QRect(m_overlapGuard->pos() - pos(), m_overlapGuard->size());

    for (QWidget *p : m_panels)
        p->setVisible(guardRect.isEmpty() || !guardRect.intersects(p->geometry()));
}

QSize PanelStrip::sizeHint() const
{
    int w = 0, h = 0;
    for (auto *p : m_panels) {
        w += p->sizeHint().width();
        h = qMax(h, p->sizeHint().height());
    }
    return { w, h };
}

QSize PanelStrip::minimumSizeHint() const
{
    return { 0, sizeHint().height() };
}

void PanelStrip::layoutPanels()
{
    // Place panels right-to-left at their preferred widths. Panels that
    // overflow the left edge are clipped at the widget boundary.
    static constexpr int kRightMargin = 12;
    int x = width() - kRightMargin;
    for (int i = m_panels.count() - 1; i >= 0; --i) {
        QWidget *p = m_panels[i];
        const int pw = p->sizeHint().width();
        const int ph = p->sizeHint().height();
        const int py = (height() - ph) / 2;
        x -= pw;
        p->setGeometry(x, py, pw, ph);
    }
    checkOverlap();
}

void PanelStrip::resizeEvent(QResizeEvent *)
{
    layoutPanels();
}

bool PanelStrip::event(QEvent *e)
{
    if (e->type() == QEvent::LayoutRequest)
        layoutPanels();
    return QWidget::event(e);
}

static constexpr uint indexToMode[] = { HVMODE_OVERWRITE, HVMODE_INSERT, HVMODE_READONLY };
static constexpr const char *indexToLabel[] = { "OVR", "INS", "READ" };

static int modeToIndex(uint mode)
{
    for (int i = 0; i < 3; ++i) {
        if (indexToMode[i] == mode)
            return i;
    }
    return 0;
}

static QString fmtHex(quint64 v, int width)
{
    return QString::number(v, 16).toUpper().rightJustified(width, '0');
}

StatusBar::StatusBar(HexView *hv, QStatusBar *bar, QObject *parent)
    : QObject(parent), m_hv(hv)
{
    m_comboCursor = new RadioComboBox({ "Hexadecimal", "Decimal" }, bar);
    connect(m_comboCursor, &RadioComboBox::selectionChanged, this, [this] { update(); });

    m_comboLength = new RadioComboBox({ "Hexadecimal", "Decimal" }, bar);
    m_comboLength->setSelection(1);
    connect(m_comboLength, &RadioComboBox::selectionChanged, this, [this] { update(); });

    m_comboValue = new ValueOptionsComboBox(bar);
    connect(m_comboValue, &ValueOptionsComboBox::optionsChanged, this, [this] { update(); });

    m_comboMode = new RadioComboBox({ "Overwrite", "Insert", "Readonly" }, bar);
    connect(m_comboMode, &RadioComboBox::selectionChanged, this, [this](int idx) {
        m_hv->setEditMode(indexToMode[idx]);
        update();
    });

    auto *strip = new PanelStrip(bar);
    strip->addPanel(m_comboCursor);
    strip->addPanel(m_comboLength);
    strip->addPanel(m_comboValue);
    strip->addPanel(m_comboMode);

    const QColor borderColor = themeBorderColor();

    auto makeSep = [&]() {
        auto *sep = new QWidget(bar);
        sep->setFixedWidth(1);
        sep->setStyleSheet(QString("background: %1;").arg(borderColor.name()));
        sep->hide();
        return sep;
    };

    auto *searchContainer = new QWidget(bar);
    auto *searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(8, 0, 8, 0);
    searchLayout->setSpacing(6);
    m_searchLabel = new QLabel("Searching:", searchContainer);
    m_searchBar = new QProgressBar(searchContainer);
    m_searchBar->setRange(0, 100);
    m_searchBar->setValue(0);
    m_searchBar->setFixedWidth(160);
    m_searchBar->setTextVisible(false);
    searchLayout->addWidget(m_searchLabel);
    searchLayout->addWidget(m_searchBar);
    m_searchWidget = searchContainer;
    bar->addWidget(m_searchWidget);
    m_searchWidget->hide();
    m_searchSep = makeSep();
    bar->addWidget(m_searchSep);

    m_patternLabel = new QLabel(bar);
    m_patternLabel->setContentsMargins(8, 0, 8, 0);
    bar->addWidget(m_patternLabel);
    m_patternLabel->hide();
    m_patternSep = makeSep();
    bar->addWidget(m_patternSep);

    bar->addPermanentWidget(strip, 1);
    bar->setMinimumWidth(0);
    strip->setOverlapGuard(m_patternLabel);

    update();
}

QString StatusBar::computeValueText() const
{
    static const int sizes[] = { 1, 2, 4, 8, 4, 8 };
    const int ds = m_comboValue->dataSize();
    const int n = sizes[ds];

    uint8_t buf[8] = {};
    if ((int)m_hv->getData(m_hv->cursorOffset(), buf, n) < n)
        return "Value: ---";

    if (m_comboValue->isBigEndian() && n > 1)
        std::reverse(buf, buf + n);

    const bool doHex = m_comboValue->isHex();
    const bool doSigned = m_comboValue->isSigned();

    if (ds == 4) {
        if (doHex) {
            uint32_t r;
            memcpy(&r, buf, 4);
            return QString("Value: 0x%1").arg(fmtHex(r, 8));
        }
        float f;
        memcpy(&f, buf, 4);
        return QString("Value: %1").arg(double(f), 0, 'g', 7);
    }
    if (ds == 5) {
        if (doHex) {
            uint64_t r;
            memcpy(&r, buf, 8);
            return QString("Value: 0x%1").arg(fmtHex(r, 16));
        }
        double d;
        memcpy(&d, buf, 8);
        return QString("Value: %1").arg(d, 0, 'g', 15);
    }

    uint64_t raw = 0;
    memcpy(&raw, buf, n);
    if (doHex)
        return QString("Value: 0x%1").arg(fmtHex(raw, n * 2));

    if (doSigned) {
        int64_t v;
        switch (ds) {
        case 0:
            v = static_cast<int8_t>(buf[0]);
            break;
        case 1: {
            int16_t t;
            memcpy(&t, buf, 2);
            v = t;
            break;
        }
        case 2: {
            int32_t t;
            memcpy(&t, buf, 4);
            v = t;
            break;
        }
        default:
            memcpy(&v, buf, 8);
            break;
        }
        return QString("Value: %1").arg(v);
    }
    return QString("Value: %1").arg(raw);
}

void StatusBar::onFindProgress(size_w pos, size_w len, double mbPerSec)
{
    if (!m_searchWidget->isVisible()) {
        m_searchWidget->show();
        m_searchSep->show();
    }
    const int pct = (len > 0) ? (int)((double)pos / (double)len * 100.0) : 0;
    m_searchBar->setValue(qBound(0, pct, 100));
    m_searchLabel->setText(QString("Searching %1 MB/s:").arg(mbPerSec, 0, 'f', 1));
    m_searchLabel->setMinimumWidth(qMax(m_searchLabel->minimumWidth(),
                                        m_searchLabel->sizeHint().width()));
}

void StatusBar::onFindDone()
{
    m_searchWidget->hide();
    m_searchSep->hide();
    m_searchLabel->setMinimumWidth(0);
}

void StatusBar::showSearchHex(const QString &hex)
{
    if (hex.isEmpty()) {
        m_patternLabel->hide();
        m_patternSep->hide();
        return;
    }
    static const QRegularExpression rxHex("^[0-9A-Fa-f ]+$");
    if (rxHex.match(hex).hasMatch())
        m_patternLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    else
        m_patternLabel->setFont(QApplication::font());

    m_patternLabel->setText(hex);
    m_patternLabel->show();
    m_patternSep->show();
}

void StatusBar::showMessage(const QString &msg)
{
    if (msg.isEmpty()) {
        m_patternLabel->hide();
        m_patternSep->hide();
        return;
    }
    m_patternLabel->setFont(QApplication::font());
    m_patternLabel->setText(msg);
    m_patternLabel->show();
    m_patternSep->show();
}

void StatusBar::update()
{
    const bool hasSel = m_hv->selectionStart() != m_hv->selectionEnd();

    // Reset the cursor panel's width floor when flipping between cursor and
    // selection mode; each mode tracks its own independent minimum width.
    if (hasSel != m_hasSel) {
        m_hasSel = hasSel;
        m_comboCursor->resetMinWidth();
        m_comboLength->resetMinWidth();
    }

    if (hasSel) {
        m_comboCursor->setDisplayText(m_comboCursor->selection() == 0
            ? QString("Selection: 0x%1 - 0x%2")
                  .arg(fmtHex(m_hv->selectionStart(), 8))
                  .arg(fmtHex(m_hv->selectionEnd(), 8))
            : QString("Selection: %1 - %2")
                  .arg(m_hv->selectionStart())
                  .arg(m_hv->selectionEnd()));
    } else {
        m_comboCursor->setDisplayText(m_comboCursor->selection() == 0
            ? QString("Cursor: 0x%1").arg(fmtHex(m_hv->cursorOffset(), 8))
            : QString("Cursor: %1").arg(m_hv->cursorOffset()));
    }

    const size_w len = hasSel ? m_hv->selectionSize() : m_hv->size();
    m_comboLength->setDisplayText(m_comboLength->selection() == 0
        ? QString("0x%1 bytes").arg(fmtHex(len, 8))
        : QString("%1 bytes").arg(QLocale(QLocale::English).toString((qulonglong)len)));

    m_comboValue->setDisplayText(computeValueText());

    const int modeIdx = modeToIndex(m_hv->editMode());
    m_comboMode->setSelection(modeIdx);
    m_comboMode->setDisplayText(indexToLabel[modeIdx]);
}
