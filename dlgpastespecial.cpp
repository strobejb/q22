//
//  dlgpastespecial.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgpastespecial.h"
#include "ui_pastespecial.h"
#include "HexView/hexview.h"

#include <QAbstractButton>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QListWidgetItem>
#include <QMimeData>

// ── Persistent state (mirrors the static locals in PasteDlgProc) ──────────────

static QString s_lastMimeType;
static int     s_lastTransform  = 0;   // combo index
static bool    s_lastInterpret  = false;
static bool    s_lastMask       = false;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Formats whose content can be parsed as text (and thus transformed)
static bool mimeTypeIsText(const QString &mime)
{
    return mime.startsWith(QLatin1String("text/")) ||
           mime == QLatin1String("application/x-qiconlist");
}

// combo index 0 → FORMAT_HEXDUMP (1), 1 → FORMAT_RAWHEX (2), …
static IMPEXP_FORMAT comboIndexToFormat(int idx)
{
    return (IMPEXP_FORMAT)(idx + (int)FORMAT_HEXDUMP);
}

static int formatToComboIndex(IMPEXP_FORMAT fmt)
{
    int i = (int)fmt - (int)FORMAT_HEXDUMP;
    return (i < 0) ? 0 : i;
}

static bool formatNeedsAddress(IMPEXP_FORMAT fmt)
{
    return fmt == FORMAT_HEXDUMP || fmt == FORMAT_INTELHEX || fmt == FORMAT_SRECORD;
}

static bool formatNeedsEndian(IMPEXP_FORMAT fmt)
{
    return fmt == FORMAT_CPP || fmt == FORMAT_ASM;
}

// ── PasteSpecialDialog ────────────────────────────────────────────────────────

PasteSpecialDialog::PasteSpecialDialog(HexView *hv, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PasteSpecialDialog)
    , m_hv(hv)
{
    ui->setupUi(this);
    removeDialogIcon(this);

    // List: item padding + lock border/outline across all states so Adwaita's
    // PE_Frame hover-highlight colour can't bleed as a ghost pixel onto the
    // checkbox immediately below.
    {
        const int vPad = qMax(4, ui->listClipFormats->fontMetrics().height() / 2);
        const bool dark = qApp->palette().window().color().lightness() < 128;
        const QString border = dark ? QLatin1String("rgba(255,255,255,0.18)")
                                    : QLatin1String("rgba(0,0,0,0.15)");
        ui->listClipFormats->setStyleSheet(QString(
            "QListWidget        { border: 1px solid %1; outline: 0; }"
            "QListWidget:hover  { border: 1px solid %1; }"
            "QListWidget:focus  { border: 1px solid %1; }"
            "QListWidget::item  { padding: %2px 4px; }"
        ).arg(border).arg(vPad));
    }

    // Strip button icons
    for (QAbstractButton *btn : ui->buttonBox->buttons())
        btn->setIcon(QIcon());

    // Rename OK → "Paste"
    if (QPushButton *ok = ui->buttonBox->button(QDialogButtonBox::Ok))
        ok->setText(tr("Paste"));


    // Populate clipboard formats
    populateClipboardFormats();

    // Restore persistent state
    ui->checkInterpret->setChecked(s_lastInterpret);
    ui->comboFormat->setCurrentIndex(s_lastTransform);
    ui->checkBigEndian->setChecked(g_PasteOptions.fBigEndian);
    ui->checkUseAddress->setChecked(g_PasteOptions.fUseAddress);

    // "Mask using selection" is only relevant when there is an active selection
    const bool hasSel = hv->selectionSize() > 0;
    ui->checkMaskSel->setEnabled(hasSel);
    ui->checkMaskSel->setChecked(hasSel && s_lastMask);

    updateTransformControls();

    // Connections
    connect(ui->listClipFormats, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *, QListWidgetItem *) { onClipFormatChanged(); });
    connect(ui->listClipFormats, &QListWidget::itemDoubleClicked,
            this, &PasteSpecialDialog::onItemDoubleClicked);
    connect(ui->checkInterpret,  &QCheckBox::toggled,
            this, &PasteSpecialDialog::onInterpretToggled);
    connect(ui->comboFormat, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PasteSpecialDialog::onFormatChanged);
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &PasteSpecialDialog::onAccepted);

    setFixedSize(sizeHint());
}

