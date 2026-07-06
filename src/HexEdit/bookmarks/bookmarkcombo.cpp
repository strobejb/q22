#include "bookmarkcombo.h"

#include "HexView/hexview.h"
#include "HexView/hexviewbookmark.h"
#include "combos/datatypecombobox.h"

#include <QFontMetrics>
#include <QObject>
#include <QColor>
#include <QStringList>
#include <QVariant>
#include <algorithm>

namespace BookmarkCombo {

static QString trimmedLeadingWhitespace(QString text)
{
    qsizetype firstNonSpace = 0;
    while (firstNonSpace < text.size() && text.at(firstNonSpace).isSpace())
        ++firstNonSpace;
    if (firstNonSpace > 0)
        text.remove(0, firstNonSpace);
    return text;
}

static QColor bookmarkColor(HexView *hv, const Bookmark &bm)
{
    if (!hv)
        return {};
    return bm.colourIndex >= 0
        ? QColor(hv->getHexColour(HvColorSlot(HVC_BOOKMARK1 + bm.colourIndex)))
        : (bm.bgColour ? QColor(bm.bgColour)
                       : QColor(hv->getHexColour(HVC_BOOKMARK1)));
}

static QString markerIconName(BookmarkKind kind)
{
    switch (kind) {
    case BookmarkKind::Function:
        return QStringLiteral("chip");
    case BookmarkKind::Structure:
        return QStringLiteral("hex");
    case BookmarkKind::Basic:
        break;
    }
    return {};
}

void populate(DataTypeComboBox *combo, HexView *hv, Mode mode, bool swatches)
{
    if (!combo || !hv)
        return;

    const QList<Bookmark> &bms = hv->bookmarks();

    combo->clear();
    const bool includeNew = mode == Mode::IncludeNewBookmark;
    const QString newBmLabel = QObject::tr("New Bookmark...\tCtrl+B");
    if (includeNew)
        combo->addItem(newBmLabel);
    if (includeNew && !bms.isEmpty())
        combo->addItem(QString());

    QList<int> sortedIdx;
    sortedIdx.reserve(bms.size());
    for (int i = 0; i < bms.size(); ++i)
        sortedIdx.append(i);
    std::sort(sortedIdx.begin(), sortedIdx.end(), [&bms](int a, int b) {
        return bms[a].offset < bms[b].offset;
    });

    const QFontMetrics fm(combo->font());
    constexpr int kMaxNamePx = 200;
    int comboIdx = includeNew ? (bms.isEmpty() ? 1 : 2) : 0;

    QStringList labels;
    labels.reserve(sortedIdx.size());
    for (int i : sortedIdx) {
        const Bookmark &bm = bms[i];
        const QString displayText = trimmedLeadingWhitespace(bm.text);
        const QString rawText = displayText.isEmpty() ? QObject::tr("(empty)") : displayText;
        const QString name = fm.elidedText(rawText, Qt::ElideRight, kMaxNamePx);
        const QString hex = QStringLiteral("0x")
                            + QString::number(bm.offset, 16).toUpper().rightJustified(8, QLatin1Char('0'));
        const QString label = name + QLatin1Char('\t') + hex;
        combo->addItem(label);

        if (swatches) {
            const QColor markerColor = bookmarkColor(hv, bm);
            const QString iconName = markerIconName(bm.kind);
            combo->setItemData(comboIdx, markerColor, Qt::DecorationRole);
            if (!iconName.isEmpty())
                combo->setItemData(comboIdx, iconName, DataTypeComboBox::MenuMarkerIconNameRole);
        }
        ++comboIdx;
        labels.append(label);
    }

    combo->buildMenu(/*checkable=*/false);
    combo->setDisplayText(QObject::tr("Bookmarks..."));

    if (includeNew)
        combo->setActionData(newBmLabel, QVariant::fromValue<int>(-1));
    for (int j = 0; j < sortedIdx.size(); ++j)
        combo->setActionData(labels[j], QVariant::fromValue<int>(sortedIdx[j]));

    combo->setEnabled(includeNew || !bms.isEmpty());
}

} // namespace BookmarkCombo
