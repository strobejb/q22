#include "focusnavigation.h"
#include "scrollhintoverlay.h"
#include "chrome/dialog-chrome.h"
#include "preferences.h"
#include "chrome/roundedlistwidget.h"
#include "settingscard.h"
#include "settings.h"
#include "slideoverlay.h"
#include "theme.h"
#include "HexView/hexview.h"

#include <algorithm>
#include <functional>

#include <QApplication>
#include <QPropertyAnimation>
#include <QTimer>
#include <QDialogButtonBox>
#include <QScrollBar>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFocusEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
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

static bool isInPaletteSwatchGrid(QWidget *widget)
{
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (qobject_cast<PaletteSwatchGrid *>(w))
            return true;
    }
    return false;
}

// ─── FontPickerDialog ────────────────────────────────────────────────────────

FontPickerDialog::FontPickerDialog(const QFont &current, QWidget *parent)
    : QDialog(parent), m_font(current)
{
    removeDialogIcon(this);
    setWindowTitle(tr("Select Font"));
    resize(460, 540);

    // ── List ─────────────────────────────────────────────────────────────────
    m_list = new RoundedListWidget(this);
    {
        m_list->setStyleSheet(QStringLiteral(
            "RoundedListWidget {"
            "  border: 1px solid palette(mid);"
            "  border-radius: 6px;"
            "  background: palette(base);"
            "  outline: 0;"
            "}"
            "RoundedListWidget::item { padding-left: 4px; }"
        ));
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
    previewFrame->setObjectName(QStringLiteral("fontPreviewFrame"));
    previewFrame->setFrameShape(QFrame::NoFrame);
    previewFrame->setStyleSheet(QStringLiteral(R"(
        QFrame#fontPreviewFrame {
            border: 1px solid palette(mid);
            border-radius: 6px;
            background: palette(base);
        }
    )"));
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

// ─── ViewMoreButton ───────────────────────────────────────────────────────────


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
        m_icon = recoloredIcon("ui/go-next-symbolic",
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

        if (isDown() || underMouse()) {
            const QColor hover = isDown() ? pal.color(QPalette::Mid)
                                          : pal.color(QPalette::Button);
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        }
        if (m_keyboardFocus) {
            p.setPen(QPen(pal.color(QPalette::Highlight), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 7, 7);
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
    void focusInEvent(QFocusEvent *e) override
    {
        m_keyboardFocus = e->reason() == Qt::TabFocusReason
                       || e->reason() == Qt::BacktabFocusReason;
        update();
        QAbstractButton::focusInEvent(e);
    }
    void focusOutEvent(QFocusEvent *e) override
    {
        m_keyboardFocus = false;
        update();
        QAbstractButton::focusOutEvent(e);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const auto oldPolicy = focusPolicy();
        setFocusPolicy(Qt::NoFocus);
        QAbstractButton::mousePressEvent(e);
        setFocusPolicy(oldPolicy);
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            click();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
            e->accept();
            return;
        }
        QAbstractButton::keyPressEvent(e);
    }

private:
    static constexpr int kIconSz = 12;
    static constexpr int kGap = 5;
    static constexpr int kPadX = 8+2;
    static constexpr int kPadY = 8;//3+2;
    QIcon m_icon;
    bool m_keyboardFocus = false;
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
    else
        m_fontNav->setValueText(AppSettings::defaultHexFont().family());

    // ── Font size ────────────────────────────────────────────────────────────
    m_fontSize = new StepSpinBox(tr("Font Size"), 6, 72, 1, this);
    m_fontSize->setValue(AppSettings::prefFontSize());

    // ── Spacing ──────────────────────────────────────────────────────────────
    m_horizSpacing = new StepSpinBox(tr("Character Spacing"), 0, 20, 1, this);
    m_horizSpacing->setValue(AppSettings::prefHorizSpacing());

    m_lineSpacing = new StepSpinBox(tr("Line Spacing"), 0, 20, 1, this);
    m_lineSpacing->setValue(AppSettings::prefLineSpacing());

    // ── Window toggles ───────────────────────────────────────────────────────
    m_restoreWindowGeometry = new SettingsToggle(tr("Restore window size/pos"), this);
    m_restoreWindowGeometry->setChecked(AppSettings::prefRestoreWindowGeometry());

    m_nativeDialogs = new SettingsToggle(tr("Native dialogs"), this);
    m_nativeDialogs->setChecked(AppSettings::prefNativeDialogs());

    m_nativeFileDialogs = new SettingsToggle(tr("Native file picker"), this);
    m_nativeFileDialogs->setChecked(AppSettings::prefNativeFileDialogs());

    // ── Appearance toggles ───────────────────────────────────────────────────
    m_nativeMenu = new SettingsToggle(tr("Native menu bar"), this);
    m_nativeMenu->setChecked(AppSettings::prefNativeMenu());

    m_menuHighlight = new SettingsToggle(tr("Menus use highlight colour"), this);
    m_menuHighlight->setChecked(AppSettings::prefMenuHighlight());

    // ── Bookmark behaviour toggles ────────────────────────────────────────────
    m_bmAutoExpand = new SettingsToggle(tr("Expand automatically"), this);
    m_bmAutoExpand->setToolTip(tr("Automatically expand bookmarks when space allows or when navigating to them"));
    m_bmAutoExpand->setChecked(AppSettings::prefBookmarkAutoExpand());

    m_bmNested = new SettingsToggle(tr("Nested bookmarks"), this);
    m_bmNested->setToolTip(tr("Allow overlapping and nested bookmarks to be defined"));
    m_bmNested->setChecked(AppSettings::prefBookmarkNested());

    m_bmSelHighlights = new SettingsToggle(tr("Highlight bookmarked range"), this);
    m_bmSelHighlights->setToolTip(tr("When a bookmark is activated, automatically select its annotated byte range in the hex view"));
    m_bmSelHighlights->setChecked(AppSettings::prefBookmarkSelectionHighlights());

    // ── Signal connections ────────────────────────────────────────────────────
    connect(m_fontSize, &StepSpinBox::valueChanged,
            this, [this](int size) {
        AppSettings::setPrefFontSize(size);
        emit fontChanged(AppSettings::hexFont());
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
    connect(m_restoreWindowGeometry, &SettingsToggle::toggled,
            this, [](bool on) {
        AppSettings::setPrefRestoreWindowGeometry(on);
    });
    connect(m_nativeFileDialogs, &SettingsToggle::toggled,
            this, [](bool on) {
        AppSettings::setPrefNativeFileDialogs(on);
    });
    connect(m_nativeDialogs, &SettingsToggle::toggled,
            this, [this](bool on) {
        AppSettings::setPrefNativeDialogs(on);
        emit nativeDialogsChanged(on);
    });
    connect(m_menuHighlight, &SettingsToggle::toggled,
            this, [this](bool on) {
        AppSettings::setPrefMenuHighlight(on);
        emit menuHighlightChanged(on);
    });
    connect(m_bmAutoExpand, &SettingsToggle::toggled,
            this, [this](bool on) {
        AppSettings::setPrefBookmarkAutoExpand(on);
        const uint mask = HVS_BOOKMARK_EXPAND_LONE |
                          HVS_BOOKMARK_EXPAND_CURSOR |
                          HVS_BOOKMARK_EXPAND_ALWAYS;
        emit bookmarkStyleChanged(mask, on ? mask : 0);
    });
    connect(m_bmNested, &SettingsToggle::toggled,
            this, [this](bool on) {
        AppSettings::setPrefBookmarkNested(on);
        emit bookmarkStyleChanged(HVS_BOOKMARK_NESTED,
                                  on ? HVS_BOOKMARK_NESTED : 0);
    });
    connect(m_bmSelHighlights, &SettingsToggle::toggled,
            this, [this](bool on) {
        AppSettings::setPrefBookmarkSelectionHighlights(on);
        emit bookmarkStyleChanged(HVS_BOOKMARK_SELECTION_HIGHLIGHTS,
                                  on ? HVS_BOOKMARK_SELECTION_HIGHLIGHTS : 0);
    });

    // ── Cards ─────────────────────────────────────────────────────────────────
    auto *fontGroup   = new SettingsCard(
        {m_fontNav, m_fontSize, m_horizSpacing, m_lineSpacing},
        SettingsCard::Style::Spaced, this);
    auto *windowGroup = new SettingsCard(
        {m_restoreWindowGeometry, m_nativeDialogs, m_nativeFileDialogs},
        SettingsCard::Style::Spaced, this);
    auto *appearGroup = new SettingsCard(
        {m_nativeMenu, m_menuHighlight},
        SettingsCard::Style::Spaced, this);
    auto *bmGroup = new SettingsCard(
        {m_bmAutoExpand, m_bmNested, m_bmSelHighlights},
        SettingsCard::Style::Spaced, this);

    // ── Reset button ──────────────────────────────────────────────────────────
    auto *resetBtn  = new DangerButton(tr("Reset to defaults"), this);
    m_lastFocusWidget = resetBtn;
    auto *resetCard = new SettingsCard({resetBtn}, SettingsCard::Style::Spaced, this);

    // ── Theme swatches ────────────────────────────────────────────────────────
    m_swatchGrid = new PaletteSwatchGrid(this);
    {
        m_palettes = reloadPalettes(QDir(paletteStorageDir()));
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

        m_swatchGrid->setAllowFocusEscape(true);
        connect(m_swatchGrid, &PaletteSwatchGrid::paletteSelected,
                this, [this](const PaletteInfo &info) {
            m_currentPalette = info;
            recordRecentPalette(info);
            emit paletteSelected(info);
        });
        connect(m_swatchGrid, &PaletteSwatchGrid::paletteEditRequested,
                this, &PreferencesDialog::openEditPaletteEditor);
        connect(m_swatchGrid, &PaletteSwatchGrid::addRequested,
                this, &PreferencesDialog::openAddPaletteEditor);
        populateMainSwatches();

    }

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
    m_swatchGrid->setBoundaryWidgets(m_viewMore, m_fontNav);
    vlay->addWidget(themeHeader);
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(m_swatchGrid);
    vlay->addSpacing(kGroupTopGap);
    vlay->addWidget(makeSectionLabel(tr("Font")));
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(fontGroup);
    vlay->addSpacing(kGroupTopGap);
    vlay->addWidget(makeSectionLabel(tr("Window")));
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(windowGroup);
    vlay->addSpacing(kGroupTopGap);
    vlay->addWidget(makeSectionLabel(tr("Appearance")));
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(appearGroup);
    vlay->addSpacing(kGroupTopGap);
    vlay->addWidget(makeSectionLabel(tr("Bookmarks")));
    vlay->addSpacing(kHeaderBottomGap);
    vlay->addWidget(bmGroup);
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
    ScrollHintOverlay::install(scroll);
    scroll->verticalScrollBar()->setProperty("preferenceScrollBar", true);
    scroll->verticalScrollBar()->style()->unpolish(scroll->verticalScrollBar());
    scroll->verticalScrollBar()->style()->polish(scroll->verticalScrollBar());

    auto *dialogLay = new QVBoxLayout(this);
    dialogLay->setContentsMargins(0, 0, 0, 0);
    dialogLay->addWidget(scroll);

    // Overlay for child dialogs — must be created after the dialog layout so it
    // stacks on top of the scroll area when raised.
    m_overlay = new SlideOverlay(this);

    // Font picker opens as an overlay slide-in
    connect(m_fontNav, &QAbstractButton::clicked, this, [this]() {
        if (hasActiveOverlay()) return;
        auto *dlg = new FontPickerDialog(AppSettings::hexFont(), this);
        m_overlay->slideIn(dlg, [this, dlg](int result) {
            if (result == QDialog::Accepted) {
                m_fontFamily = dlg->selectedFont().family();
                m_fontNav->setValueText(m_fontFamily);
                AppSettings::setPrefFontFamily(m_fontFamily);
                emit fontChanged(AppSettings::hexFont());
            }
        });
    });

    setSizeGripEnabled(false);

    // Hide this modeless dialog whenever any modal window opens, restore when it closes.
    qApp->installEventFilter(this);
}

void PreferencesDialog::prepareShow()
{
    installDialogChrome(this);

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
    const int gridW = m_swatchGrid->gridWidthForColumns(kSwatchCols);
    const int sbW = (hint.height() > maxH)
                    ? style()->pixelMetric(QStyle::PM_ScrollBarExtent) : 0;
    const int w = gridW + 2 * 20 + sbW;
    const int h = qMin(hint.height(), maxH);
    // Mirror execCentered(): resize -> move -> hidden HWND creation, then lock.
    // setFixedSize() before move() changes size constraints that affect HWND
    // creation geometry on Windows, producing a wrong initial position.
    prepareDialogForShow(this, QSize(w, h));
    setFixedSize(size()); // lock size after HWND is in place
}

void PreferencesDialog::setVisible(bool visible)
{
    const bool opening = visible && !isVisible() && !m_hiddenByModal;
    if (!visible) {
        // Only dismiss overlay on an explicit close, not a temporary modal hide.
        if (!m_hiddenByModal) {
            std::function<void(SlideOverlay *)> dismissStack = [&](SlideOverlay *overlay) {
                if (!overlay || !overlay->isActive())
                    return;
                const auto children = overlay->findChildren<SlideOverlay *>(
                    QString(), Qt::FindDirectChildrenOnly);
                for (SlideOverlay *child : children)
                    dismissStack(child);
                overlay->dismiss();
            };
            dismissStack(m_overlay);
        }
    } else if (!isVisible() && !m_hiddenByModal) {
        // Fallback: centre on parent if prepareShow() wasn't called first.
        prepareShow();
    }
    if (opening)
        populateMainSwatches();
    QDialog::setVisible(visible);
    if (visible) {
        // Preferences contains dynamically rebuilt palette swatches, so refresh
        // the visual tab order each time the modeless dialog is shown.
        FocusNavigation::assignTabOrder(this);
        m_swatchGrid->focusCurrent();
    }
}

bool PreferencesDialog::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() == QEvent::FocusIn) {
        auto *w = qobject_cast<QWidget *>(obj);
        if (w && (w == this || isAncestorOf(w)))
            FocusNavigation::ensureFocusedWidgetVisible(w);
    }

    if (e->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(e);
        auto *w = qobject_cast<QWidget *>(obj);
        const bool backtab = key->key() == Qt::Key_Backtab
                          || (key->key() == Qt::Key_Tab
                              && key->modifiers().testFlag(Qt::ShiftModifier));
        if (w == m_viewMore
                && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Tab
                    || key->key() == Qt::Key_Backtab)) {
            if (backtab) {
                if (m_lastFocusWidget && m_lastFocusWidget->focusPolicy() != Qt::NoFocus)
                    m_lastFocusWidget->setFocus(Qt::BacktabFocusReason);
                key->accept();
                return true;
            }
            m_swatchGrid->focusFirst(Qt::TabFocusReason);
            key->accept();
            return true;
        }
        if (w == m_fontNav && backtab) {
            m_swatchGrid->focusLast(Qt::BacktabFocusReason);
            key->accept();
            return true;
        }

        const bool up = key->key() == Qt::Key_Up;
        const bool down = key->key() == Qt::Key_Down;
        if ((up || down)
                && w
                && (w == this || isAncestorOf(w))
                && !isInPaletteSwatchGrid(w)
                && !FocusNavigation::hasFocusableWidget(
                    this, w, up ? FocusNavigation::Direction::Up
                                : FocusNavigation::Direction::Down)) {
            return true;
        }
    }

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

    return false;
}


void PreferencesDialog::populateMainSwatches()
{
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

    m_swatchGrid->setPalettes(ordered, m_currentPalette.name, kMainPaletteLimit);
}

void PreferencesDialog::openAddPaletteEditor()
{
    auto *overlay = nextOverlay();
    if (!overlay || overlay->isActive()) return;
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
        applyAdwaitaTheme(cs);
    });
    overlay->slideIn(dlg, [this, dlg, before, savedScheme](int result) {
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

void PreferencesDialog::openEditPaletteEditor(const PaletteInfo &info)
{
    auto *overlay = nextOverlay();
    if (!overlay || overlay->isActive()) return;
    const PaletteInfo before = m_currentPalette;
    const auto savedScheme = static_cast<ColorScheme>(AppSettings::prefColorScheme());
    auto *dlg = new PaletteEditorDialog(info, this);
    connect(dlg, &PaletteEditorDialog::paletteChanged,
            this, &PreferencesDialog::paletteSelected);
    connect(dlg, &PaletteEditorDialog::paletteSaved,
            this, &PreferencesDialog::addCustomSwatch);
    connect(dlg, &PaletteEditorDialog::previewModeRequested,
            this, [savedScheme](int mode) {
        const ColorScheme cs = mode == 1 ? ColorScheme::Light
                            : mode == 2 ? ColorScheme::Dark : savedScheme;
        applyAdwaitaTheme(cs);
    });
    overlay->slideIn(dlg, [this, dlg, before, savedScheme](int result) {
        const bool accepted = (result == QDialog::Accepted);
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
    if (hasActiveOverlay()) return;
    if (m_viewMore) {
        m_viewMore->setDown(false);
        m_viewMore->clearFocus();
        m_viewMore->update();
    }

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

    auto *grid = new PaletteSwatchGrid(dlg);
    grid->setGridContentsMargins(20, 0, 20, 20);
    grid->setAllowFocusEscape(false);
    grid->setPalettes(m_palettes, m_currentPalette.name);
    connect(grid, &PaletteSwatchGrid::paletteSelected,
            this, [this](const PaletteInfo &info) {
        m_currentPalette = info;
        recordRecentPalette(info);
        emit paletteSelected(info);
        populateMainSwatches();
    });
    connect(grid, &PaletteSwatchGrid::paletteEditRequested,
            this, [this](const PaletteInfo &info) {
        openEditPaletteEditor(info);
    });
    connect(grid, &PaletteSwatchGrid::addRequested,
            this, [this]() { openAddPaletteEditor(); });

    auto *scroll = new QScrollArea(dlg);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setWidget(grid);
    ScrollHintOverlay::install(scroll);
    scroll->verticalScrollBar()->setProperty("preferenceScrollBar", true);
    scroll->verticalScrollBar()->style()->unpolish(scroll->verticalScrollBar());
    scroll->verticalScrollBar()->style()->polish(scroll->verticalScrollBar());

    auto *lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(header);
    lay->addWidget(scroll, 1);

    QTimer::singleShot(0, dlg, [dlg, grid]() {
        // The overlay has just been embedded/reparented; wait until layout
        // geometry is valid, then build the visual tab chain and focus a swatch.
        auto *backButton = dlg->findChild<QWidget *>(QStringLiteral("overlayBackButton"));
        grid->setBoundaryWidgets(backButton, backButton);
        FocusNavigation::assignTabOrder(dlg);
        grid->focusCurrent();
    });

    m_overlay->slideIn(dlg, [this](int result) {
        Q_UNUSED(result);
        if (m_viewMore) {
            m_viewMore->setDown(false);
            m_viewMore->clearFocus();
            m_viewMore->update();
        }
        QTimer::singleShot(0, this, [this]() { m_swatchGrid->focusCurrent(); });
    }, true);
}

bool PreferencesDialog::hasActiveOverlay() const
{
    return activeOverlay() != nullptr;
}

SlideOverlay *PreferencesDialog::activeOverlay() const
{
    if (!m_overlay || !m_overlay->isActive())
        return nullptr;

    SlideOverlay *top = m_overlay;
    while (true) {
        SlideOverlay *activeChild = nullptr;
        const auto children = top->findChildren<SlideOverlay *>(QString(),
                                                                Qt::FindDirectChildrenOnly);
        for (SlideOverlay *child : children) {
            if (child->isActive()) {
                activeChild = child;
                break;
            }
        }
        if (!activeChild)
            return top;
        top = activeChild;
    }
}

SlideOverlay *PreferencesDialog::nextOverlay()
{
    SlideOverlay *parentOverlay = activeOverlay();
    if (!parentOverlay)
        return m_overlay;

    const auto children = parentOverlay->findChildren<SlideOverlay *>(QString(),
                                                                      Qt::FindDirectChildrenOnly);
    for (SlideOverlay *child : children) {
        if (!child->isActive())
            return child;
    }

    return new SlideOverlay(parentOverlay);
}

void PreferencesDialog::addCustomSwatch(const PaletteInfo &)
{
    rebuildCustomSwatches();
}

void PreferencesDialog::refreshPalettes()
{
    rebuildCustomSwatches();
}

void PreferencesDialog::rebuildCustomSwatches()
{
    const QString prevName = m_currentPalette.name;
    m_palettes = reloadPalettes(QDir(paletteStorageDir()));
    for (const PaletteInfo &info : m_palettes) {
        if (info.name == prevName) {
            m_currentPalette = info;
            break;
        }
    }
    populateMainSwatches();

}
