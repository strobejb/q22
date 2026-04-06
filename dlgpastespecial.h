//
//  dlgpastespecial.h
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#pragma once

#include <QDialog>
#include "dlgimport.h"

namespace Ui { class PasteSpecialDialog; }

class HexView;
class QGraphicsOpacityEffect;
class QListWidgetItem;

//
//  PasteDlgProc analogue – modal QDialog that mirrors IDD_PASTESPECIAL.
//
class PasteSpecialDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PasteSpecialDialog(HexView *hv, QWidget *parent = nullptr);
    ~PasteSpecialDialog();

private slots:
    void onClipFormatChanged();
    void onInterpretToggled(bool checked);
    void onFormatChanged(int index);
    void onAccepted();
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    void populateClipboardFormats();
    void updateTransformControls();

    QString selectedMimeType() const;

    Ui::PasteSpecialDialog *ui;
    HexView                *m_hv;
};

// Show the dialog. Mirrors HexPasteSpecialDlg(HWND hwnd).
bool HexPasteSpecialDlg(HexView *hv, QWidget *parent = nullptr);
