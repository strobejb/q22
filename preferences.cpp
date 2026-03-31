#include "preferences.h"
#include "settings.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
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
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

// ─── SettingsToggle ──────────────────────────────────────────────────────────

static constexpr int PILL_W         = 40;
static constexpr int PILL_H         = 22;
static constexpr int THUMB_MARGIN   =  2;
static constexpr int TOGGLE_SPACING = 16;
static constexpr int TOGGLE_VPAD    =  4;

SettingsToggle::SettingsToggle(const QString &text, QWidget *parent)
    : QAbstractButton(parent)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize SettingsToggle::sizeHint() const
{
    const QFontMetrics fm(font());
    const int textW = fm.horizontalAdvance(text());
    const int h     = qMax(fm.height(), PILL_H) + 2 * TOGGLE_VPAD;
    return QSize(textW + TOGGLE_SPACING + PILL_W, h);
}

void SettingsToggle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette &pal = palette();
    const QRect     r   = rect();

    // ── Label (left side) ────────────────────────────────────────────────────
    const QFontMetrics fm(font());
    const int ty = (r.height() + fm.ascent() - fm.descent()) / 2;
    p.setPen(isEnabled() ? pal.color(QPalette::WindowText)
                         : pal.color(QPalette::Disabled, QPalette::WindowText));
    p.setFont(font());
    p.drawText(QPoint(0, ty), text());

    // ── Pill track (right side) ───────────────────────────────────────────────
    const int    pillX = r.width() - PILL_W;
    const int    pillY = (r.height() - PILL_H) / 2;
    const QRectF pill(pillX, pillY, PILL_W, PILL_H);

    const QColor trackOn  = pal.color(QPalette::Highlight);
    const QColor trackOff = pal.color(QPalette::Mid);

    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? trackOn : trackOff);
    p.drawRoundedRect(pill, PILL_H / 2.0, PILL_H / 2.0);

    // ── Thumb ─────────────────────────────────────────────────────────────────
    const int thumbD = PILL_H - 2 * THUMB_MARGIN;
    const int thumbX = isChecked()
                         ? pillX + PILL_W - THUMB_MARGIN - thumbD
                         : pillX + THUMB_MARGIN;
    p.setBrush(Qt::white);
    p.drawEllipse(QRect(thumbX, pillY + THUMB_MARGIN, thumbD, thumbD));
}

// ─── StepSpinBox ─────────────────────────────────────────────────────────────

static constexpr int SSB_BTN_W   = 26;   // width of each half of the button pair
static constexpr int SSB_BTN_H   = 22;   // height of the button group
static constexpr int SSB_RADIUS  =  5;   // corner radius
static constexpr int SSB_VPAD    =  4;   // top/bottom padding
static constexpr int SSB_VAL_GAP =  8;   // gap between value text and buttons
static constexpr int SSB_VAL_W   = 24;   // reserved width for value text

StepSpinBox::StepSpinBox(const QString &label, int min, int max, int step,
                         QWidget *parent)
    : QWidget(parent), m_label(label), m_value(min), m_min(min), m_max(max),
      m_step(step)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void StepSpinBox::setValue(int v)
{
    v = qBound(m_min, v, m_max);
    if (v == m_value)
        return;
    m_value = v;
    update();
    emit valueChanged(m_value);
}

QSize StepSpinBox::sizeHint() const
{
    const QFontMetrics fm(font());
    const int h = qMax(fm.height(), SSB_BTN_H) + 2 * SSB_VPAD;
    const int w = fm.horizontalAdvance(m_label) + TOGGLE_SPACING
                  + SSB_VAL_W + SSB_VAL_GAP + SSB_BTN_W * 2 + 1;
    return QSize(w, h);
}

QRect StepSpinBox::groupRect() const
{
    const int gy = (height() - SSB_BTN_H) / 2;
    return QRect(width() - SSB_BTN_W * 2 - 1, gy, SSB_BTN_W * 2 + 1, SSB_BTN_H);
}

QRect StepSpinBox::minusRect() const
{
    const QRect g = groupRect();
    return QRect(g.left(), g.top(), SSB_BTN_W, g.height());
}

QRect StepSpinBox::plusRect() const
{
    const QRect g = groupRect();
    return QRect(g.left() + SSB_BTN_W + 1, g.top(), SSB_BTN_W, g.height());
}

StepSpinBox::HitZone StepSpinBox::hitZone(const QPoint &pos) const
{
    if (minusRect().contains(pos)) return Minus;
    if (plusRect().contains(pos))  return Plus;
    return None;
}

void StepSpinBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette &pal = palette();
    const QRect     r   = rect();
    const QFontMetrics fm(font());

    // ── Label (left) ─────────────────────────────────────────────────────────
    const int ty = (r.height() + fm.ascent() - fm.descent()) / 2;
    p.setPen(pal.color(QPalette::WindowText));
    p.setFont(font());
    p.drawText(QPoint(0, ty), m_label);

    // ── Value text (right of label, left of buttons) ──────────────────────────
    const QRect grp  = groupRect();
    const QRect valR(0, 0, grp.left() - SSB_VAL_GAP, r.height());
    p.drawText(valR, Qt::AlignRight | Qt::AlignVCenter, QString::number(m_value));

    // ── Button group background ───────────────────────────────────────────────
    const QColor btnBg     = pal.color(QPalette::Button);
    const QColor btnBorder = pal.color(QPalette::Mid);
    const QRectF grpF(grp.adjusted(0, 0, -1, -1));  // half-pixel inset for border

    p.setPen(QPen(btnBorder, 1));
    p.setBrush(btnBg);
    p.drawRoundedRect(grpF.adjusted(0.5, 0.5, -0.5, -0.5),
                      SSB_RADIUS, SSB_RADIUS);

    // ── Hover / press overlays ────────────────────────────────────────────────
    const bool dark       = pal.color(QPalette::Window).lightness() < 128;
    const QColor hoverCol = dark ? QColor(255,255,255, 30) : QColor(0,0,0, 22);
    const QColor presCol  = dark ? QColor(255,255,255, 55) : QColor(0,0,0, 45);

    auto drawOverlay = [&](HitZone zone, const QRect &btnR) {
        if (m_pressed != zone && m_hover != zone) return;
        const QColor col = (m_pressed == zone) ? presCol : hoverCol;

        p.save();
        QPainterPath clip;
        clip.addRoundedRect(grpF, SSB_RADIUS, SSB_RADIUS);
        p.setClipPath(clip);
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawRect(btnR);
        p.restore();
    };
    drawOverlay(Minus, minusRect());
    drawOverlay(Plus,  plusRect());

    // ── Divider ───────────────────────────────────────────────────────────────
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(btnBorder);
    const int divX = grp.left() + SSB_BTN_W;
    p.drawLine(divX, grp.top() + 3, divX, grp.bottom() - 3);

    // ── Button symbols ────────────────────────────────────────────────────────
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(pal.color(QPalette::ButtonText));
    p.setFont(font());
    p.drawText(minusRect(), Qt::AlignCenter, "\u2212");
    p.drawText(plusRect(),  Qt::AlignCenter, "+");
}

void StepSpinBox::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressed = hitZone(e->pos());
        update();
    }
}

void StepSpinBox::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (m_pressed != None && hitZone(e->pos()) == m_pressed) {
            if (m_pressed == Minus) setValue(m_value - m_step);
            else                    setValue(m_value + m_step);
        }
        m_pressed = None;
        m_hover   = hitZone(e->pos());
        update();
    }
}

void StepSpinBox::mouseMoveEvent(QMouseEvent *e)
{
    const HitZone h = hitZone(e->pos());
    if (h != m_hover) {
        m_hover = h;
        update();
    }
}

void StepSpinBox::leaveEvent(QEvent *)
{
    m_hover   = None;
    m_pressed = None;
    update();
}

// ─── FontPickerDialog ────────────────────────────────────────────────────────

FontPickerDialog::FontPickerDialog(const QFont &current, QWidget *parent)
    : QDialog(parent), m_font(current)
{
    setWindowTitle(tr("Select Font"));
    resize(460, 540);

    // ── List ─────────────────────────────────────────────────────────────────
    m_list = new QListWidget(this);
    m_list->setUniformItemSizes(true);
    m_list->setStyleSheet(
        "QListWidget {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "}"
    );

    int selectRow = -1;
    const QStringList families = QFontDatabase::families();
    for (const QString &family : families) {
        if (!QFontDatabase::isFixedPitch(family))
            continue;
        const QStringList styles = QFontDatabase::styles(family);
        for (const QString &style : styles) {
            const QString sl = style.toLower();
            const bool isNormal = (sl == "regular" || sl == "normal" || sl.isEmpty());
            const QString label = isNormal ? family : (family + "  " + style);

            auto *item = new QListWidgetItem(label);
            item->setData(Qt::UserRole,     family);
            item->setData(Qt::UserRole + 1, style);
            m_list->addItem(item);

            const int row = m_list->count() - 1;
            if (family == current.family()) {
                if (selectRow == -1 || isNormal)
                    selectRow = row;
            }
        }
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

    // ── Layout ────────────────────────────────────────────────────────────────
    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(20, 20, 20, 20);
    vlay->setSpacing(14);
    vlay->addWidget(m_list, 1);
    vlay->addWidget(previewFrame);
    vlay->addWidget(buttons);

    // ── Selection → preview ───────────────────────────────────────────────────
    connect(m_list, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *item) {
        if (!item) return;
        const QString family = item->data(Qt::UserRole).toString();
        const QString style  = item->data(Qt::UserRole + 1).toString();
        m_font = QFontDatabase::font(family, style, m_font.pointSize() > 0
                                                    ? m_font.pointSize() : 13);
        updatePreview();
    });

    updatePreview();
}

