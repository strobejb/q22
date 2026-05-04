#include "preferences.h"
#include "settingscard.h"
#include "settings.h"
#include "slideoverlay.h"
#include "theme.h"

#include <algorithm>
#include <memory>

#include <QApplication>
#include <QPropertyAnimation>
#include <QTimer>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QScrollBar>
#include <QDir>
#include <QFileSystemWatcher>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

static constexpr int kSwatchCols = 3;
static constexpr int kMainPaletteLimit = 5;
static constexpr int kHeaderTopGap = 4;
static constexpr int kHeaderBottomGap = 8;
static constexpr int kGroupTopGap = 20;
static constexpr int kContentGap = 14;

static void recordRecentPalette(const PaletteInfo &info)
{
    AppSettings::addRecentPalette(info.name);
}

// ─── FontPickerDialog ────────────────────────────────────────────────────────

FontPickerDialog::FontPickerDialog(const QFont &current, QWidget *parent)
    : QDialog(parent), m_font(current)
{
    removeDialogIcon(this);
    setWindowTitle(tr("Select Font"));
    resize(460, 540);

    // ── List ─────────────────────────────────────────────────────────────────
    m_list = new QListWidget(this);
    {
        const bool dark = qApp->palette().window().color().lightness() < 128;
        const QString border = dark ? QLatin1String("rgba(255,255,255,0.18)")
                                    : QLatin1String("rgba(0,0,0,0.15)");
        m_list->setStyleSheet(QString(
            "QListWidget { border: 1px solid %1; outline: 0; }"
        ).arg(border));
    }
    applyListItemPadding(m_list);

    int selectRow = -1;
    const QStringList families = QFontDatabase::families();
    for (const QString &family : families) {
        if (!QFontDatabase::isFixedPitch(family))
            continue;
        auto *item = new QListWidgetItem(family);
        item->setData(Qt::UserRole, family);
        m_list->addItem(item);
        if (family == current.family())
            selectRow = m_list->count() - 1;
    }
    if (selectRow >= 0)
        m_list->setCurrentRow(selectRow);
    else if (m_list->count() > 0)
        m_list->setCurrentRow(0);

    // ── Preview ───────────────────────────────────────────────────────────────
    m_preview = new QLabel;
    m_preview->setText("AaBbCcDd  0123456789\n! @ # $ %  [ ] { } ( )\nThe quick brown fox");
    m_preview->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_preview->setContentsMargins(10, 8, 10, 8);

    auto *previewFrame = new QFrame(this);
    previewFrame->setFrameShape(QFrame::StyledPanel);
    previewFrame->setFrameShadow(QFrame::Sunken);
    previewFrame->setMinimumHeight(90);
    auto *previewLay = new QVBoxLayout(previewFrame);
    previewLay->setContentsMargins(0, 0, 0, 0);
    previewLay->addWidget(m_preview);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QAbstractButton *btn : buttons->buttons())
        btn->setIcon(QIcon());

    // ── Header row (overlayHeader) — receives the back button when hosted ─────
    auto *fontHeader = new QWidget(this);
    fontHeader->setObjectName(QStringLiteral("overlayHeader"));
    {
        auto *hlay = new QHBoxLayout(fontHeader);
        hlay->setContentsMargins(0, 0, 0, 0);
        hlay->setSpacing(8);
        auto *title = new QLabel(tr("Font"), fontHeader);
        QFont f = title->font();
        f.setBold(true);
        title->setFont(f);
        hlay->addWidget(title);
        hlay->addStretch();
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(20, 20, 20, 20);
    vlay->setSpacing(0);
    vlay->addSpacing(kHeaderTopGap);
    vlay->addWidget(fontHeader);
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(m_list, 1);
    vlay->addSpacing(kContentGap);
    vlay->addWidget(previewFrame);
    vlay->addSpacing(kContentGap);
    vlay->addWidget(buttons);

    connect(m_list, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *item) {
        if (!item) return;
        const QString family = item->data(Qt::UserRole).toString();
        m_font = QFontDatabase::font(family, "Regular", m_font.pointSize() > 0
                                                        ? m_font.pointSize() : 13);
        updatePreview();
    });

    updatePreview();
}

void FontPickerDialog::updatePreview()
{
    m_preview->setFont(m_font);
}

// ─── AddPaletteSwatch ─────────────────────────────────────────────────────────


