#include "statusbar.h"
#include "HexView/hexview.h"
#include "theme.h"
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLocale>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>

bool ValueComboBox::eventFilter(QObject *obj, QEvent *e)
{
    // On Wayland the compositor may reposition popup windows; move() here
    // overrides the compositor's placement with the position we computed.
    if (e->type() == QEvent::Show && m_pendingMenuPos.x() >= 0) {
        // PM_MenuPanelWidth is the shadow inset on Adwaita (9px): the menu
        // window is that much wider than the visible frame on each side.
        // Adding it here shifts the window right so the visible right edge
        // aligns with the combo's right edge.  Returns 0 on Windows/Fusion.
        QWidget *w = static_cast<QWidget *>(obj);
        const QRect ag  = QRect(mapToGlobal(QPoint(0, 0)), size());
        const int panel = w->style()->pixelMetric(
            QStyle::PM_MenuPanelWidth, nullptr, w);
        w->move(ag.right() - w->width() + 1 + panel, m_pendingMenuPos.y());
        m_pendingMenuPos = {-1, -1};
    }
    return QComboBox::eventFilter(obj, e);
}

QSize ValueComboBox::sizeHint() const
{
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = m_displayText;
    const QSize textSz = fontMetrics().size(Qt::TextSingleLine, m_displayText);
    return style()->sizeFromContents(QStyle::CT_ComboBox, &opt, textSz, this);
}

// ── RadioComboBox ─────────────────────────────────────────────────────────────

RadioComboBox::RadioComboBox(const QStringList &items, QWidget *parent)
    : ValueComboBox(parent)
{
    m_menu = new QMenu(this);
    themeMenu(m_menu);
    QActionGroup *group = new QActionGroup(this);
    group->setExclusive(true);

    for (int i = 0; i < items.size(); ++i) {
        QAction *a = m_menu->addAction(items[i]);
        a->setCheckable(true);
        a->setChecked(i == 0);
        group->addAction(a);
        m_actions.append(a);

        connect(a, &QAction::toggled, this, [this, i](bool checked) {
            if (!m_updating && checked) { m_selection = i; emit selectionChanged(i); }
        });
    }
}

void RadioComboBox::setSelection(int index)
{
    if (index < 0 || index >= m_actions.size() || index == m_selection) return;
    m_updating = true;
    m_actions[index]->setChecked(true);   // QActionGroup unchecks the rest
    m_updating = false;
    m_selection = index;
}

void RadioComboBox::showPopup()
{
    popupRight(m_menu);
}

// ── ValueOptionsComboBox ──────────────────────────────────────────────────────

ValueOptionsComboBox::ValueOptionsComboBox(QWidget *parent)
    : ValueComboBox(parent)
{
    m_menu = new QMenu(this);
    themeMenu(m_menu);

    m_actSigned = m_menu->addAction("Signed");
    m_actSigned->setCheckable(true);
    m_actSigned->setChecked(false);

    m_actBigEndian = m_menu->addAction("Big Endian");
    m_actBigEndian->setCheckable(true);
    m_actBigEndian->setChecked(false);

    m_actHex = m_menu->addAction("Hexadecimal");
    m_actHex->setCheckable(true);
    m_actHex->setChecked(false);

    m_menu->addSeparator();

    QActionGroup *sizeGroup = new QActionGroup(this);
    sizeGroup->setExclusive(true);
    const char *sizeLabels[6] = {
        "8-bit Byte", "16-bit Word", "32-bit Dword",
        "64-bit Qword", "Float (32-bit IEEE)", "Double (64-bit IEEE)"
    };
    for (int i = 0; i < 6; ++i) {
        m_sizeActions[i] = m_menu->addAction(sizeLabels[i]);
        m_sizeActions[i]->setCheckable(true);
        sizeGroup->addAction(m_sizeActions[i]);
    }
    m_sizeActions[0]->setChecked(true);

    connect(m_actSigned,    &QAction::toggled, this, [this](bool v) { m_signed    = v; emit optionsChanged(); });
    connect(m_actBigEndian, &QAction::toggled, this, [this](bool v) { m_bigEndian = v; emit optionsChanged(); });
    connect(m_actHex,       &QAction::toggled, this, [this](bool v) { m_hex       = v; emit optionsChanged(); });
    for (int i = 0; i < 6; ++i) {
        connect(m_sizeActions[i], &QAction::toggled, this, [this, i](bool checked) {
            if (checked) { m_dataSize = i; emit optionsChanged(); }
        });
    }
}

