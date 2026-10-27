#ifndef STRUCTVIEW_STRATAROOT_H
#define STRUCTVIEW_STRATAROOT_H

#include "structview/structuredefinitionmanager.h"

#include <QList>
#include <QString>

#include <cstdint>
#include <functional>

// Same short-read contract as HexView::getData(): returns how many bytes were
// actually read into `buf`.
using StrataRootByteReader = std::function<size_t(uint64_t offset, uint8_t *buf, size_t len)>;

// Picks the exported Strata root for a file: exact extension association
// first, then compound extensions (.tar.gz), then magic signatures. Structure
// View, code discovery and the file map all select roots through this, so
// background consumers agree with what the user sees in the panel. Ties go to
// the earlier entry in `exportedTypes`. Returns an index, or -1.
int associatedStrataRootIndex(const QList<ExportedStructureType> &exportedTypes,
                              const QString &fileName,
                              const StrataRootByteReader &reader);

TypeDecl *associatedStrataRoot(const QList<ExportedStructureType> &exportedTypes,
                               const QString &fileName,
                               const StrataRootByteReader &reader);

// The extension half of the above, for callers that check magic themselves
// (nested sources, which match magic first and fall back to the name).
int strataRootIndexForFileName(const QList<ExportedStructureType> &exportedTypes,
                               const QString &fileName);

// Constant-folded [offset(...)] on the root typedef. Root offsets are a
// file-placement hint and must be constant; data-dependent offsets are the
// render engine's business. False when there is no constant offset tag; a
// negative offset folds to 0.
bool strataRootExplicitOffset(TypeDecl *rootType, uint64_t *offset);

// strataRootExplicitOffset(), or 0.
uint64_t strataRootBaseOffset(TypeDecl *rootType);

#endif // STRUCTVIEW_STRATAROOT_H