class ViewMoreButton : public QAbstractButton
{
public:
    explicit ViewMoreButton(QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setText(tr("View More"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_icon = recoloredIcon("go-next-symbolic",
                               QApplication::palette().color(QPalette::WindowText),
                               kIconSz);
    }

    QSize sizeHint() const override
    {
        QFont f = font();
        f.setBold(true);
        const QFontMetrics fm(f);
        return QSize(fm.horizontalAdvance(text()) + kGap + kIconSz + 2 * kPadX,
                     qMax(fm.height(), kIconSz) + 2 * kPadY);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette &pal = palette();

        if (isDown() || underMouse() || hasFocus()) {
            const QColor hover = isDown() ? pal.color(QPalette::Mid)
                                          : pal.color(QPalette::Button);
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        }

        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(pal.color(QPalette::WindowText));

        const QFontMetrics fm(f);
        const int textW = fm.horizontalAdvance(text());
        const int contentW = textW + kGap + kIconSz;
        int x = (width() - contentW) / 2;
        const int ty = (height() + fm.ascent() - fm.descent()) / 2;
        p.drawText(QPoint(x, ty), text());
        x += textW + kGap;
        m_icon.paint(&p, QRect(x, (height() - kIconSz) / 2, kIconSz, kIconSz));
    }

    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e) override { update(); QAbstractButton::leaveEvent(e); }
    void focusInEvent(QFocusEvent *e) override { update(); QAbstractButton::focusInEvent(e); }
    void focusOutEvent(QFocusEvent *e) override { update(); QAbstractButton::focusOutEvent(e); }

private:
    static constexpr int kIconSz = 12;
    static constexpr int kGap = 5;
    static constexpr int kPadX = 8+2;
    static constexpr int kPadY = 8;//3+2;
    QIcon m_icon;
};

// ─── PreferencesDialog ───────────────────────────────────────────────────────

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    removeDialogIcon(this);
    setWindowTitle(tr("Preferences"));

    // ── Font family (nav row opens picker overlay) ────────────────────────────
    m_fontFamily = AppSettings::prefFontFamily();
    m_fontNav = new NavigationRow(tr("Font"), NavigationRow::Icon::Next, this);
    if (!m_fontFamily.isEmpty())
        m_fontNav->setValueText(m_fontFamily);

    // ── Font size ────────────────────────────────────────────────────────────
    m_fontSize = new StepSpinBox(tr("Font Size"), 6, 72, 1, this);
    m_fontSize->setValue(AppSettings::prefFontSize());

    // ── Spacing ──────────────────────────────────────────────────────────────
    m_horizSpacing = new StepSpinBox(tr("Character Spacing"), 0, 20, 1, this);
    m_horizSpacing->setValue(AppSettings::prefHorizSpacing());

    m_lineSpacing = new StepSpinBox(tr("Line Spacing"), 0, 20, 1, this);
    m_lineSpacing->setValue(AppSettings::prefLineSpacing());

    // ── Appearance toggles ───────────────────────────────────────────────────
    m_nativeMenu = new SettingsToggle(tr("Native menu bar"), this);
    m_nativeMenu->setChecked(AppSettings::prefNativeMenu());

    m_menuHighlight = new SettingsToggle(tr("Menus use highlight colour"), this);
    m_menuHighlight->setChecked(AppSettings::prefMenuHighlight());

    // ── Signal connections ────────────────────────────────────────────────────
    connect(m_fontSize, &StepSpinBox::valueChanged,
            this, [this](int size) {
        AppSettings::setPrefFontSize(size);
        emit fontChanged(QFont(m_fontFamily, size));
    });
    connect(m_horizSpacing, &StepSpinBox::valueChanged,
            this, [this](int h) {
        AppSettings::setPrefHorizSpacing(h);
        emit fontSpacingChanged(h, m_lineSpacing->value());
    });
    connect(m_lineSpacing, &StepSpinBox::valueChanged,
            this, [this](int v) {
        AppSettings::setPrefLineSpacing(v);
        emit fontSpacingChanged(m_horizSpacing->value(), v);
    });
    connect(m_nativeMenu, &SettingsToggle::toggled,
            this, [this](bool on) {
        AppSettings::setPrefNativeMenu(on);
        emit nativeMenuChanged(on);
    });
    connect(m_menuHighlight, &SettingsToggle::toggled,
            this, [this](bool on) {
        AppSettings::setPrefMenuHighlight(on);
        emit menuHighlightChanged(on);
    });

