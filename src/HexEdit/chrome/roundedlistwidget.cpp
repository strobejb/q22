#include "chrome/roundedlistwidget.h"

namespace {
static constexpr int kRadius = 6;
static constexpr int kViewportInset = 4;
}

RoundedListWidget::RoundedListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_StyledBackground, false);
    setStyleSheet(QStringLiteral(R"(
        RoundedListWidget {
            border: 1px solid palette(mid);
            border-radius: %1px;
            background: palette(base);
            outline: 0;
        }
        RoundedListWidget::item {
            background: transparent;
        }
    )").arg(kRadius));
    configureViewport();
}

void RoundedListWidget::configureViewport()
{
    setViewportMargins(kViewportInset, kViewportInset, kViewportInset, kViewportInset);
    if (viewport()) {
        viewport()->setAutoFillBackground(false);
        viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    }
}