void ValueOptionsComboBox::showPopup()
{
    popupRight(m_menu);
}

// ── PanelStrip ────────────────────────────────────────────────────────────────

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
    if (w) w->installEventFilter(this);
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
    // Both the strip and the guard are children of the same parent (the status bar),
    // so subtracting their positions gives the guard's rect in strip-local coordinates.
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
        h  = qMax(h, p->sizeHint().height());
    }
    return {w, h};
}

QSize PanelStrip::minimumSizeHint() const
{
    // Advertise zero width so the window can shrink below the panels' total width.
    return {0, sizeHint().height()};
}

void PanelStrip::layoutPanels()
{
    // Place panels right-to-left at their preferred widths.
    // Panels that overflow the left edge are clipped at the widget boundary.
    static constexpr int kRightMargin = 12;
    int x = width() - kRightMargin;
    for (int i = m_panels.count() - 1; i >= 0; --i) {
        QWidget *p  = m_panels[i];
        int      pw = p->sizeHint().width();
        int      ph = p->sizeHint().height();
        int      py = (height() - ph) / 2;
        x -= pw;
        p->setGeometry(x, py, pw, ph);
    }
    checkOverlap();
}

void PanelStrip::resizeEvent(QResizeEvent *) { layoutPanels(); }

bool PanelStrip::event(QEvent *e)
{
    // A child called updateGeometry() because its sizeHint changed — re-layout.
    if (e->type() == QEvent::LayoutRequest)
        layoutPanels();
    return QWidget::event(e);
}

// ── StatusBar ─────────────────────────────────────────────────────────────────

static constexpr uint   indexToMode[]      = { HVMODE_OVERWRITE, HVMODE_INSERT, HVMODE_READONLY };
static constexpr const char *indexToLabel[] = { "OVR", "INS", "READ" };

static int modeToIndex(uint mode)
{
    for (int i = 0; i < 3; ++i)
        if (indexToMode[i] == mode) return i;
    return 0;
}

static QString fmtHex(quint64 v, int width)
{
    return QString::number(v, 16).toUpper().rightJustified(width, '0');
}

StatusBar::StatusBar(HexView *hv, QStatusBar *bar, QObject *parent)
    : QObject(parent), m_hv(hv)
{
    // Panel 1: cursor / selection position
    m_comboCursor = new RadioComboBox({"Hexadecimal", "Decimal"}, bar);
    connect(m_comboCursor, &RadioComboBox::selectionChanged, this, [this] { update(); });

    // Panel 2: length
    m_comboLength = new RadioComboBox({"Hexadecimal", "Decimal"}, bar);
    m_comboLength->setSelection(1);
    connect(m_comboLength, &RadioComboBox::selectionChanged, this, [this] { update(); });

    // Panel 3: value under cursor
    m_comboValue = new ValueOptionsComboBox(bar);
    connect(m_comboValue, &ValueOptionsComboBox::optionsChanged, this, [this] { update(); });

    // Panel 4: edit mode
    m_comboMode = new RadioComboBox({"Overwrite", "Insert", "Readonly"}, bar);
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

    // Search progress widget — shown on the left side of the status bar during active searches
    auto *searchContainer = new QWidget(bar);
    auto *searchLayout    = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(8, 0, 8, 0);
    searchLayout->setSpacing(6);
    m_searchLabel = new QLabel("Searching:", searchContainer);
    m_searchBar   = new QProgressBar(searchContainer);
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

    // Search hex preview — shows live byte representation of the current pattern
    m_patternLabel = new QLabel(bar);
    m_patternLabel->setContentsMargins(8, 0, 8, 0);
    bar->addWidget(m_patternLabel);
    m_patternLabel->hide();
    m_patternSep = makeSep();
    bar->addWidget(m_patternSep);

    bar->addPermanentWidget(strip, 1); // stretch=1: strip fills remaining width
    bar->setMinimumWidth(0);           // prevent status bar from enforcing a floor
    strip->setOverlapGuard(m_patternLabel);

    update();
}