    // ── Cards ─────────────────────────────────────────────────────────────────
    auto *fontGroup   = new SettingsCard(
        {m_fontNav, m_fontSize, m_horizSpacing, m_lineSpacing},
        SettingsCard::Style::Spaced, this);
    auto *appearGroup = new SettingsCard(
        {m_nativeMenu, m_menuHighlight},
        SettingsCard::Style::Spaced, this);

    // ── Reset button ──────────────────────────────────────────────────────────
    auto *resetBtn  = new DangerButton(tr("Reset to defaults"), this);
    auto *resetCard = new SettingsCard({resetBtn}, SettingsCard::Style::Spaced, this);

    // ── Theme swatches ────────────────────────────────────────────────────────
    m_swatchWidget = new QWidget(this);
    {
        m_swatchLayout = new QGridLayout(m_swatchWidget);
        m_swatchLayout->setContentsMargins(0, 0, 0, 0);
        m_swatchLayout->setSpacing(5 + 2 * SW_SHADOW);
        m_swatchLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

        m_swatchGroup = new QButtonGroup(m_swatchWidget);
        m_swatchGroup->setExclusive(true);

        m_palettes = loadAllPalettes();
        if (!m_palettes.isEmpty())
            m_currentPalette = m_palettes.first();

        const QString savedName = AppSettings::prefPaletteName();
        if (!savedName.isEmpty()) {
            for (const PaletteInfo &info : m_palettes) {
                if (info.name == savedName) {
                    m_currentPalette = info;
                    break;
                }
            }
        }

        m_addBtn = new PaletteSwatch(m_swatchWidget);
        connect(m_addBtn, &QAbstractButton::clicked,
                this, &PreferencesDialog::openAddPaletteEditor);
        populateMainSwatches();

        m_watcher = new QFileSystemWatcher(this);
        const QString paletteDir = paletteStorageDir();
        if (QDir(paletteDir).exists())
            m_watcher->addPath(paletteDir);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                this, [this](const QString &) { rebuildCustomSwatches(); });
    }
    m_swatchWidget->setFocusPolicy(Qt::StrongFocus);
    m_swatchWidget->installEventFilter(this);

    // ── Section label helper ──────────────────────────────────────────────────
    auto makeSectionLabel = [this](const QString &text) {
        auto *lbl = new QLabel(text, this);
        QFont f = lbl->font();
        f.setBold(true);
        lbl->setFont(f);
        return lbl;
    };

    // ── Scrollable content ────────────────────────────────────────────────────
    auto *content = new QWidget;
    auto *vlay = new QVBoxLayout(content);
    vlay->setContentsMargins(20, 20, 20, 20);
    vlay->setSpacing(0);
    vlay->addSpacing(kHeaderTopGap);
    auto *themeHeader = new QWidget(content);
    auto *themeHeaderLay = new QHBoxLayout(themeHeader);
    themeHeaderLay->setContentsMargins(0, 0, 0, 0);
    themeHeaderLay->setSpacing(8);
    themeHeaderLay->addWidget(makeSectionLabel(tr("Theme")));
    themeHeaderLay->addStretch();
    m_viewMore = new ViewMoreButton(themeHeader);
    connect(m_viewMore, &QAbstractButton::clicked,
            this, &PreferencesDialog::showPaletteListOverlay);
    themeHeaderLay->addWidget(m_viewMore);
    vlay->addWidget(themeHeader);
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(m_swatchWidget);
    vlay->addSpacing(kGroupTopGap);
    vlay->addWidget(makeSectionLabel(tr("Font")));
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(fontGroup);
    vlay->addSpacing(kGroupTopGap);
    vlay->addWidget(makeSectionLabel(tr("Appearance")));
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(appearGroup);
    vlay->addSpacing(kGroupTopGap);
    vlay->addWidget(resetCard);
    vlay->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    scroll->setWidget(content);

    auto *dialogLay = new QVBoxLayout(this);
    dialogLay->setContentsMargins(0, 0, 0, 0);
    dialogLay->addWidget(scroll);

    // Overlay for child dialogs — must be created after the dialog layout so it
    // stacks on top of the scroll area when raised.
    m_overlay = new SlideOverlay(this);

    // Font picker opens as an overlay slide-in
    connect(m_fontNav, &QAbstractButton::clicked, this, [this]() {
        if (m_overlay->isActive()) return;
        auto *dlg = new FontPickerDialog(QFont(m_fontFamily), this);
        m_overlay->slideIn(dlg, [this, dlg](int result) {
            if (result == QDialog::Accepted) {
                m_fontFamily = dlg->selectedFont().family();
                m_fontNav->setValueText(m_fontFamily);
                AppSettings::setPrefFontFamily(m_fontFamily);
                emit fontChanged(QFont(m_fontFamily, m_fontSize->value()));
            }
        });
    });

    setSizeGripEnabled(false);

    // Hide this modeless dialog whenever any modal window opens, restore when it closes.
    qApp->installEventFilter(this);
}

