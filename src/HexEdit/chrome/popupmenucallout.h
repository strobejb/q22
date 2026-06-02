#ifndef POPUPMENUCALLOUT_H
#define POPUPMENUCALLOUT_H

#include <QRect>

class QMenu;

void installPopupMenuCallout(QMenu *menu, QRect anchorGlobal);
void installPopupMenuRightAligned(QMenu *menu, QRect anchorGlobal);

#endif // POPUPMENUCALLOUT_H
