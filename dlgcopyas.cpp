//
//  dlgcopyas.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgcopyas.h"
#include "ui_copyas.h"
#include "HexView/hexview.h"
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QGraphicsOpacityEffect>

// ── Globals ──────────────────────────────────────────────────────────────────

// default to 'plain text' hex dump
IMPEXP_OPTIONS g_CopyOptions = { FORMAT_HEXDUMP, SEARCHTYPE_BYTE };

// ── Helpers ───────────────────────────────────────────────────────────────────
//
// The combo box lists formats starting at FORMAT_HEXDUMP (index 0 == FORMAT_HEXDUMP==1)
// since raw binary is not meaningful for clipboard copy.
// comboIndex 0  → FORMAT_HEXDUMP (1)
// comboIndex 1  → FORMAT_RAWHEX  (2)
// ...
// comboIndex 8  → FORMAT_UUENCODE (9)

static int comboIndexToFormat(int idx)
{
    return idx + FORMAT_HEXDUMP;   // skip FORMAT_RAWDATA=0
}

static int formatToComboIndex(IMPEXP_FORMAT fmt)
{
    int idx = (int)fmt - (int)FORMAT_HEXDUMP;
    return (idx < 0) ? 0 : idx;
}

// Data type combo maps 0→SEARCHTYPE_BYTE, 1→SEARCHTYPE_WORD, ...
static SEARCHTYPE comboIndexToSearchType(int idx)
{
    return (SEARCHTYPE)(idx + (int)SEARCHTYPE_BYTE);
}

static int searchTypeToComboIndex(SEARCHTYPE st)
{
    int idx = (int)st - (int)SEARCHTYPE_BYTE;
    return (idx < 0) ? 0 : idx;
}

// Only CPP and ASM formats expose a meaningful data-type / endian selection.
static bool formatNeedsDataType(IMPEXP_FORMAT fmt)
{
    return fmt == FORMAT_CPP || fmt == FORMAT_ASM;
}

// ── CopyAsDialog ─────────────────────────────────────────────────────────────

CopyAsDialog::CopyAsDialog(HexView *hv, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CopyAsDialog)
    , m_hv(hv)
{
    ui->setupUi(this);

    // Add vertical padding to combo boxes by bumping their minimum height.
    // Stylesheet padding is unreliable with native platform styles (Adwaita etc.),
    // so we use the same technique as DataTypeComboBox::minimumSizeHint().
    const int kPad = qMax(1, qRound(qApp->devicePixelRatio() * 2.0));
    for (QComboBox *cb : {ui->comboFormat, ui->comboDataType})
        cb->setMinimumHeight(cb->sizeHint().height() + 2 * kPad);

    // Apply an opacity effect to the data-type row so the disabled state is
    // clearly visible regardless of the platform theme.
    for (QWidget *w : {(QWidget *)ui->labelDataType,
                       (QWidget *)ui->comboDataType,
                       (QWidget *)ui->checkBigEndian})
        w->setGraphicsEffect(new QGraphicsOpacityEffect(w));

    // Strip any platform-supplied icons from OK / Cancel so the buttons stay clean.
    for (QAbstractButton *btn : ui->buttonBox->buttons())
        btn->setIcon(QIcon());

    // Initialise controls from the persistent options
    ui->comboFormat->setCurrentIndex(formatToComboIndex(g_CopyOptions.format));
    ui->comboDataType->setCurrentIndex(searchTypeToComboIndex(g_CopyOptions.basetype));
    ui->checkBigEndian->setChecked(g_CopyOptions.fBigEndian);

    updateDataTypeEnabled();

    connect(ui->comboFormat,  qOverload<int>(&QComboBox::currentIndexChanged),
            this, &CopyAsDialog::onFormatChanged);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &CopyAsDialog::onAccepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setFixedSize(qMax(sizeHint().width(), 440), sizeHint().height());
}

CopyAsDialog::~CopyAsDialog()
{
    delete ui;
}

void CopyAsDialog::onFormatChanged(int /*index*/)
{
    updateDataTypeEnabled();
}

void CopyAsDialog::updateDataTypeEnabled()
{
    IMPEXP_FORMAT fmt = (IMPEXP_FORMAT)comboIndexToFormat(ui->comboFormat->currentIndex());
    bool enable = formatNeedsDataType(fmt);
    ui->labelDataType->setEnabled(enable);
    ui->comboDataType->setEnabled(enable);
    ui->checkBigEndian->setEnabled(enable);

    const qreal opacity = enable ? 1.0 : 0.35;
    for (QWidget *w : {(QWidget *)ui->labelDataType,
                       (QWidget *)ui->comboDataType,
                       (QWidget *)ui->checkBigEndian})
        static_cast<QGraphicsOpacityEffect *>(w->graphicsEffect())->setOpacity(opacity);
}

void CopyAsDialog::onAccepted()
{
    g_CopyOptions.format     = (IMPEXP_FORMAT)comboIndexToFormat(ui->comboFormat->currentIndex());
    g_CopyOptions.basetype   = comboIndexToSearchType(ui->comboDataType->currentIndex());
    g_CopyOptions.fBigEndian = ui->checkBigEndian->isChecked();

    CopyAs(m_hv, &g_CopyOptions);

    accept();
}

// ── Entry point ───────────────────────────────────────────────────────────────

bool CopyAsDlg(HexView *hv, QWidget *parent)
{
    CopyAsDialog dlg(hv, parent);
    return dlg.exec() == QDialog::Accepted;
}