void PreferencesDialog::prepareShow()
{
    // WA_Moved is set by move() below; if it's already set this was called
    // twice (e.g. from mainwindow and then again from setVisible) — skip.
    if (testAttribute(Qt::WA_Moved)) return;

    layout()->activate();
    const int maxH = 560;
    const QSize hint = sizeHint();

    // Derive width so the swatch grid sits with zero padding on each side at
    // this size.  vlay uses 20 px margins left and right; the grid is 3 columns
    // at the swatch fixed width with (5 + 2*SW_SHADOW) px gaps between them.
    // When content exceeds maxH a vertical scrollbar appears and steals some
    // horizontal space from the viewport — add its extent to compensate.
    const int swatchGap = 5 + 2 * SW_SHADOW;
    const int gridW     = kSwatchCols * m_addBtn->width()
                        + (kSwatchCols - 1) * swatchGap;
    const int sbW = (hint.height() > maxH)
                    ? style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 0;
    const int w = gridW + 2 * 20 + sbW;
    const int h = qMin(hint.height(), maxH);
    // Mirror execCentered(): resize → move → winId(), then lock.
    // setFixedSize() before move() changes size constraints that affect HWND
    // creation geometry on Windows, producing a wrong initial position.
    resize(w, h);
    if (QWidget *par = parentWidget()) {
        const QPoint c = par->frameGeometry().center();
        move(c.x() - w / 2, c.y() - h / 2);
    }
#ifdef Q_OS_WIN
    (void)winId(); // force HWND creation while hidden — same pattern as execCentered()
#endif
    setFixedSize(w, h); // lock size after HWND is in place
}

void PreferencesDialog::setVisible(bool visible)
{
    const bool opening = visible && !isVisible() && !m_hiddenByModal;
    if (!visible) {
        // Only dismiss overlay on an explicit close, not a temporary modal hide.
        if (!m_hiddenByModal && m_overlay->isActive())
            m_overlay->dismiss();
    } else if (!isVisible() && !m_hiddenByModal) {
        // Fallback: centre on parent if prepareShow() wasn't called first.
        prepareShow();
    }
    if (opening)
        populateMainSwatches();
    if (visible) m_suppressRingOnFocus = true;
    QDialog::setVisible(visible);
    if (visible)
        m_swatchWidget->setFocus();
}

