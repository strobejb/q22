#include "preferences.h"
#include "settingscard.h"
#include "settings.h"
#include "slideoverlay.h"
#include "theme.h"

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
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

// ─── FontPickerDialog ────────────────────────────────────────────────────────

FontPickerDialog::FontPickerDialog(const QFont &current, QWidget *parent)
    : QDialog(parent), m_font(current)
{
    removeDialogIcon(this);
    setWindowTitle(tr("Select Font"));
    resize(460, 540);

    // ── List ─────────────────────────────────────────────────────────────────
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    {
        const int vPad = qMax(4, m_list->fontMetrics().height() / 2);
        const bool dark = qApp->palette().window().color().lightness() < 128;
        const QString border = dark ? QLatin1String("rgba(255,255,255,0.18)")
                                    : QLatin1String("rgba(0,0,0,0.15)");
        m_list->setStyleSheet(QString(
            "QListWidget { border: 1px solid %1; outline: 0; }"
            "QListWidget::item { padding: %2px 4px; }"
        ).arg(border).arg(vPad));
    }

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
    vlay->setSpacing(14);
    vlay->addWidget(fontHeader);
    vlay->addWidget(m_list, 1);
    vlay->addWidget(previewFrame);
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


static constexpr int kSwatchCols = 4;

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
        m_swatchLayout->setSpacing(10 + 2 * SW_SHADOW);
        m_swatchLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        m_swatchGroup = new QButtonGroup(m_swatchWidget);
        m_swatchGroup->setExclusive(true);

        QList<PaletteInfo> palettes = loadAllPalettes();
        if (!palettes.isEmpty())
            m_currentPalette = palettes.first();

        const QString savedName = AppSettings::prefPaletteName();

        for (const PaletteInfo &info : palettes) {
            auto *sw = new PaletteSwatch(info, m_swatchWidget);
            if (!savedName.isEmpty() && info.name == savedName) {
                sw->setChecked(true);
                m_currentPalette = info;
            }
            m_swatchGroup->addButton(sw);
            m_swatchLayout->addWidget(sw, m_swatchCount / kSwatchCols,
                                          m_swatchCount % kSwatchCols);
            ++m_swatchCount;
            connect(sw, &QAbstractButton::clicked, this, [this, info]() {
                m_currentPalette = info;
                emit paletteSelected(info);
            });
            auto sharedInfo = std::make_shared<PaletteInfo>(info);
            connect(sw, &PaletteSwatch::doubleClicked, this, [this, sharedInfo]() {
                if (m_overlay->isActive()) return;
                const PaletteInfo before = m_currentPalette;
                auto *dlg = new PaletteEditorDialog(*sharedInfo, this);
                connect(dlg, &PaletteEditorDialog::paletteChanged,
                        this, &PreferencesDialog::paletteSelected);
                connect(dlg, &PaletteEditorDialog::paletteSaved,
                        this, &PreferencesDialog::addCustomSwatch);
                m_overlay->slideIn(dlg, [this, dlg, sharedInfo, before](int result) {
                    if (result == QDialog::Accepted)
                        *sharedInfo = dlg->currentInfo();
                    else
                        emit paletteSelected(before);
                });
            });
        }

        m_addBtn = new PaletteSwatch(m_swatchWidget);
        connect(m_addBtn, &QAbstractButton::clicked, this, [this]() {
            if (m_overlay->isActive()) return;
            const PaletteInfo before = m_currentPalette;
            auto *dlg = new PaletteEditorDialog(m_currentPalette, this);
            connect(dlg, &PaletteEditorDialog::paletteChanged,
                    this, &PreferencesDialog::paletteSelected);
            connect(dlg, &PaletteEditorDialog::paletteSaved,
                    this, &PreferencesDialog::addCustomSwatch);
            m_overlay->slideIn(dlg, [this, before](int result) {
                if (result != QDialog::Accepted)
                    emit paletteSelected(before);
            });
        });
        m_swatchLayout->addWidget(m_addBtn, m_swatchCount / kSwatchCols,
                                             m_swatchCount % kSwatchCols);

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
    vlay->addWidget(makeSectionLabel(tr("Theme")));
    vlay->addSpacing(6);
    vlay->addWidget(m_swatchWidget);
    vlay->addSpacing(16);
    vlay->addWidget(makeSectionLabel(tr("Font")));
    vlay->addSpacing(6);
    vlay->addWidget(fontGroup);
    vlay->addSpacing(16);
    vlay->addWidget(makeSectionLabel(tr("Appearance")));
    vlay->addSpacing(6);
    vlay->addWidget(appearGroup);
    vlay->addSpacing(16);
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
    setMinimumWidth(460);

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
    const int w = qMax(hint.width(), minimumWidth());
    const int h = qMin(hint.height(), maxH);
    setFixedSize(w, h);
    if (QWidget *par = parentWidget()) {
        const QPoint c = par->frameGeometry().center();
        move(c.x() - w / 2, c.y() - h / 2);
    }
#ifdef Q_OS_WIN
    // Force HWND creation NOW, while the window is still hidden, so the
    // subsequent ShowWindow call just flips visibility without repositioning.
    // This must happen BEFORE setVisible/show() is entered — the same
    // pattern execCentered() uses for modal dialogs.
    (void)winId();
#endif
}

