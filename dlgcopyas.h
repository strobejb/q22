//
//  dlgcopyas.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#pragma once

#include <QDialog>
#include "dlgexport.h"

namespace Ui { class CopyAsDialog; }

class HexView;

//
//  CopyAsDlgProc analogue - modal QDialog that mirrors the original
//  Win32 IDD_COPYAS dialog.  Call CopyAsDlg() to show it.
//
class CopyAsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CopyAsDialog(HexView *hv, QWidget *parent = nullptr);
    ~CopyAsDialog();

private slots:
    void onFormatChanged(int index);
    void onAccepted();

private:
    void updateDataTypeEnabled();

    Ui::CopyAsDialog  *ui;
    HexView           *m_hv;
};

// Show the dialog and perform the copy if accepted.
// Mirrors the original CopyAsDlg(HWND hwnd) entry point.
bool CopyAsDlg(HexView *hv, QWidget *parent = nullptr);

extern IMPEXP_OPTIONS g_CopyOptions;