bool PreferencesDialog::eventFilter(QObject *obj, QEvent *e)
{
    // ── Modal watch (installed on qApp — runs for all QObjects) ──────────────
    if (e->type() == QEvent::Show) {
        auto *w = qobject_cast<QWidget *>(obj);
        // Don't hide for modals that are transient to our own embedded content
        // (e.g. the overwrite-confirmation QMessageBox from PaletteEditorDialog).
        // Their native parent is PreferencesDialog (traversed up through the
        // SlideOverlay child chain), so hiding here severs the Wayland transient
        // relationship and makes the dialog non-interactive — causing exec() to hang.
        auto *wpar = w ? w->parentWidget() : nullptr;
        const bool isOwnTransient = wpar && (wpar == this || isAncestorOf(wpar));
        if (w && w->isWindow() && w != this
                && w->windowModality() != Qt::NonModal
                && isVisible() && !m_hiddenByModal && !isOwnTransient) {
            m_hiddenByModal = true;
            m_savedPos = pos();
            // Defer so Qt finishes enterModal() before we hide — if we call
            // hide() synchronously here, activeModalWidget() is still null when
            // the resulting QEvent::Hide fires, causing an immediate restore.
            QTimer::singleShot(0, this, &QWidget::hide);
        }
    } else if (e->type() == QEvent::Hide && m_hiddenByModal) {
        // Defer so Qt finishes exitModal() before we check activeModalWidget().
        QTimer::singleShot(0, this, [this] {
            if (m_hiddenByModal && !QApplication::activeModalWidget()) {
                move(m_savedPos);
                setWindowOpacity(0.0);
                show();             // our setVisible skips size/centre recalc while m_hiddenByModal
                m_hiddenByModal = false;
                auto *anim = new QPropertyAnimation(this, "windowOpacity", this);
                anim->setDuration(200);
                anim->setStartValue(0.0);
                anim->setEndValue(1.0);
                anim->start(QAbstractAnimation::DeleteWhenStopped);
            }
        });
    }

    // ── Swatch container ──────────────────────────────────────────────────────
    if (obj != m_swatchWidget)
        return false;

    const auto palBtns = m_swatchGroup->buttons();
    // Navigation covers all cards: palette swatches followed by the add card.
    QList<QAbstractButton *> allBtns = palBtns;
    allBtns.append(m_addBtn);
    const int palCount = allBtns.size();

    // Helper: set/clear the keyboard cursor ring on a card by index.
    auto setCursor = [&](int idx, bool on) {
        if (idx < 0 || idx >= allBtns.size()) return;
        if (auto *sw = qobject_cast<PaletteSwatch *>(allBtns[idx]))
            sw->setKeyboardCursor(on);
    };

    if (e->type() == QEvent::FocusIn) {
        // Consume the suppress flag first — set by setVisible() to block the
        // spurious FocusIn(Tab) Qt fires when activating the dialog window.
        const bool suppress = m_suppressRingOnFocus;
        m_suppressRingOnFocus = false;
        // Clear any stale ring at the old cursor position before moving it.
        setCursor(m_swatchCursor, false);
        auto *checked  = m_swatchGroup->checkedButton();
        m_swatchCursor = checked ? palBtns.indexOf(checked) : 0;
        if (m_swatchCursor < 0) m_swatchCursor = 0;
        const auto reason = static_cast<QFocusEvent *>(e)->reason();
        if (!suppress && (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason))
            setCursor(m_swatchCursor, true);
        return false;
    }

    if (e->type() == QEvent::FocusOut) {
        setCursor(m_swatchCursor, false);
        return false;
    }

    if (e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(e)->key();

        // Space / Return / Enter activate the focused card (select palette or open add dialog).
        if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
            allBtns[m_swatchCursor]->click();
            return true;
        }

        int next = m_swatchCursor;

        switch (key) {
        case Qt::Key_Right: next = qMin(m_swatchCursor + 1,           palCount - 1); break;
        case Qt::Key_Left:  next = qMax(m_swatchCursor - 1,           0);            break;
        case Qt::Key_Up:    next = qMax(m_swatchCursor - kSwatchCols, 0);            break;
        case Qt::Key_Down:
            next = m_swatchCursor + kSwatchCols;
            if (next >= palCount) {
                setCursor(m_swatchCursor, false);
                focusNextPrevChild(true); // hand off to next Tab stop
                return true;
            }
            break;
        default: return false;
        }

        if (next != m_swatchCursor) {
            setCursor(m_swatchCursor, false);
            m_swatchCursor = next;
            setCursor(m_swatchCursor, true);
        }
        return true; // consume — don't let the scroll area scroll
    }

    return false;
}


void PreferencesDialog::syncCursorToSwatch(int idx)
{
    QList<QAbstractButton *> allBtns = m_swatchGroup->buttons();
    allBtns.append(m_addBtn);
    if (auto *old = qobject_cast<PaletteSwatch *>(allBtns.value(m_swatchCursor)))
        old->setKeyboardCursor(false);
    m_swatchCursor = idx;
    // Do not show the cursor ring here: this is called from mouse-click handlers
    // and the ring is only activated by keyboard navigation.
}

