//
//  dlgabout.cpp
//
//  www.catch22.net
//
//  Copyright (C) 2012 James Brown
//  Please refer to the file LICENCE.TXT for copying permission
//

#include "dlgabout.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

void ShowAboutDlg(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("About"));
    dlg.setSizeGripEnabled(false);

    // ── Icon ─────────────────────────────────────────────────────────────────
    QLabel *iconLabel = new QLabel;
    QPixmap px(QLatin1String(":/qexed.png"));
    iconLabel->setPixmap(px.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setFixedSize(64, 64);
    iconLabel->setAlignment(Qt::AlignCenter);

    // ── App name (bold, larger) ───────────────────────────────────────────────
    QLabel *nameLabel = new QLabel(QLatin1String("q22 Hex Editor"));
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 4);
    nameLabel->setFont(nameFont);

    // ── Version ───────────────────────────────────────────────────────────────
    QLabel *verLabel = new QLabel(
        QObject::tr("Version %1").arg(QApplication::applicationVersion()));

    // ── Copyright ─────────────────────────────────────────────────────────────
    QLabel *copyrightLabel = new QLabel(
        QString("Copyright \u00a9 2012 James Brown"));

    // ── Website (clickable) ───────────────────────────────────────────────────
    QLabel *webLabel = new QLabel(
        QLatin1String("<a href=\"https://www.catch22.net\">www.catch22.net</a>"));
    webLabel->setOpenExternalLinks(true);
    webLabel->setTextFormat(Qt::RichText);
    webLabel->setStyleSheet(QLatin1String("QLabel { margin: 0; padding: 0; }"));

    // ── Text column ───────────────────────────────────────────────────────────
    QVBoxLayout *textLayout = new QVBoxLayout;
    textLayout->setSpacing(4);
    textLayout->addWidget(nameLabel);
    textLayout->addSpacing(4);
    textLayout->addWidget(verLabel);
    textLayout->addWidget(copyrightLabel);
    textLayout->addSpacing(8);
    textLayout->addWidget(webLabel);
    textLayout->addSpacing(16);

    // ── Header row (icon + text) ──────────────────────────────────────────────
    QHBoxLayout *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(32);
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);
    headerLayout->addLayout(textLayout);
    headerLayout->addSpacing(16);
    headerLayout->addStretch();

    // ── Buttons ───────────────────────────────────────────────────────────────
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    for (QAbstractButton *btn : buttons->buttons())
        btn->setIcon(QIcon());
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

    // ── Main layout ───────────────────────────────────────────────────────────
    QVBoxLayout *main = new QVBoxLayout(&dlg);
    main->setContentsMargins(20, 20, 20, 20);
    main->setSpacing(0);
    main->addLayout(headerLayout);
    main->addSpacing(14);
    main->addWidget(buttons);
    main->setSizeConstraint(QLayout::SetFixedSize);
    dlg.exec();
}