void PreferencesDialog::setVisible(bool visible)
{
    if (!visible) {
        // Only dismiss overlay on an explicit close, not a temporary modal hide.
        if (!m_hiddenByModal && m_overlay->isActive())
            m_overlay->dismiss();
    } else if (!isVisible() && !m_hiddenByModal) {
        // Fallback: centre on parent if prepareShow() wasn't called first.
        prepareShow();
    }
    QDialog::setVisible(visible);
    if (visible)
        m_swatchWidget->setFocus();
}

bool PreferencesDialog::eventFilter(QObject *obj, QEvent *e)
{
    // ── Modal watch (installed on qApp — runs for all QObjects) ──────────────
    if (e->type() == QEvent::Show) {
        auto *w = qobject_cast<QWidget *>(obj);
        if (w && w->isWindow() && w != this
                && w->windowModality() != Qt::NonModal
                && isVisible() && !m_hiddenByModal) {
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

    const auto palBtns  = m_swatchGroup->buttons();
    const int  palCount = palBtns.size();

    if (e->type() == QEvent::FocusIn) {
        // Initialise cursor at the currently checked swatch (or 0).
        auto *checked  = m_swatchGroup->checkedButton();
        m_swatchCursor = checked ? palBtns.indexOf(checked) : 0;
        if (m_swatchCursor < 0) m_swatchCursor = 0;
        return false;
    }

    if (e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(e)->key();
        int next = m_swatchCursor;

        switch (key) {
        case Qt::Key_Right: next = qMin(m_swatchCursor + 1,           palCount - 1); break;
        case Qt::Key_Left:  next = qMax(m_swatchCursor - 1,           0);            break;
        case Qt::Key_Up:    next = qMax(m_swatchCursor - kSwatchCols, 0);            break;
        case Qt::Key_Down:
            next = m_swatchCursor + kSwatchCols;
            if (next >= palCount) {
                focusNextPrevChild(true); // hand off to next Tab stop
                return true;
            }
            break;
        default: return false;
        }

        if (next != m_swatchCursor) {
            m_swatchCursor = next;
            palBtns[m_swatchCursor]->click();
        }
        return true; // consume — don't let the scroll area scroll
    }

    return false;
}


void PreferencesDialog::addCustomSwatch(const PaletteInfo &)
{
    rebuildCustomSwatches();
}

void PreferencesDialog::rebuildCustomSwatches()
{
    static constexpr int kSwatchCols = 4;
    const QString prevName = m_currentPalette.name;

    const auto old = m_swatchGroup->buttons();
    for (auto *b : old) delete b;
    m_swatchCount = 0;
    m_swatchLayout->removeWidget(m_addBtn);

    QList<PaletteInfo> palettes = loadAllPalettes();

    for (const PaletteInfo &info : palettes) {
        auto *sw = new PaletteSwatch(info, m_swatchWidget);
        if (info.name == prevName) {
            sw->setChecked(true);
            m_currentPalette = info;
        }
        sw->installEventFilter(this);
        m_swatchGroup->addButton(sw);
        m_swatchLayout->addWidget(sw, m_swatchCount / kSwatchCols,
                                      m_swatchCount % kSwatchCols);
        ++m_swatchCount;
        connect(sw, &QAbstractButton::clicked, this, [this, info]() {
            m_currentPalette = info;
            emit paletteSelected(info);
        });
        auto sharedInfo = std::make_shared<PaletteInfo>(info);
        connect(sw, &PaletteSwatch::doubleClicked, this, [this, sharedInfo]() {
            if (m_overlay->isActive()) return;
            const PaletteInfo before = m_currentPalette;
            auto *dlg = new PaletteEditorDialog(*sharedInfo, this);
            connect(dlg, &PaletteEditorDialog::paletteChanged,
                    this, &PreferencesDialog::paletteSelected);
            connect(dlg, &PaletteEditorDialog::paletteSaved,
                    this, &PreferencesDialog::addCustomSwatch);
            m_overlay->slideIn(dlg, [this, dlg, sharedInfo, before](int result) {
                if (result == QDialog::Accepted)
                    *sharedInfo = dlg->currentInfo();
                else
                    emit paletteSelected(before);
            });
        });
    }

    m_swatchLayout->addWidget(m_addBtn, m_swatchCount / kSwatchCols,
                                        m_swatchCount % kSwatchCols);

    const QString dir = paletteStorageDir();
    if (m_watcher && QDir(dir).exists() && !m_watcher->directories().contains(dir))
        m_watcher->addPath(dir);
}