PaletteSwatch *PreferencesDialog::createPaletteSwatch(const PaletteInfo &info, QWidget *parent)
{
    auto *sw = new PaletteSwatch(info, parent);
    if (info.name == m_currentPalette.name)
        sw->setChecked(true);
    connect(sw, &QAbstractButton::clicked, this, [this, sw, info]() {
        syncCursorToSwatch(m_swatchGroup->buttons().indexOf(sw));
        m_currentPalette = info;
        recordRecentPalette(info);
        emit paletteSelected(info);
    });
    auto sharedInfo = std::make_shared<PaletteInfo>(info);
    connect(sw, &PaletteSwatch::doubleClicked, this, [this, sharedInfo]() {
        openEditPaletteEditor(sharedInfo);
    });
    return sw;
}

void PreferencesDialog::populateMainSwatches()
{
    const auto old = m_swatchGroup->buttons();
    for (auto *b : old)
        delete b;

    while (auto *item = m_swatchLayout->takeAt(0))
        delete item;

    QList<PaletteInfo> ordered;
    if (AppSettings::prefRecentPaletteOrdering()) {
        const QStringList recentNames = AppSettings::prefRecentPalettes();
        for (const QString &recent : recentNames) {
            const auto it = std::find_if(m_palettes.cbegin(), m_palettes.cend(),
                                         [&](const PaletteInfo &p) { return p.name == recent; });
            if (it != m_palettes.cend())
                ordered.append(*it);
        }
        for (const PaletteInfo &info : m_palettes) {
            const auto it = std::find_if(ordered.cbegin(), ordered.cend(),
                                         [&](const PaletteInfo &p) { return p.name == info.name; });
            if (it == ordered.cend())
                ordered.append(info);
        }
    } else {
        ordered = m_palettes;
    }

    m_swatchCount = 0;
    const int visiblePaletteCount = qMin(kMainPaletteLimit, ordered.size());
    for (int i = 0; i < visiblePaletteCount; ++i) {
        auto *sw = createPaletteSwatch(ordered.at(i), m_swatchWidget);
        m_swatchGroup->addButton(sw);
        m_swatchLayout->addWidget(sw, m_swatchCount / kSwatchCols,
                                      m_swatchCount % kSwatchCols);
        ++m_swatchCount;
    }

    m_swatchLayout->addWidget(m_addBtn, m_swatchCount / kSwatchCols,
                                        m_swatchCount % kSwatchCols);
    m_addBtn->show();
    m_swatchCursor = qBound(0, m_swatchCursor, m_swatchCount);
}

void PreferencesDialog::openAddPaletteEditor()
{
    if (m_overlay->isActive()) return;
    const PaletteInfo before = m_currentPalette;
    const auto savedScheme = static_cast<ColorScheme>(AppSettings::prefColorScheme());
    PaletteInfo newInfo;
    auto *dlg = new PaletteEditorDialog(newInfo, this);
    connect(dlg, &PaletteEditorDialog::paletteChanged,
            this, &PreferencesDialog::paletteSelected);
    connect(dlg, &PaletteEditorDialog::paletteSaved,
            this, &PreferencesDialog::addCustomSwatch);
    connect(dlg, &PaletteEditorDialog::previewModeRequested,
            this, [savedScheme](int mode) {
        const ColorScheme cs = mode == 1 ? ColorScheme::Light
                            : mode == 2 ? ColorScheme::Dark : savedScheme;
        QTimer::singleShot(0, [cs]() { applyAdwaitaTheme(cs); });
    });
    m_overlay->slideIn(dlg, [this, dlg, before, savedScheme](int result) {
        const bool accepted = (result == QDialog::Accepted);
        const PaletteInfo toApply = (result == QDialog::Accepted) ? dlg->currentInfo() : before;
        QTimer::singleShot(0, this, [this, savedScheme, toApply, accepted]() {
            applyAdwaitaTheme(savedScheme);
            if (accepted && !toApply.name.isEmpty())
                recordRecentPalette(toApply);
            emit paletteSelected(toApply);
        });
    });
}