void FontPickerDialog::updatePreview()
{
    m_preview->setFont(m_font);
}

// ─── FontNavButton ───────────────────────────────────────────────────────────
// Right-aligned button showing the current font name and a painted chevron.
// No background — sits inside a labelled row alongside a "Font" QLabel.

static constexpr int CHEV_SIZE = 6;   // half-height of the chevron arms
static constexpr int CHEV_GAP  = 8;   // space between text and chevron

class FontNavButton : public QAbstractButton
{
public:
    explicit FontNavButton(QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
    }

    QFont selectedFont() const { return m_font; }
    void setSelectedFont(const QFont &f) { m_font = f; updateGeometry(); update(); }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        const QString name = m_font.family().isEmpty() ? tr("None") : m_font.family();
        const int w = fm.horizontalAdvance(name) + CHEV_GAP + CHEV_SIZE + 4;
        const int h = qMax(fm.height(), PILL_H) + 2 * TOGGLE_VPAD;
        return QSize(w, h);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QPalette &pal = palette();
        const QRect     r   = rect();

        // ── Font family name ──────────────────────────────────────────────────
        const QString name = m_font.family().isEmpty() ? tr("None") : m_font.family();
        p.setFont(font());
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(QRect(0, 0, r.width() - CHEV_GAP - CHEV_SIZE - 4, r.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, name);

        // ── Chevron ───────────────────────────────────────────────────────────
        const int cx = r.width() - CHEV_SIZE - 2;
        const int cy = r.height() / 2;
        p.setPen(QPen(pal.color(QPalette::WindowText), 1.5,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath ch;
        ch.moveTo(cx,              cy - CHEV_SIZE);
        ch.lineTo(cx + CHEV_SIZE,  cy);
        ch.lineTo(cx,              cy + CHEV_SIZE);
        p.drawPath(ch);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
            FontPickerDialog dlg(m_font, window());
            if (dlg.exec() == QDialog::Accepted) {
                m_font = dlg.selectedFont();
                updateGeometry();
                update();
                emit clicked();
            }
        }
    }

private:
    QFont m_font;
};



// ─── AddPaletteSwatch ─────────────────────────────────────────────────────────

class AddPaletteSwatch : public QAbstractButton
{
public:
    explicit AddPaletteSwatch(QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(SW_W, SW_H);
        setToolTip(tr("Add palette…"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QPalette &pal = palette();
        const bool      hov = underMouse();
        const QRectF    r   = QRectF(rect()).adjusted(SW_BORDER, SW_BORDER, -SW_BORDER, -SW_BORDER);

        p.setPen(Qt::NoPen);
        p.setBrush(hov ? pal.color(QPalette::Midlight) : pal.color(QPalette::Button));
        p.drawRoundedRect(r, SW_RADIUS, SW_RADIUS);

        p.setPen(QPen(pal.color(QPalette::Mid), SW_BORDER));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(SW_BORDER * 0.5, SW_BORDER * 0.5,
                                     -SW_BORDER * 0.5, -SW_BORDER * 0.5),
                          SW_RADIUS - SW_BORDER * 0.5, SW_RADIUS - SW_BORDER * 0.5);

        // "+" glyph
        QFont f = font();
        f.setPixelSize(24);
        p.setFont(f);
        p.setPen(pal.color(QPalette::Mid));
        p.drawText(rect(), Qt::AlignCenter, "+");
    }

    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent       *e) override { update(); QAbstractButton::leaveEvent(e); }
};



// ─── DangerButton ────────────────────────────────────────────────────────────
// Full-width clickable row that renders its label in the danger/red colour.

class DangerButton : public QAbstractButton
{
public:
    explicit DangerButton(const QString &text, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        return QSize(fm.horizontalAdvance(text()),
                     fm.height() + 2 * TOGGLE_VPAD);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        static const QColor kDanger("#e01b24");
        QPainter p(this);
        p.setFont(font());
        p.setPen(isEnabled() ? kDanger
                             : palette().color(QPalette::Disabled, QPalette::WindowText));
        p.drawText(rect(), Qt::AlignCenter, text());
    }
};

// ─── SettingsGroup ───────────────────────────────────────────────────────────

static constexpr int GRP_RADIUS   = 10;
static constexpr int GRP_H_PAD    = 20;
static constexpr int GRP_V_PAD    = 12;
static constexpr int GRP_SPACING  = 28;

class SettingsGroup : public QWidget
{
public:
    explicit SettingsGroup(QList<QWidget *> items, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(GRP_H_PAD, GRP_V_PAD, GRP_H_PAD, GRP_V_PAD);
        lay->setSpacing(GRP_SPACING);
        for (QWidget *w : items)
            lay->addWidget(w);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QPalette &pal      = palette();
        const QColor   borderCol = pal.color(QPalette::Mid);

        // ── Rounded card background ───────────────────────────────────────────
        p.setPen(QPen(borderCol, 1));
        p.setBrush(pal.color(QPalette::Base));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                          GRP_RADIUS, GRP_RADIUS);

