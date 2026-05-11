//
//  dlgabout.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgabout.h"
#include "settingscard.h"
#include "slideoverlay.h"
#include "theme.h"

#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QPixmap>
#include <QSettings>
#include <QUrl>
#include <QHBoxLayout>
#include <QVBoxLayout>

void ShowAboutDlg(QWidget *parent)
{
    QDialog dlg(parent);
    removeDialogIcon(&dlg);
    dlg.setWindowTitle(QObject::tr("About"));
    dlg.setSizeGripEnabled(false);

    // ── Icon (192×192) ────────────────────────────────────────────────────────
    auto *iconLabel = new QLabel(&dlg);
    QPixmap px(QLatin1String(":/HexEdit.png"));
    iconLabel->setPixmap(px.scaled(192, 192, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(192, 192);
    iconLabel->setAlignment(Qt::AlignCenter);

    // ── App name (bold, 1.5× base font size) ─────────────────────────────────
    auto *nameLabel = new QLabel(QLatin1String("Catch22 HexEdit"), &dlg);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(qRound(nameFont.pointSize() * 1.5));
    nameLabel->setFont(nameFont);
    nameLabel->setAlignment(Qt::AlignCenter);

    // ── Author ────────────────────────────────────────────────────────────────
    auto *authorLabel = new QLabel(QLatin1String("James Brown"), &dlg);
    authorLabel->setAlignment(Qt::AlignCenter);

    // ── Version badge ─────────────────────────────────────────────────────────
    auto *verLabel = new QLabel(QApplication::applicationVersion(), &dlg);
    verLabel->setAttribute(Qt::WA_StyledBackground, true);
    verLabel->setAlignment(Qt::AlignCenter);
    {
        const int r = (QFontMetrics(verLabel->font()).height() + 8) / 2;
        verLabel->setStyleSheet(
            QString("QLabel { padding: 4px 10px; border-radius: %1px;"
                    " background-color: black; color: white; }").arg(r));
    }

    // ── Links / navigation card ───────────────────────────────────────────────
    auto *websiteRow  = new NavigationRow(QObject::tr("Website"),
                                          QUrl(QLatin1String("https://www.catch22.net")),
                                          &dlg);
    auto *creditsRow  = new NavigationRow(QObject::tr("Credits"),
                                          NavigationRow::Icon::Next, &dlg);
    auto *websiteCard = new SettingsCard({websiteRow, creditsRow},
                                         SettingsCard::Style::Compact, &dlg);

    // ── Config folder card ────────────────────────────────────────────────────
    const QString configPath = QSettings(QSettings::IniFormat, QSettings::UserScope,
                                         "Catch22", "HexEdit").fileName();
    const QUrl configDirUrl  = QUrl::fromLocalFile(QFileInfo(configPath).absolutePath());
    auto *configRow  = new NavigationRow(QObject::tr("Config"), configDirUrl, &dlg);
    auto *configCard = new SettingsCard({configRow},
                                         SettingsCard::Style::Compact, &dlg);

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
    main->addWidget(websiteCard);
    main->addSpacing(6);
    main->addWidget(configCard);
    main->addSpacing(6);

    // Fix width at 150% of natural so the dialog reads wider than its content.
    main->activate();
    dlg.setFixedSize(qRound(dlg.sizeHint().width() * 1.5), dlg.sizeHint().height());

    // ── Credits slides in as an overlay ──────────────────────────────────────
    auto *overlay = new SlideOverlay(&dlg);
    QObject::connect(creditsRow, &QAbstractButton::clicked, &dlg,
                     [&dlg, overlay]() {
        if (overlay->isActive()) return;

        auto *creditsDlg = new QDialog(&dlg);
        creditsDlg->setSizeGripEnabled(false);

        // Title row — SlideOverlay inserts the back button here because this
        // widget is named "overlayHeader".
        auto *headerRow = new QWidget(creditsDlg);
        headerRow->setObjectName(QStringLiteral("overlayHeader"));
        {
            auto *hlay = new QHBoxLayout(headerRow);
            hlay->setContentsMargins(0, 0, 0, 0);
            hlay->setSpacing(8);
            auto *title = new QLabel(QObject::tr("Credits"), headerRow);
            QFont f = title->font();
            f.setBold(true);
            title->setFont(f);
            hlay->addWidget(title);
            hlay->addStretch();
        }

        auto *codeCard  = new SettingsCard(
            { new TextRow(QLatin1String("James Brown"), creditsDlg) },
            SettingsCard::Style::Compact, creditsDlg);
        auto *iconsCard = new SettingsCard(
            { new TextRow(QLatin1String("Icons by GNOME Project"), creditsDlg) },
            SettingsCard::Style::Compact, creditsDlg);

        auto *codeHeader  = new QLabel(QObject::tr("Code by"),  creditsDlg);
        auto *iconsHeader = new QLabel(QObject::tr("Resources"), creditsDlg);
        for (auto *h : { codeHeader, iconsHeader }) {
            QFont f = h->font();
            f.setBold(true);
            h->setFont(f);
            h->setContentsMargins(4, 0, 0, 0);
        }

        auto *lay = new QVBoxLayout(creditsDlg);
        lay->setContentsMargins(16, 16, 16, 16);
        lay->setSpacing(0);
        lay->addSpacing(4);
        lay->addWidget(headerRow);
        lay->addSpacing(20);
        lay->addWidget(codeHeader);
        lay->addSpacing(6);
        lay->addWidget(codeCard);
        lay->addSpacing(20);
        lay->addWidget(iconsHeader);
        lay->addSpacing(6);
        lay->addWidget(iconsCard);
        lay->addStretch();

        overlay->slideIn(creditsDlg, {}, /*resizeParent=*/false);
    });

    execCentered(&dlg);
}
