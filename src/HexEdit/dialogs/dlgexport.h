#pragma once

#include "dlgprogress.h"
#include "exportformat.h"

#include <QString>

class HexView;
class QWidget;

// Export data from hv to a file. parent is used to centre the progress dialog.
bool Export(const QString &szFileName, HexView *hv, IMPEXP_OPTIONS *eopt, QWidget *parent = nullptr);

// Export data from hv into the clipboard using eopt's format.
bool CopyAs(HexView *hv, IMPEXP_OPTIONS *eopt, QWidget *parent = nullptr);

extern IMPEXP_OPTIONS g_ExportOptions;