        // ── Separators between items (full card width, inside border) ─────────
        auto *lay = static_cast<QVBoxLayout *>(layout());
        const int count = lay->count();
        if (count < 2) return;

        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(pal.color(QPalette::Midlight), 1));

        for (int i = 1; i < count; ++i) {
            const QWidget *prev = lay->itemAt(i - 1)->widget();
            const QWidget *cur  = lay->itemAt(i)->widget();
            if (!prev || !cur) continue;
            const int y = (prev->geometry().bottom() + cur->geometry().top()) / 2;
            p.drawLine(1, y, width() - 2, y);
        }
    }
};

// ─── PreferencesDialog ───────────────────────────────────────────────────────

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));

    // ── Font family ──────────────────────────────────────────────────────────
    m_fontFamily = AppSettings::prefFontFamily();
    auto *m_fontBtn = new FontNavButton(this);
    if (!m_fontFamily.isEmpty())
        m_fontBtn->setSelectedFont(QFont(m_fontFamily));

    // ── Font size ────────────────────────────────────────────────────────────
    m_fontSize = new StepSpinBox(tr("Font Size"), 6, 72, 1, this);
    m_fontSize->setValue(AppSettings::prefFontSize());

    // ── Spacing ──────────────────────────────────────────────────────────────
    m_horizSpacing = new StepSpinBox(tr("Character Spacing"), 0, 20, 1, this);
    m_horizSpacing->setValue(AppSettings::prefHorizSpacing());

    m_lineSpacing = new StepSpinBox(tr("Line Spacing"), 0, 20, 1, this);
    m_lineSpacing->setValue(AppSettings::prefLineSpacing());

    // ── Native menu toggle ───────────────────────────────────────────────────
    m_nativeMenu = new SettingsToggle(tr("Native menu bar"), this);
    m_nativeMenu->setChecked(AppSettings::prefNativeMenu());

    // ── Live-save and signal on every change ─────────────────────────────────
    connect(m_fontBtn, &QAbstractButton::clicked, this, [this, m_fontBtn]() {
        m_fontFamily = m_fontBtn->selectedFont().family();
        AppSettings::setPrefFontFamily(m_fontFamily);
        emit fontChanged(QFont(m_fontFamily, m_fontSize->value()));
    });
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

    // ── Layout ───────────────────────────────────────────────────────────────
    auto makeSectionLabel = [this](const QString &text) {
        auto *lbl = new QLabel(text, this);
        QFont f = lbl->font();
        f.setBold(true);
        lbl->setFont(f);
        return lbl;
    };

    auto *fontRow = new QWidget(this);
    {
        auto *lay = new QHBoxLayout(fontRow);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(TOGGLE_SPACING);
        lay->addWidget(new QLabel(tr("Font"), fontRow));
        lay->addStretch();
        lay->addWidget(m_fontBtn);
    }

    auto *fontGroup   = new SettingsGroup({fontRow, m_fontSize, m_horizSpacing, m_lineSpacing}, this);
    auto *appearGroup = new SettingsGroup({m_nativeMenu}, this);

    // ── Theme swatches (no group border — swatches are their own visual units) ──
    m_swatchWidget = new QWidget(this);
    {
        static constexpr int kSwatchCols = 4;

        m_swatchLayout = new QGridLayout(m_swatchWidget);
        m_swatchLayout->setContentsMargins(0, 0, 0, 0);
        m_swatchLayout->setSpacing(10);
        m_swatchLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        m_swatchGroup = new QButtonGroup(m_swatchWidget);
        m_swatchGroup->setExclusive(true);

        // Embedded palettes first, then any saved custom ones
        QList<PaletteInfo> palettes = loadEmbeddedPalettes();
        palettes += loadCustomPalettes();
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
            connect(sw, &PaletteSwatch::doubleClicked, this, [this, info]() {
                const PaletteInfo before = m_currentPalette;
                PaletteEditorDialog dlg(info, this);
                connect(&dlg, &PaletteEditorDialog::paletteChanged,
                        this, &PreferencesDialog::paletteSelected);
                connect(&dlg, &PaletteEditorDialog::paletteSaved,
                        this, &PreferencesDialog::addCustomSwatch);
                if (dlg.exec() != QDialog::Accepted)
                    emit paletteSelected(before);
            });
        }

        m_addBtn = new AddPaletteSwatch(m_swatchWidget);
        connect(m_addBtn, &QAbstractButton::clicked, this, [this]() {
            const PaletteInfo before = m_currentPalette;
            PaletteEditorDialog dlg(m_currentPalette, this);
            connect(&dlg, &PaletteEditorDialog::paletteChanged,
                    this, &PreferencesDialog::paletteSelected);
            connect(&dlg, &PaletteEditorDialog::paletteSaved,
                    this, &PreferencesDialog::addCustomSwatch);
            if (dlg.exec() != QDialog::Accepted)
                emit paletteSelected(before);
        });
        m_swatchLayout->addWidget(m_addBtn, m_swatchCount / kSwatchCols,
                                             m_swatchCount % kSwatchCols);

        // Watch for external changes to the palette directory
        m_watcher = new QFileSystemWatcher(this);
        const QString paletteDir = paletteStorageDir();
        if (QDir(paletteDir).exists())
            m_watcher->addPath(paletteDir);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                this, [this](const QString &) { rebuildCustomSwatches(); });
    }

    // ── Reset to defaults ─────────────────────────────────────────────────────
    auto *resetBtn = new DangerButton(tr("Reset to defaults"), this);
    auto *resetGroup = new SettingsGroup({resetBtn}, this);

    // ── Scrollable content widget ─────────────────────────────────────────────
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
    vlay->addWidget(resetGroup);
    vlay->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);

    auto *dialogLay = new QVBoxLayout(this);
    dialogLay->setContentsMargins(0, 0, 0, 0);
    dialogLay->addWidget(scroll);

    setSizeGripEnabled(false);
    setMinimumWidth(460);
}

void PreferencesDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    const int maxH = 560;
    setFixedSize(width(), qMin(sizeHint().height(), maxH));
}

void PreferencesDialog::addCustomSwatch(const PaletteInfo &)
{
    rebuildCustomSwatches();
}

void PreferencesDialog::rebuildCustomSwatches()
{
    static constexpr int kSwatchCols = 4;
    const QString prevName = m_currentPalette.name;

    // Remove and delete all existing palette swatches
    const auto old = m_swatchGroup->buttons();
    for (auto *b : old) delete b;   // auto-removed from layout and group on deletion
    m_swatchCount = 0;
    m_swatchLayout->removeWidget(m_addBtn);

    QList<PaletteInfo> palettes = loadEmbeddedPalettes();
    palettes += loadCustomPalettes();

    for (const PaletteInfo &info : palettes) {
        auto *sw = new PaletteSwatch(info, m_swatchWidget);
        if (info.name == prevName) {
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
        connect(sw, &PaletteSwatch::doubleClicked, this, [this, info]() {
            const PaletteInfo before = m_currentPalette;
            PaletteEditorDialog dlg(info, this);
            connect(&dlg, &PaletteEditorDialog::paletteChanged,
                    this, &PreferencesDialog::paletteSelected);
            connect(&dlg, &PaletteEditorDialog::paletteSaved,
                    this, &PreferencesDialog::addCustomSwatch);
            if (dlg.exec() != QDialog::Accepted)
                emit paletteSelected(before);
        });
    }

    m_swatchLayout->addWidget(m_addBtn, m_swatchCount / kSwatchCols,
                                        m_swatchCount % kSwatchCols);

    // Start watching if the directory now exists (e.g. just created by a save)
    const QString dir = paletteStorageDir();
    if (m_watcher && QDir(dir).exists() && !m_watcher->directories().contains(dir))
        m_watcher->addPath(dir);
}