void PreferencesDialog::openEditPaletteEditor(const std::shared_ptr<PaletteInfo> &sharedInfo)
{
    if (m_overlay->isActive()) return;
    const PaletteInfo before = m_currentPalette;
    const auto savedScheme = static_cast<ColorScheme>(AppSettings::prefColorScheme());
    auto *dlg = new PaletteEditorDialog(*sharedInfo, this);
    connect(dlg, &PaletteEditorDialog::paletteChanged,
            this, &PreferencesDialog::paletteSelected);
    connect(dlg, &PaletteEditorDialog::paletteSaved,
            this, &PreferencesDialog::addCustomSwatch);
    connect(dlg, &PaletteEditorDialog::previewModeRequested,
            this, [savedScheme](int mode) {
        const ColorScheme cs = mode == 1 ? ColorScheme::Light
                            : mode == 2 ? ColorScheme::Dark : savedScheme;
        QTimer::singleShot(0, [cs]() { applyAdwaitaTheme(cs); });
    });
    m_overlay->slideIn(dlg, [this, dlg, sharedInfo, before, savedScheme](int result) {
        const bool accepted = (result == QDialog::Accepted);
        if (accepted) *sharedInfo = dlg->currentInfo();
        const PaletteInfo toApply = accepted ? dlg->currentInfo() : before;
        QTimer::singleShot(0, this, [this, savedScheme, toApply, accepted]() {
            applyAdwaitaTheme(savedScheme);
            if (accepted && !toApply.name.isEmpty())
                recordRecentPalette(toApply);
            emit paletteSelected(toApply);
        });
    });
}

void PreferencesDialog::showPaletteListOverlay()
{
    if (m_overlay->isActive()) return;

    enum ResultCode { AddPalette = 1001, EditPalette = 1002 };
    auto editInfo = std::make_shared<std::shared_ptr<PaletteInfo>>();

    auto *dlg = new QDialog(this);
    removeDialogIcon(dlg);
    dlg->setWindowTitle(tr("Themes"));

    auto *header = new QWidget(dlg);
    header->setObjectName(QStringLiteral("overlayHeader"));
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(20, 20 + kHeaderTopGap, 20, kHeaderBottomGap);
    headerLay->setSpacing(8);
    auto *title = new QLabel(tr("All Palettes"), header);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    headerLay->addWidget(title);
    headerLay->addStretch();

    auto *gridWidget = new QWidget(dlg);
    auto *grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(20, 0, 20, 20);
    grid->setSpacing(5 + 2 * SW_SHADOW);
    grid->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    auto *group = new QButtonGroup(gridWidget);
    group->setExclusive(true);

    int count = 0;
    for (const PaletteInfo &info : m_palettes) {
        auto *sw = new PaletteSwatch(info, gridWidget);
        if (info.name == m_currentPalette.name)
            sw->setChecked(true);
        group->addButton(sw);
        grid->addWidget(sw, count / kSwatchCols, count % kSwatchCols);
        ++count;
        connect(sw, &QAbstractButton::clicked, this, [this, info]() {
            m_currentPalette = info;
            recordRecentPalette(info);
            emit paletteSelected(info);
        });
        auto sharedInfo = std::make_shared<PaletteInfo>(info);
        connect(sw, &PaletteSwatch::doubleClicked, dlg, [dlg, editInfo, sharedInfo]() {
            *editInfo = sharedInfo;
            dlg->done(EditPalette);
        });
    }

    auto *add = new PaletteSwatch(gridWidget);
    grid->addWidget(add, count / kSwatchCols, count % kSwatchCols);
    connect(add, &QAbstractButton::clicked, dlg, [dlg]() { dlg->done(AddPalette); });

    auto *scroll = new QScrollArea(dlg);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setWidget(gridWidget);

    auto *lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(header);
    lay->addWidget(scroll, 1);

    m_overlay->slideIn(dlg, [this, editInfo](int result) {
        if (result == AddPalette) {
            QTimer::singleShot(320, this, &PreferencesDialog::openAddPaletteEditor);
        } else if (result == EditPalette && *editInfo) {
            const auto selected = *editInfo;
            QTimer::singleShot(320, this, [this, selected]() {
                openEditPaletteEditor(selected);
            });
        }
    }, true);
}

void PreferencesDialog::addCustomSwatch(const PaletteInfo &)
{
    rebuildCustomSwatches();
}

void PreferencesDialog::rebuildCustomSwatches()
{
    const QString prevName = m_currentPalette.name;
    m_palettes = loadAllPalettes();
    for (const PaletteInfo &info : m_palettes) {
        if (info.name == prevName) {
            m_currentPalette = info;
            break;
        }
    }
    populateMainSwatches();

    const QString dir = paletteStorageDir();
    if (m_watcher && QDir(dir).exists() && !m_watcher->directories().contains(dir))
        m_watcher->addPath(dir);
}
