#include "bookmarkdialog.h"
#include "ui_bookmarks.h"
#include "colourpickerwidget.h"

#include <QApplication>

BookmarkDialog::BookmarkDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::BookmarkDialog)
    , m_foreground(QApplication::palette().text().color())
{
    ui->setupUi(this);
    connect(ui->pushButton,   &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->pushButton_2, &QPushButton::clicked, this, &QDialog::reject);
}

BookmarkDialog::~BookmarkDialog()
{
    delete ui;
}

void BookmarkDialog::setOffset(quint64 offset)
{
    m_offset = offset;
    ui->lineEdit->setText(QString::number(offset, 16).toUpper());
}


void BookmarkDialog::setLength(quint64 length)
{
    m_length = length;
    ui->spinBox->setValue(static_cast<int>(length));
}

void BookmarkDialog::setForegroundColour(const QColor &fg)
{
    m_foreground = fg;
    ui->colourPicker->setForegroundColour(fg);
}

QString BookmarkDialog::bookmarkName() const
{
    return ui->bookmarkName->text();
}

QColor BookmarkDialog::selectedColour() const
{
    return ui->colourPicker->selectedColour();
}