PasteSpecialDialog::~PasteSpecialDialog()
{
    delete ui;
}

void PasteSpecialDialog::populateClipboardFormats()
{
    ui->listClipFormats->clear();

    const QMimeData *md = QApplication::clipboard()->mimeData();
    if (!md) return;

    const QStringList formats = md->formats();
    int selectRow = 0;

    for (const QString &fmt : formats) {
        QListWidgetItem *item = new QListWidgetItem(fmt, ui->listClipFormats);
        item->setData(Qt::UserRole, fmt);

        // Pre-select last used format, or the first text format we find
        if (!s_lastMimeType.isEmpty() && fmt == s_lastMimeType)
            selectRow = ui->listClipFormats->row(item);
        else if (s_lastMimeType.isEmpty() && mimeTypeIsText(fmt) && selectRow == 0)
            selectRow = ui->listClipFormats->row(item);
    }

    if (ui->listClipFormats->count() > 0)
        ui->listClipFormats->setCurrentRow(selectRow);
}

void PasteSpecialDialog::onClipFormatChanged()
{
    updateTransformControls();
}

void PasteSpecialDialog::onInterpretToggled(bool /*checked*/)
{
    updateTransformControls();
}

void PasteSpecialDialog::onFormatChanged(int /*index*/)
{
    updateTransformControls();
}

void PasteSpecialDialog::updateTransformControls()
{
    const QString mime = selectedMimeType();
    const bool isText  = mimeTypeIsText(mime);

    // "Interpret as" checkbox only makes sense for text MIME types
    ui->checkInterpret->setEnabled(isText);
    if (!isText)
        ui->checkInterpret->setChecked(false);

    const bool interpret = isText && ui->checkInterpret->isChecked();
    const IMPEXP_FORMAT fmt = comboIndexToFormat(ui->comboFormat->currentIndex());

    ui->comboFormat->setEnabled(interpret);
    ui->checkUseAddress->setEnabled(interpret && formatNeedsAddress(fmt));
    ui->checkBigEndian->setEnabled(interpret && formatNeedsEndian(fmt));

    // Force "Text (hex dump)" when interpret is off or format was just locked
    if (!interpret)
        ui->comboFormat->setCurrentIndex(0);

    // If a non-text format is selected, force the interpret combo to "Text (hex dump)"
    // (mirrors the original: selecting a non-CF_TEXT format forces CB_SETCURSEL 0)
    if (!isText)
        ui->comboFormat->setCurrentIndex(0);
}

QString PasteSpecialDialog::selectedMimeType() const
{
    QListWidgetItem *item = ui->listClipFormats->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void PasteSpecialDialog::onItemDoubleClicked(QListWidgetItem * /*item*/)
{
    onAccepted();
}

void PasteSpecialDialog::onAccepted()
{
    const QString mime = selectedMimeType();
    if (mime.isEmpty()) { reject(); return; }

    // Persist state
    s_lastMimeType   = mime;
    s_lastTransform  = ui->comboFormat->currentIndex();
    s_lastInterpret  = ui->checkInterpret->isChecked();
    s_lastMask       = ui->checkMaskSel->isChecked();

    g_PasteOptions.fBigEndian  = ui->checkBigEndian->isChecked();
    g_PasteOptions.fUseAddress = ui->checkUseAddress->isChecked();

    if (s_lastInterpret && ui->checkInterpret->isEnabled())
        g_PasteOptions.format = comboIndexToFormat(s_lastTransform);
    else
        g_PasteOptions.format = FORMAT_RAWDATA;

    PasteSpecial(m_hv, s_lastMask, mime, &g_PasteOptions);

    accept();
}

// ── Entry point ───────────────────────────────────────────────────────────────

bool HexPasteSpecialDlg(HexView *hv, QWidget *parent)
{
    PasteSpecialDialog dlg(hv, parent);
    return dlg.exec() == QDialog::Accepted;
}