QString StatusBar::computeValueText() const
{
    static const int sizes[] = { 1, 2, 4, 8, 4, 8 };
    int ds = m_comboValue->dataSize();
    int n  = sizes[ds];

    uint8_t buf[8] = {};
    if ((int)m_hv->getData(m_hv->cursorOffset(), buf, n) < n)
        return "Value: ---";

    if (m_comboValue->isBigEndian() && n > 1)
        std::reverse(buf, buf + n);

    bool doHex    = m_comboValue->isHex();
    bool doSigned = m_comboValue->isSigned();

    if (ds == 4) {
        if (doHex) { uint32_t r; memcpy(&r, buf, 4); return QString("Value: 0x%1").arg(fmtHex(r, 8)); }
        float f; memcpy(&f, buf, 4);
        return QString("Value: %1").arg(double(f), 0, 'g', 7);
    }
    if (ds == 5) {
        if (doHex) { uint64_t r; memcpy(&r, buf, 8); return QString("Value: 0x%1").arg(fmtHex(r, 16)); }
        double d; memcpy(&d, buf, 8);
        return QString("Value: %1").arg(d, 0, 'g', 15);
    }

    uint64_t raw = 0;
    memcpy(&raw, buf, n);
    if (doHex)
        return QString("Value: 0x%1").arg(fmtHex(raw, n * 2));

    if (doSigned) {
        int64_t v;
        switch (ds) {
        case 0: v = static_cast<int8_t>(buf[0]); break;
        case 1: { int16_t t; memcpy(&t, buf, 2); v = t; } break;
        case 2: { int32_t t; memcpy(&t, buf, 4); v = t; } break;
        default: memcpy(&v, buf, 8); break;
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
    int pct = (len > 0) ? (int)((double)pos / (double)len * 100.0) : 0;
    m_searchBar->setValue(qBound(0, pct, 100));
    m_searchLabel->setText(QString("Searching %1 MB/s:").arg(mbPerSec, 0, 'f', 1));
}

void StatusBar::onFindDone()
{
    m_searchWidget->hide();
    m_searchSep->hide();
}

void StatusBar::showSearchHex(const QString &hex)
{
    if (hex.isEmpty()) {
        m_patternLabel->hide();
        m_patternSep->hide();
        return;
    }
    static const QRegularExpression rxHex("^[0-9A-Fa-f ]+$");
    if(rxHex.match(hex).hasMatch()) {
        m_patternLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        //m_patternLabel->setForegroundRole(QPalette::ColorRole::WindowText);//
        //m_patternLabel->setStyleSheet("color:black");
    }
    else {
        m_patternLabel->setFont(QApplication::font());
        //m_patternLabel->setStyleSheet("color:darkred; border-right:1px solid #ddd");
    }
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
    bool hasSel = m_hv->selectionStart() != m_hv->selectionEnd();

    // Panel 1: cursor offset or selection range
    if (hasSel) {
        m_comboCursor->setDisplayText(m_comboCursor->selection() == 0
            ? QString("Selection: 0x%1 \u2013 0x%2")
                  .arg(fmtHex(m_hv->selectionStart(), 8))
                  .arg(fmtHex(m_hv->selectionEnd(), 8))
            : QString("Selection: %1 \u2013 %2")
                  .arg(m_hv->selectionStart())
                  .arg(m_hv->selectionEnd()));
    } else {
        m_comboCursor->setDisplayText(m_comboCursor->selection() == 0
            ? QString("Cursor: 0x%1").arg(fmtHex(m_hv->cursorOffset(), 8))
            : QString("Cursor: %1").arg(m_hv->cursorOffset()));
    }

    // Panel 2: file or selection length
    size_w len = hasSel ? m_hv->selectionSize() : m_hv->size();
    m_comboLength->setDisplayText(m_comboLength->selection() == 0
        ? QString("0x%1 bytes").arg(fmtHex(len, 8))
        : QString("%1 bytes").arg(QLocale(QLocale::English).toString((qulonglong)len)));

    // Panel 3: value under cursor
    m_comboValue->setDisplayText(computeValueText());

    // Panel 4: edit mode — sync check state in case mode changed outside the combo
    int modeIdx = modeToIndex(m_hv->editMode());
    m_comboMode->setSelection(modeIdx);
    m_comboMode->setDisplayText(indexToLabel[modeIdx]);
}
