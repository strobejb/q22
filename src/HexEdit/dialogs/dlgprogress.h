//
//  dlgprogress.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#pragma once

#include "exportformat.h"

#include <QDialog>
#include <QElapsedTimer>

class QProgressBar;

// ── ProgressDialog ────────────────────────────────────────────────────────────
// Generic progress dialog. Use open() to show non-blocking (window-modal).
// totalBytes drives the bar; pass 0 for an indeterminate (busy) bar.
class ProgressDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit ProgressDialog(qint64 totalBytes, const QString &title, QWidget *parent = nullptr);

  public slots:
    void onProgress(qint64 bytesConsumed);
    void onFinished(bool success);
    void setRateText(const QString &rateText);

  signals:
    void cancelRequested();

  protected:
    void keyPressEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

  private:
    QProgressBar *m_bar;
    qint64        m_total;
};

// ── SyncProgressReporter ─────────────────────────────────────────────────────
// Main-thread reporter: updates the dialog and pumps the event loop so the
// Cancel button stays responsive. Used by Export() and any similar operation
// that runs on the main thread.
class SyncProgressReporter : public ProgressReporter
{
  public:
    explicit SyncProgressReporter(ProgressDialog *dlg);
    void reportProgress(qint64 bytes) override;

    bool isCancelled() const override
    {
        return m_cancelled;
    }

  private:
    ProgressDialog *m_dlg;
    QElapsedTimer   m_timer;
    bool            m_cancelled = false;
};
