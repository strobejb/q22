#include "bookmarkpopup.h"

#include "HexView/hexview.h"
#include "bookmarkcolourwidget.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

// ─── BookmarkMenuPositioner ───────────────────────────────────────────────────
// Event filter installed on the popup QMenu; positions it right-aligned under
// the gear button whose global rect is supplied at construction time.

namespace {

class BookmarkMenuPositioner : public QObject {
    QRect m_btn;
public:
    BookmarkMenuPositioner(QRect btnGlobal, QObject *parent)
        : QObject(parent), m_btn(btnGlobal) {}

    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (e->type() == QEvent::Show) {
            auto *w = static_cast<QWidget *>(obj);
            // Identical to the titlebar formula:
            // btn->mapToGlobal(QPoint(btn->width() - menu->width() + offset, btn->height()))
            w->move(m_btn.left() + m_btn.width() - w->width() + themedMenuRightAlignOffset(),
                    m_btn.top()  + m_btn.height());
        }
        return false;
    }
};

} // namespace

// ─── showBookmarkContextPopup ────────────────────────────────────────────────

void showBookmarkContextPopup(HexView *hv, int idx, QRect btnGlobal)
{
    // Toggle: if this bookmark's popup is already open, the deferred clear
    // hasn't fired yet — the user clicked the gear button a second time to
    // close it.  Qt has already dismissed the popup; just return.
    if (hv->bookmarkPopupIdx() == idx) return;

    const QList<Bookmark> &bms = hv->bookmarks();
    if (idx < 0 || idx >= bms.size()) return;
    const Bookmark &bm = bms[idx];

    // Build swatches from the first 5 palette-indexed bookmark colours.
    QVector<QColor> swatches;
    for (int i = 0; i < MAX_BOOKMARK_COLORS; ++i)
        swatches.append(QColor(hv->getHexColour(HvColorSlot(HVC_BOOKMARK1 + i))));

    // Standard themed QMenu — background, shadow, separator come for free.
    auto *menu = new QMenu(nullptr);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    themeMenu(menu);

    // ── Colour picker (QWidgetAction) ─────────────────────────────────────────
    {
        // Wrap in a transparent container so the QMenu background shows through.
        auto *container = new QWidget;
        container->setAutoFillBackground(false);
        auto *lay = new QVBoxLayout(container);
        // 11px left/right: 8px base + 3px extra padding requested.
        // minimum width: swatch default (5×38 + 8 = 198px) + margins (22px)
        // + 32px extra requested = 252px.
        lay->setContentsMargins(11, 8, 11, 6);
        lay->setSpacing(0);
        container->setMinimumWidth(252);

        auto *cw = new BookmarkColourWidget(container);
        cw->setColumns(MAX_BOOKMARK_COLORS);
        cw->setColours(swatches);
        if (bm.colourIndex >= 0 && bm.colourIndex < swatches.size())
            cw->setSelectedColour(swatches[bm.colourIndex]);
        lay->addWidget(cw);

        // QMenu calls sizeHint() on widget actions before they have a real
        // width (width()==0), so the height estimate is based on the default
        // cell size and comes up short.  Pre-compute the correct height at
        // the known content width and pin it as the container's minimum height
        // so the menu allocates exactly the right item slot.
        {
            const auto &cm = lay->contentsMargins();
            const int contentW = container->minimumWidth() - cm.left() - cm.right();
            const int contentH = cw->heightForWidth(contentW);
            container->setMinimumHeight(contentH + cm.top() + cm.bottom());
        }

        auto *act = new QWidgetAction(menu);
        act->setDefaultWidget(container);
        menu->addAction(act);

        QObject::connect(cw, &BookmarkColourWidget::colourSelected, menu,
                [hv, idx, cw, menu](const QColor &) {
            const QList<Bookmark> &bms2 = hv->bookmarks();
            if (idx >= 0 && idx < bms2.size()) {
                Bookmark updated = bms2[idx];
                updated.colourIndex = cw->selectedIndex();
                hv->replaceBookmark(idx, updated);
            }
            menu->close();
        });
    }

    menu->addSeparator();

    // ── Copy / Delete button row (QWidgetAction) ──────────────────────────────
    {
        auto *container = new QWidget;
        container->setAutoFillBackground(false);
        auto *hlay = new QHBoxLayout(container);
        hlay->setContentsMargins(7, 2, 7, 2);
        hlay->setSpacing(2);

        auto makeBtn = [container](const QString &label, const QString &iconName) {
            auto *btn = new QPushButton(label, container);
            btn->setFlat(true);
            // palette(button) = neutral grey, matches QMenu item hover colour.
            btn->setStyleSheet(
                QStringLiteral("QPushButton {"
                "  border: none; border-radius: 5px; background: transparent;"
                "  padding: 8px 10px; }"
                "QPushButton:hover   { background: palette(button); }"
                "QPushButton:pressed { background: palette(mid); }"));

            // Qt's CE_PushButtonLabel hardcodes a 4px icon-text gap and ignores
            // QSS spacing.  The only reliable workaround: render the icon onto a
            // wider transparent canvas so the gap is baked into the pixmap itself.
            constexpr int kIconSz = 16;
            constexpr int kGap    = 14;
            const QColor fg = btn->palette().buttonText().color();
            const QPixmap src = recoloredIcon(iconName, fg, kIconSz).pixmap(kIconSz, kIconSz);
            if (!src.isNull()) {
                const qreal dpr = src.devicePixelRatio();
                QPixmap padded(qRound((kIconSz + kGap) * dpr), qRound(kIconSz * dpr));
                padded.setDevicePixelRatio(dpr);
                padded.fill(Qt::transparent);
                QPainter pp(&padded);
                pp.drawPixmap(0, 0, src);
                btn->setIcon(QIcon(padded));
                btn->setIconSize(QSize(kIconSz + kGap, kIconSz));
            }

            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            return btn;
        };

        auto *copyBtn   = makeBtn(QObject::tr("Copy"),   QStringLiteral("actions/edit-copy-symbolic"));
        auto *deleteBtn = makeBtn(QObject::tr("Delete"), QStringLiteral("actions/user-trash-symbolic"));
        hlay->addWidget(copyBtn);
        hlay->addWidget(deleteBtn);

        QObject::connect(copyBtn, &QPushButton::clicked, menu, [hv, idx, menu]() {
            const QList<Bookmark> &bms2 = hv->bookmarks();
            if (idx >= 0 && idx < bms2.size())
                QApplication::clipboard()->setText(bms2[idx].text);
            menu->close();
        });
        QObject::connect(deleteBtn, &QPushButton::clicked, menu, [hv, idx, menu]() {
            hv->removeBookmark(idx);
            menu->close();
        });

        auto *act = new QWidgetAction(menu);
        act->setDefaultWidget(container);
        menu->addAction(act);
    }

    // Right-align under the gear button — identical to the titlebar pattern.
    menu->installEventFilter(new BookmarkMenuPositioner(btnGlobal, menu));

    // Keep the gear button visually pressed while the popup is open.
    // Defer the clear so that bookmarkPopupIdx() is still set when the
    // mouse-release fires after the user clicks the button again (toggle).
    hv->setBookmarkPopupIdx(idx);
    QObject::connect(menu, &QMenu::aboutToHide, hv, [hv]() {
        QTimer::singleShot(0, hv, [hv]() { hv->setBookmarkPopupIdx(-1); });
    });

    menu->popup({0, 0});   // Show event filter corrects the position
}
