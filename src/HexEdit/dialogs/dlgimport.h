#pragma once

#include "importformat.h"

#include <QString>

class HexView;
class QWidget;

// Parse a formatted stream into hv; returns byte count written (0 on failure).
size_w Import(QIODevice *dev, HexView *hv, IMPEXP_OPTIONS *ieopt, ProgressReporter *reporter = nullptr);
size_w ImportFile(const QString &path, HexView *hv, IMPEXP_OPTIONS *ieopt, QWidget *parent = nullptr);

// Retrieve clipboard data in mimeType, optionally interpret via ieopt,
// and paste into hv at the current cursor. fMaskSel limits the write to
// the current selection size.
bool PasteSpecial(HexView *hv, bool fMaskSel, const QString &mimeType, IMPEXP_OPTIONS *ieopt);

extern IMPEXP_OPTIONS g_ImportOptions;
extern IMPEXP_OPTIONS g_PasteOptions;
