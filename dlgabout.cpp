//
//  dlgabout.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgabout.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

// ── AboutCard ─────────────────────────────────────────────────────────────────

namespace {

static constexpr int AC_SHADOW = 4;
static constexpr int AC_RADIUS = 10;

class AboutCard : public QWidget
{
public:
    explicit AboutCard(QList<QWidget *> rows, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setContentsMargins(AC_SHADOW, AC_SHADOW, AC_SHADOW, AC_SHADOW);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        for (QWidget *w : rows)
            lay->addWidget(w);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const bool   dark = palette().color(QPalette::Window).lightness() < 128;
        const QRectF card = QRectF(rect()).adjusted(AC_SHADOW, AC_SHADOW,
                                                    -AC_SHADOW, -AC_SHADOW);
        p.setPen(Qt::NoPen);
        for (int i = AC_SHADOW; i >= 1; --i) {
            const int alpha = qRound(7.0 * qreal(AC_SHADOW - i + 1) / AC_SHADOW);
            p.setBrush(QColor(0, 0, 0, dark ? alpha / 2 : alpha));
            const qreal r = AC_RADIUS + i * 0.4;
            p.drawRoundedRect(card.adjusted(-i, -(i - 1), i, i), r, r);
        }
        const QColor border = dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 18);
        p.setPen(QPen(border, 1));
        p.setBrush(palette().color(QPalette::Base));
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), AC_RADIUS, AC_RADIUS);
    }
};

// ── AboutLinkRow ──────────────────────────────────────────────────────────────

class AboutLinkRow : public QAbstractButton
{
public:
    AboutLinkRow(const QString &label, const QUrl &url, QWidget *parent = nullptr)
        : QAbstractButton(parent), m_url(url)
    {
        setText(label);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(this, &QAbstractButton::clicked,
                this, [this] { QDesktopServices::openUrl(m_url); });
    }

    QSize sizeHint() const override
    {
        return QSize(200, fontMetrics().height() + 28);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette &pal = palette();

        if (underMouse()) {
            const bool dark = pal.color(QPalette::Window).lightness() < 128;
            p.fillRect(rect(), dark ? QColor(255, 255, 255, 15) : QColor(0, 0, 0, 8));
        }

        p.setPen(pal.color(QPalette::WindowText));
        p.setFont(font());
        p.drawText(rect().adjusted(16, 0, -30, 0), Qt::AlignLeft | Qt::AlignVCenter, text());

        // External-link icon: open box (3 sides) with arrow escaping top-right.
        const int sz = 10;
        const QRect ir(rect().right() - 14 - sz,
                       (rect().height() - sz) / 2,
                       sz, sz);
        p.setPen(QPen(pal.color(QPalette::Mid), 1.3,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        // Three-sided box open at top-right
        QPainterPath box;
        box.moveTo(ir.left() + 4, ir.top());
        box.lineTo(ir.left(),     ir.top());
        box.lineTo(ir.left(),     ir.bottom());
        box.lineTo(ir.right(),    ir.bottom());
        box.lineTo(ir.right(),    ir.top() + 4);
        p.drawPath(box);
        // Arrow shaft + head pointing top-right
        p.drawLine(ir.left() + 4, ir.bottom() - 4, ir.right(), ir.top());
        p.drawLine(ir.right(),     ir.top(),     ir.right() - 3, ir.top());
        p.drawLine(ir.right(),     ir.top(),     ir.right(),     ir.top() + 3);
    }

    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent       *e) override { update(); QAbstractButton::leaveEvent(e); }

private:
    QUrl m_url;
};

} // namespace

// ── ShowAboutDlg ──────────────────────────────────────────────────────────────

void ShowAboutDlg(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("About"));
    dlg.setSizeGripEnabled(false);

    // ── Icon (192×192) ────────────────────────────────────────────────────────
    auto *iconLabel = new QLabel(&dlg);
    QPixmap px(QLatin1String(":/qexed.png"));
    iconLabel->setPixmap(px.scaled(192, 192, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(192, 192);
    iconLabel->setAlignment(Qt::AlignCenter);

    // ── App name (2× base font size, bold) ────────────────────────────────────
    auto *nameLabel = new QLabel(QLatin1String("Catch22 HexEdit"), &dlg);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() * 1.5);
    nameLabel->setFont(nameFont);
    nameLabel->setAlignment(Qt::AlignCenter);

    // ── Author ───────────────────────────────────────────────────────────────
    auto *authorLabel = new QLabel(QLatin1String("James Brown"), &dlg);
    authorLabel->setAlignment(Qt::AlignCenter);

    // ── Version ───────────────────────────────────────────────────────────────
    auto *verLabel = new QLabel(
        QObject::tr("1.0").arg(QApplication::applicationVersion()), &dlg);
    verLabel->setAlignment(Qt::AlignCenter);

    // ── Website group ─────────────────────────────────────────────────────────
    auto *websiteRow   = new AboutLinkRow(QObject::tr("Website"),
                                          QUrl(QLatin1String("https://www.catch22.net")),
                                          &dlg);
    auto *websiteGroup = new AboutCard({websiteRow}, &dlg);

    // ── Main layout ───────────────────────────────────────────────────────────
    auto *main = new QVBoxLayout(&dlg);
    main->setContentsMargins(8, 8, 8, 8);
    main->setSpacing(0);
    main->addWidget(iconLabel,    0, Qt::AlignHCenter);
    main->addSpacing(0);
    main->addWidget(nameLabel);
    main->addSpacing(6);
    main->addWidget(authorLabel);
    main->addSpacing(18);
    main->addWidget(verLabel);
    main->addSpacing(24);
    main->addWidget(websiteGroup);
    main->addSpacing(6);

    // Fix size: 150% of natural width so the dialog reads wider than its content.
    // setFixedSize also suppresses the resize cursor on all platforms.
    main->activate();
    dlg.setFixedSize(qRound(dlg.sizeHint().width() * 1.5), dlg.sizeHint().height());

    dlg.exec();
}
