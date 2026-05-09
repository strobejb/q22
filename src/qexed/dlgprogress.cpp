//
//  dlgprogress.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgprogress.h"
#include "theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

// ── ProgressDialog ────────────────────────────────────────────────────────────

ProgressDialog::ProgressDialog(qint64 totalBytes, const QString &title, QWidget *parent)
    : QDialog(parent), m_total(totalBytes)
{
    removeDialogIcon(this);
    setWindowTitle(title);
    setSizeGripEnabled(false);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    m_bar = new QProgressBar(this);
    m_bar->setTextVisible(true);
    m_bar->setFormat("%p%");
    m_bar->setAlignment(Qt::AlignCenter);
    m_bar->setMinimumWidth(320);
    m_bar->setRange(0, totalBytes > 0 ? 10000 : 0);

    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &ProgressDialog::cancelRequested);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 16, 16, 16);
    lay->setSpacing(12);
    lay->addWidget(m_bar);
    lay->addLayout(btnRow);
}

void ProgressDialog::onProgress(qint64 bytes)
{
    if (m_total > 0)
        m_bar->setValue((int)(bytes * 10000 / m_total));
}

void ProgressDialog::setRateText(const QString &rateText)
{
    m_bar->setFormat(rateText.isEmpty() ? "%p%" : QString("%p%  (%1)").arg(rateText));
}

void ProgressDialog::onFinished(bool /*success*/)
{
    accept();
}

void ProgressDialog::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
        emit cancelRequested();
    else
        QDialog::keyPressEvent(e);
}

void ProgressDialog::closeEvent(QCloseEvent *e)
{
    emit cancelRequested();
    e->accept();
}

// ── SyncProgressReporter ─────────────────────────────────────────────────────

SyncProgressReporter::SyncProgressReporter(ProgressDialog *dlg)
    : m_dlg(dlg)
{
    QObject::connect(dlg, &ProgressDialog::cancelRequested, dlg, [this]() {
        m_cancelled = true;
    });
    m_timer.start();
}

void SyncProgressReporter::reportProgress(qint64 bytes)
{
    qint64 elapsedMs = m_timer.elapsed();
    if (elapsedMs > 0) {
        double mbps = (bytes / (1024.0 * 1024.0)) / (elapsedMs / 1000.0);
        m_dlg->setRateText(QString("%1 MB/s").arg(mbps, 0, 'f', 1));
    }
    m_dlg->onProgress(bytes);
    QApplication::processEvents();
}
