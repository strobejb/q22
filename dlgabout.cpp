//
//  dlgabout.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgabout.h"
#include "theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

static constexpr int CHEV_SIZE = 5;   // half-height of chevron arms

// ── AboutCard ─────────────────────────────────────────────────────────────────

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
        for (int i = 0; i < rows.size(); ++i) {
            lay->addWidget(rows[i]);
            if (i < rows.size() - 1)
                lay->addWidget(new Hairline(this));
        }
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
#ifdef Q_OS_WIN
        m_icon = QIcon(QLatin1String(":/icons/hicolor/scalable/actions/external-link-symbolic.svg"));
#else
        m_icon = recoloredIcon(QLatin1String("external-link-symbolic"),
                               QApplication::palette().color(QPalette::WindowText), 12);
#endif
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

        if (!m_icon.isNull()) {
            const int sz = 12;
            const QRect ir(rect().right() - 14 - sz,
                           (rect().height() - sz) / 2,
                           sz, sz);
            m_icon.paint(&p, ir);
        }
    }

    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent       *e) override { update(); QAbstractButton::leaveEvent(e); }

private:
    QUrl  m_url;
    QIcon m_icon;
};


// ── AboutNavRow ───────────────────────────────────────────────────────────────
// Navigation row: label on the left, painted chevron on the right.

class AboutNavRow : public QAbstractButton
{
public:
    explicit AboutNavRow(const QString &label, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setText(label);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
        p.drawText(rect().adjusted(16, 0, -(CHEV_SIZE * 2 + 16), 0),
                   Qt::AlignLeft | Qt::AlignVCenter, text());

        const int cx = rect().right() - CHEV_SIZE - 10;
        const int cy = rect().height() / 2;
        p.setPen(QPen(pal.color(QPalette::WindowText), 1.5,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath ch;
        ch.moveTo(cx,              cy - CHEV_SIZE);
        ch.lineTo(cx + CHEV_SIZE,  cy);
        ch.lineTo(cx,              cy + CHEV_SIZE);
        p.drawPath(ch);
    }

    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent       *e) override { update(); QAbstractButton::leaveEvent(e); }
};

// ── AboutTextRow ──────────────────────────────────────────────────────────────
// Non-interactive row showing a plain text value, inset to match link rows.

class AboutTextRow : public QWidget
{
public:
    explicit AboutTextRow(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return QSize(200, fontMetrics().height() + 20);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        p.setFont(font());
        p.drawText(rect().adjusted(16, 0, -16, 0), Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }

private:
    QString m_text;
};

// ── showCreditsDlg ────────────────────────────────────────────────────────────

static void showCreditsDlg(QWidget *parent, QSize size)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Credits"));
    dlg.setSizeGripEnabled(false);
    dlg.setFixedSize(size);

    auto *codeGroup  = new AboutCard({ new AboutTextRow(QLatin1String("James Brown"), &dlg) }, &dlg);
    auto *iconsGroup = new AboutCard({ new AboutTextRow(QLatin1String("GNOME Project"), &dlg) }, &dlg);

    auto *codeHeader  = new QLabel(QObject::tr("Code by"),  &dlg);
    auto *iconsHeader = new QLabel(QObject::tr("Icons by"), &dlg);
    for (auto *h : { codeHeader, iconsHeader }) {
        QFont f = h->font();
        f.setBold(true);
        h->setFont(f);
    }

    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(0);
    lay->addStretch();
    lay->addWidget(codeHeader);
    lay->addSpacing(4);
    lay->addWidget(codeGroup);
    lay->addSpacing(16);
    lay->addWidget(iconsHeader);
    lay->addSpacing(4);
    lay->addWidget(iconsGroup);
    lay->addStretch();

    dlg.exec();
}

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
    auto *verLabel = new QLabel(QApplication::applicationVersion(), &dlg);
    verLabel->setAttribute(Qt::WA_StyledBackground, true);
    verLabel->setAlignment(Qt::AlignCenter);
    {
        const int r = (QFontMetrics(verLabel->font()).height() + 8) / 2; // 8 = 2×4px padding
        verLabel->setStyleSheet(
            QString("QLabel { padding: 4px 10px; border-radius: %1px;"
                    " background-color: black; color: white; }").arg(r));
    }

    // ── Links / navigation group ──────────────────────────────────────────────
    auto *websiteRow  = new AboutLinkRow(QObject::tr("Website"),
                                         QUrl(QLatin1String("https://www.catch22.net")),
                                         &dlg);
    auto *creditsRow  = new AboutNavRow(QObject::tr("Credits"), &dlg);
    auto *websiteGroup = new AboutCard({websiteRow, creditsRow}, &dlg);

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
    main->addWidget(verLabel, 0, Qt::AlignHCenter);
    main->addSpacing(24);
    main->addWidget(websiteGroup);
    main->addSpacing(6);

    // Fix size: 150% of natural width so the dialog reads wider than its content.
    // setFixedSize also suppresses the resize cursor on all platforms.
    main->activate();
    dlg.setFixedSize(qRound(dlg.sizeHint().width() * 1.5), dlg.sizeHint().height());

    QObject::connect(creditsRow, &QAbstractButton::clicked, &dlg,
                     [&dlg] { showCreditsDlg(&dlg, dlg.size()); });

    dlg.exec();
}
