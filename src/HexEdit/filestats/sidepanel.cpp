#include "filestats/sidepanel.h"

#include "HexView/hexview.h"
#include "chrome/dialog-chrome.h"
#include "combos/menucombobox.h"
#include "filestats/resizegrip.h"
#include "filestats/widgets.h"
#include "settings/scrollhintoverlay.h"
#include "settings/settings.h"
#include "settings/settingscard.h"
#include "theme.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QByteArray>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QCursor>
#include <QDesktopServices>
#include <QEnterEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QPaintEvent>
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QHeaderView>
#include <QStyle>
#include <QThread>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QtMath>

#include <array>
#include <algorithm>
#include <functional>
#include <utility>

using namespace filestats;

static constexpr int kFileInfoPaneMinWidth = 280;
static constexpr int kFileInfoPaneMaxWidth = 720;
static constexpr int kFileInfoPaneAnimMs = 220;
static constexpr bool kAutoStartPanelOperations = true;

FilePropertiesPanel::FilePropertiesPanel(HexView *hexView, QWidget *parent)
    : QDialog(parent), m_hexView(hexView)
{
    m_sectionOrder = { SectionId::Properties, SectionId::Checksums, SectionId::Strings };

    setWindowTitle(tr("File Properties"));
    setSizeGripEnabled(false);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, pal.color(QPalette::Window));
    setPalette(pal);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_scrollArea->verticalScrollBar()->setFocusPolicy(Qt::NoFocus);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_scrollArea->setStyleSheet(QStringLiteral(R"(
        QScrollArea {
            background: transparent;
            border: none;
        }
    )"));
    ScrollHintOverlay::install(m_scrollArea);
    m_scrollArea->verticalScrollBar()->setProperty("filePropertiesScrollBar", true);
    m_scrollArea->verticalScrollBar()->style()->unpolish(m_scrollArea->verticalScrollBar());
    m_scrollArea->verticalScrollBar()->style()->polish(m_scrollArea->verticalScrollBar());

    m_content = new QWidget(m_scrollArea);
    m_content->setMinimumWidth(0);
    m_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(kSectionHeaderOuterMargin, kContentMargin,
                                      kSectionHeaderOuterMargin, kContentMargin);
    contentLayout->setSpacing(0);

    m_fileHeader = new SectionHeader(tr("File Properties"), m_content);
    m_fileHeader->setClickedCallback([this]() {
        setSectionCollapsed(SectionId::Properties, !isSectionCollapsed(SectionId::Properties));
    });
    contentLayout->addWidget(m_fileHeader);
    m_fileHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_fileHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_fileHeaderGap);

    m_fileSectionBody = new QWidget(m_content);
    m_fileSectionBody->setMinimumWidth(0);
    auto *fileBodyLayout = new QVBoxLayout(m_fileSectionBody);
    fileBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                       kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    fileBodyLayout->setSpacing(0);

    auto *card = new SettingsCard({
        new PropertyRow(tr("Name"), &m_nameValue, m_fileSectionBody),
        new PropertyRow(tr("Location"), &m_locationValue, m_fileSectionBody,
                        PropertyRow::Action::OpenExternal,
                        [this]() {
                            if (!m_hexView)
                                return;
                            const QString path = m_hexView->filePath();
                            if (!path.isEmpty())
                                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
                        }),
        new PropertyRow(tr("Size"), &m_sizeValue, m_fileSectionBody),
    }, SettingsCard::Style::Spaced, m_fileSectionBody);
    card->setMinimumWidth(0);
    fileBodyLayout->addWidget(card);
    contentLayout->addWidget(m_fileSectionBody);

    m_interSectionGaps.append(new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed));
    contentLayout->addSpacerItem(m_interSectionGaps[0]);
    m_checksumHeader = new SectionHeader(tr("Checksums"), m_content);
    m_checksumHeader->setClickedCallback([this]() {
        setSectionCollapsed(SectionId::Checksums, !isSectionCollapsed(SectionId::Checksums));
    });
    contentLayout->addWidget(m_checksumHeader);
    m_checksumHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_checksumHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_checksumHeaderGap);

    m_checksumSectionBody = new QWidget(m_content);
    m_checksumSectionBody->setMinimumWidth(0);
    auto *checksumBodyLayout = new QVBoxLayout(m_checksumSectionBody);
    checksumBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                           kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    checksumBodyLayout->setSpacing(0);

    auto startChecksums = [this]() {
        if (m_checksumState.started)
            return;
        m_checksumState.started = true;
        startChecksumCalculation();
    };
    m_checksumOperation = new SectionOperationStrip(m_content,
        startChecksums,
        [this]() { cancelChecksumCalculation(); },
        startChecksums);
    contentLayout->addWidget(m_checksumOperation->widget());

    auto checksumRow = [this](const QString &name, bool checked) {
        QLabel *value = nullptr;
        QCheckBox *checkBox = nullptr;
        auto *row = new PropertyRow(name, &value, m_checksumSectionBody,
                                    PropertyRow::Action::CopyValue, {},
                                    &checkBox, checked);
        m_checksumValues.insert(name, value);
        if (checkBox) {
            m_checksumChecks.insert(name, checkBox);
            connect(checkBox, &QCheckBox::toggled,
                    this, &FilePropertiesPanel::markChecksumAlgorithmsChanged);
        }
        return row;
    };

    auto *checksumCard = new SettingsCard({
        checksumRow(QStringLiteral("CRC32"), true),
        checksumRow(QStringLiteral("CRC16"), false),
        checksumRow(QStringLiteral("SHA256"), true),
        checksumRow(QStringLiteral("SHA1"), false),
        checksumRow(QStringLiteral("MD5"), false),
        checksumRow(QStringLiteral("MD4"), false),
        checksumRow(QStringLiteral("MD2"), false),
    }, SettingsCard::Style::Spaced, m_checksumSectionBody);
    checksumCard->setMinimumWidth(0);
    checksumBodyLayout->addWidget(checksumCard);
    contentLayout->addWidget(m_checksumSectionBody);

    m_interSectionGaps.append(new QSpacerItem(0, kGroupTopGap, QSizePolicy::Minimum, QSizePolicy::Fixed));
    contentLayout->addSpacerItem(m_interSectionGaps[1]);
    m_stringsHeader = new SectionHeader(tr("Strings"), m_content);
    m_stringsHeader->setClickedCallback([this]() {
        setSectionCollapsed(SectionId::Strings, !isSectionCollapsed(SectionId::Strings));
    });
    contentLayout->addWidget(m_stringsHeader);
    m_stringsHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_stringsHeaderGap = new QSpacerItem(0, kHeaderControlGap, QSizePolicy::Minimum, QSizePolicy::Fixed);
    contentLayout->addSpacerItem(m_stringsHeaderGap);

    m_stringsSectionBody = new QWidget(m_content);
    m_stringsSectionBody->setMinimumWidth(0);
    auto *stringsBodyLayout = new QVBoxLayout(m_stringsSectionBody);
    stringsBodyLayout->setContentsMargins(kSectionHeaderOuterMargin + kCardLeftInset, 0,
                                          kSectionHeaderOuterMargin + kCardScrollbarInset, 0);
    stringsBodyLayout->setSpacing(0);

    auto startStrings = [this]() {
        if (m_stringsState.started)
            return;
        m_stringsState.started = true;
        startStringScan();
    };
    m_stringsOperation = new SectionOperationStrip(m_content,
        startStrings,
        [this]() { cancelStringScan(); },
        [this, startStrings]() {
            if (m_stringsList)
                m_stringsList->clear();
            startStrings();
        });
    contentLayout->addWidget(m_stringsOperation->widget());

    auto *stringsControlsStack = new QWidget(m_stringsSectionBody);
    auto *stringsControlsStackLayout = new QVBoxLayout(stringsControlsStack);
    stringsControlsStackLayout->setContentsMargins(kSettingsCardShadowInset, 0,
                                                   kSettingsCardShadowInset, 0);
    stringsControlsStackLayout->setSpacing(0);

    auto *stringsOptionsMenu = new QMenu(this);
    themeMenu(stringsOptionsMenu);
    m_includeWhitespaceAction = stringsOptionsMenu->addAction(tr("Include whitespace"));
    m_includeWhitespaceAction->setCheckable(true);
    m_includeWhitespaceAction->setChecked(true);
    stringsOptionsMenu->addSeparator();
    m_prefixHexOffsetAction = stringsOptionsMenu->addAction(tr("Prefix hex offset"));
    m_prefixHexOffsetAction->setCheckable(true);
    m_prefixHexOffsetAction->setChecked(false);

    m_stringOptionsButton = new QToolButton(m_stringsSectionBody);
    m_stringOptionsButton->setFixedSize(28, 28);
    m_stringOptionsButton->setFocusPolicy(Qt::TabFocus);
    m_stringOptionsButton->setToolTip(tr("String options"));
    m_stringOptionsButton->setAutoRaise(true);
    m_stringOptionsButton->setPopupMode(QToolButton::InstantPopup);
    m_stringOptionsButton->setProperty("iconThemeName", QStringLiteral("permissions"));
    m_stringOptionsButton->setProperty("iconSize", 16);
    m_stringOptionsButton->setIconSize(QSize(16, 16));
    m_stringOptionsButton->setIcon(recoloredIcon(QStringLiteral("permissions"),
                                                 palette().buttonText().color(), 16));
    {
        const bool dark = QApplication::palette().window().color().lightness() < 128;
        const QString hover = dark ? QStringLiteral("rgba(255,255,255,0.15)")
                                   : QStringLiteral("rgba(0,0,0,0.10)");
        const QString pressed = dark ? QStringLiteral("rgba(255,255,255,0.25)")
                                     : QStringLiteral("rgba(0,0,0,0.18)");
        m_stringOptionsButton->setStyleSheet(QStringLiteral(R"(
            QToolButton {
                border: none;
                border-radius: 6px;
                background: transparent;
            }
            QToolButton:hover { background: %1; }
            QToolButton:focus { border: 2px solid palette(highlight); }
            QToolButton:pressed { background: %2; }
            QToolButton::menu-indicator { image: none; width: 0; }
        )").arg(hover, pressed));
    }

    m_stringEncoding = new MenuComboBox(m_stringsSectionBody);
    m_stringEncoding->addItem(tr("Printable ASCII"), QStringLiteral("[ -~]"));
    m_stringEncoding->addItem(tr("Alphanumeric"), QStringLiteral("[A-Za-z0-9]"));
    m_stringEncoding->addItem(tr("ASCII text"), QStringLiteral("[\\t -~]"));
    m_stringEncoding->addItem(tr("C identifiers"), QStringLiteral("[A-Za-z_][A-Za-z0-9_]*\\0"));
    m_stringEncoding->setCurrentIndex(1);
    m_stringEncoding->setFocusPolicy(Qt::StrongFocus);
    m_stringEncoding->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_stringEncoding->setFixedHeight(qMax(24, m_stringEncoding->sizeHint().height() - 4));

    m_minStringLength = new StepSpinBox(tr("Min:"), 3, 128, 1, m_stringsSectionBody);
    m_minStringLength->setValue(3);
    m_minStringLength->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_minStringLength->setLabelAlignment(Qt::AlignRight);
    m_minStringLength->setLabelValueSpacing(4);
    m_minStringLength->setValueBold(true);

    auto *stringsControls = new QWidget(m_stringsSectionBody);
    auto *stringsControlsLayout = new QHBoxLayout(stringsControls);
    stringsControlsLayout->setContentsMargins(0, 0, 0, 0);
    stringsControlsLayout->setSpacing(kContentMargin + 6);
    stringsControlsLayout->addWidget(m_stringOptionsButton, 0, Qt::AlignVCenter);
    stringsControlsLayout->addWidget(m_stringEncoding, 0, Qt::AlignVCenter);
    stringsControlsLayout->addStretch(1);
    stringsControlsLayout->addWidget(m_minStringLength, 0);
    stringsControlsStackLayout->addWidget(stringsControls);
    stringsControlsStackLayout->addSpacing(kHeaderControlGap + 4);

    auto *stringsListFrame = new StringListFrame(m_stringsSectionBody);
    stringsListFrame->setObjectName(QStringLiteral("stringsListFrame"));
    stringsListFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    stringsListFrame->setFixedHeight(kStringsListMinHeight);
    stringsListFrame->setStyleSheet(QStringLiteral(R"(
        QFrame#stringsListFrame {
            background: palette(base);
            border: 1px solid palette(mid);
            border-radius: 6px;
        }
    )"));

    m_stringsList = new QTreeWidget(stringsListFrame);
    m_stringsList->setMinimumSize(0, 0);
    m_stringsList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_stringsList->setColumnCount(2);
    m_stringsList->setHeaderHidden(true);
    m_stringsList->setRootIsDecorated(false);
    m_stringsList->setAlternatingRowColors(false);
    m_stringsList->setUniformRowHeights(true);
    m_stringsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stringsList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_stringsList->header()->setStretchLastSection(false);
    m_stringsList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stringsList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_stringsList->setStyleSheet(QStringLiteral(R"(
        QTreeWidget {
            background: palette(base);
            border: none;
            padding: 0px;
        }
        QTreeWidget::item {
            padding: 3px 6px;
        }
        QTreeWidget::item:hover {
            background: palette(button);
        }
        QTreeWidget::item:selected {
            background: palette(highlight);
            color: palette(highlighted-text);
        }
    )"));
    stringsListFrame->setListWidget(m_stringsList);
    stringsControlsStackLayout->addWidget(stringsListFrame);

    m_stringsStatusRow = new QWidget(m_stringsSectionBody);
    auto *stringsStatusLayout = new QGridLayout(m_stringsStatusRow);
    stringsStatusLayout->setContentsMargins(6, 6, 6, 2);
    stringsStatusLayout->setHorizontalSpacing(8);
    stringsStatusLayout->setVerticalSpacing(0);
    m_stringsStatusLabel = new QLabel(m_stringsStatusRow);
    m_stringsStatusLabel->setWordWrap(true);
    m_stringsStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_stringsStatusLabel->setStyleSheet(QStringLiteral("color: %1;")
                                        .arg(cssColor(subduedTextColor(palette()))));
    m_stringsProgressLabel = new QLabel(m_stringsStatusRow);
    m_stringsProgressLabel->setStyleSheet(QStringLiteral("color: %1;")
                                          .arg(cssColor(subduedTextColor(palette()))));
    static constexpr int kStringsFooterButtonSize = 24;

    m_stringsNextButton = new QPushButton(m_stringsStatusRow);
    m_stringsNextButton->setFlat(false);
    m_stringsNextButton->setCursor(Qt::PointingHandCursor);
    m_stringsNextButton->setToolTip(tr("Next"));
    m_stringsNextButton->setFixedSize(kStringsFooterButtonSize, kStringsFooterButtonSize);
    m_stringsNextButton->setIcon(recoloredIcon(QStringLiteral("ui/go-next-symbolic"),
                                               palette().buttonText().color(), 12));
    m_stringsNextButton->setIconSize(QSize(12, 12));

    m_stringsAllButton = new QPushButton(m_stringsStatusRow);
    m_stringsAllButton->setFlat(false);
    m_stringsAllButton->setCursor(Qt::PointingHandCursor);
    m_stringsAllButton->setToolTip(tr("Complete scan to end of file"));
    m_stringsAllButton->setFixedSize(kStringsFooterButtonSize, kStringsFooterButtonSize);
    m_stringsAllButton->setIcon(recoloredIcon(QStringLiteral("actions/go-last-symbolic"),
                                              palette().buttonText().color(), 12));
    m_stringsAllButton->setIconSize(QSize(12, 12));

    m_stringsExportButton = new QPushButton(m_stringsStatusRow);
    m_stringsExportButton->setFlat(false);
    m_stringsExportButton->setCursor(Qt::PointingHandCursor);
    m_stringsExportButton->setToolTip(tr("Export results"));
    m_stringsExportButton->setFixedSize(kStringsFooterButtonSize, kStringsFooterButtonSize);
    m_stringsExportButton->setIcon(recoloredIcon(QStringLiteral("actions/downloads"),
                                                 palette().buttonText().color(), 12));
    m_stringsExportButton->setIconSize(QSize(12, 12));
    const QColor nextBg = palette().button().color();
    const bool nextDark = palette().window().color().lightness() < 128;
    auto overlayColor = [](const QColor &base, const QColor &overlay, int alpha) {
        const int inv = 255 - alpha;
        return QColor((base.red() * inv + overlay.red() * alpha) / 255,
                      (base.green() * inv + overlay.green() * alpha) / 255,
                      (base.blue() * inv + overlay.blue() * alpha) / 255);
    };
    const QColor nextHover = nextDark
                                 ? overlayColor(nextBg, QColor(255, 255, 255), 30)
                                 : overlayColor(nextBg, QColor(0, 0, 0), 22);
    const QColor nextPressed = palette().mid().color();
    const QColor nextBorder = palette().mid().color();
    const QString stringsFooterButtonStyle = QStringLiteral(R"(
        QPushButton {
            border: 1px solid %1;
            border-radius: 6px;
            min-width: %6px;
            max-width: %6px;
            min-height: %6px;
            max-height: %6px;
            padding: 0px;
            color: %2;
            background: %3;
        }
        QPushButton:hover {
            background: %4;
        }
        QPushButton:pressed {
            background: %5;
        }
    )").arg(cssColor(nextBorder),
            cssColor(palette().buttonText().color()),
            cssColor(nextBg),
            cssColor(nextHover),
            cssColor(nextPressed),
            QString::number(kStringsFooterButtonSize));
    m_stringsNextButton->setStyleSheet(stringsFooterButtonStyle);
    m_stringsAllButton->setStyleSheet(stringsFooterButtonStyle);
    m_stringsExportButton->setStyleSheet(stringsFooterButtonStyle);
    stringsStatusLayout->addWidget(m_stringsStatusLabel, 0, 0, Qt::AlignVCenter);
    stringsStatusLayout->addWidget(m_stringsNextButton, 0, 1, Qt::AlignVCenter);
    stringsStatusLayout->addWidget(m_stringsAllButton, 0, 2, Qt::AlignVCenter);
    stringsStatusLayout->addWidget(m_stringsExportButton, 0, 3, Qt::AlignVCenter);
    stringsStatusLayout->addWidget(m_stringsProgressLabel, 1, 0);
    stringsStatusLayout->setColumnStretch(0, 1);
    m_stringsStatusRow->hide();

    auto *stringsResizeWrap = new QWidget(m_stringsSectionBody);
    auto *stringsResizeLayout = new QVBoxLayout(stringsResizeWrap);
    stringsResizeLayout->setContentsMargins(0, 0, 0, 0);
    stringsResizeLayout->setSpacing(0);
    m_stringsResizeHandle = new VerticalResizeHandle([this](int dy) {
        resizeSection(SectionId::Strings, dy);
    }, stringsResizeWrap);
    stringsResizeLayout->addWidget(m_stringsResizeHandle);
    stringsResizeLayout->setAlignment(m_stringsResizeHandle, Qt::AlignTop);
    stringsControlsStackLayout->addWidget(stringsResizeWrap);
    stringsControlsStackLayout->addWidget(m_stringsStatusRow);
    stringsBodyLayout->addWidget(stringsControlsStack);
    contentLayout->addWidget(m_stringsSectionBody);
    contentLayout->addStretch();

    m_scrollArea->setWidget(m_content);
    root->addWidget(m_scrollArea, 1);

    m_stickyHeader = new SectionHeader(QString(), m_scrollArea->viewport());
    m_stickyHeader->hide();
    m_stickyHeader->raise();

    m_dropIndicator = new QWidget(m_scrollArea->viewport());
    m_dropIndicator->setFixedHeight(4);
    m_dropIndicator->setAutoFillBackground(true);
    {
        QPalette ip = m_dropIndicator->palette();
        ip.setColor(QPalette::Window, palette().mid().color());
        m_dropIndicator->setPalette(ip);
    }
    m_dropIndicator->hide();

    registerPanelSection({
        SectionId::Properties,
        tr("File Properties"),
        m_fileHeader,
        m_fileSectionBody,
        m_fileHeaderGap,
    });
    registerPanelSection({
        SectionId::Checksums,
        tr("Checksums"),
        m_checksumHeader,
        m_checksumSectionBody,
        m_checksumHeaderGap,
        m_checksumOperation,
        nullptr,
        0,
        [this]() { maybeStartChecksumCalculation(); },
        [this](bool collapsed) {
            if (m_checksumState.pause && m_checksumState.started)
                m_checksumState.pause->setPaused(collapsed);
            if (m_checksumState.started)
                setChecksumProgressTitle(m_checksumState.progress);
        },
        [this](bool contentsChanged) {
            if (contentsChanged)
                markChecksumContentsChanged();
        },
        [this]() { resetChecksumForCurrentDocument(); },
    });
    registerPanelSection({
        SectionId::Strings,
        tr("Strings"),
        m_stringsHeader,
        m_stringsSectionBody,
        m_stringsHeaderGap,
        m_stringsOperation,
        stringsListFrame,
        kStringsListMinHeight,
        [this]() { maybeStartStringScan(); },
        [this](bool collapsed) {
            if (m_stringsState.pause && m_stringsState.started)
                m_stringsState.pause->setPaused(collapsed);
            if (m_stringsState.started)
                setStringsProgressTitle(m_stringsState.progress);
        },
        [this](bool contentsChanged) {
            if (contentsChanged)
                markStringsContentsChanged();
        },
        [this]() { resetStringsForCurrentDocument(); },
    });

    for (SectionId s : m_sectionOrder) {
        sectionFor(s)->header->setDragCallbacks(
            [this, s](QPoint p) { onDragStarted(s, p); },
            [this]   (QPoint p) { onDragMoved(p); },
            [this, s](QPoint p) { onDragEnded(s, p); });
    }
    connect(m_minStringLength, &StepSpinBox::valueChanged, this, [this](int) {
        m_stringsState.started = false;
        ++m_stringsState.generation;
        if (m_stringsState.cancel)
            m_stringsState.cancel->store(true);
        if (m_stringsState.pause)
            m_stringsState.pause->wake();
        m_stringMoreAvailable = false;
        m_stringsState.rescanRequired = true;
        m_stringsState.rescanMessage = tr("Options changed");
        m_stringNextOffset = 0;
        clearStringExportTemp();
        if (m_stringsList)
            m_stringsList->clear();
        if (m_stringsStatusRow)
            m_stringsStatusRow->hide();
        if (m_stringsOperation)
            m_stringsOperation->showRescan(m_stringsState.rescanMessage);
        requestSectionLayoutRefresh(SectionId::Strings);
    });
    connect(m_stringEncoding, &QComboBox::currentIndexChanged, this, [this](int) {
        m_stringsState.started = false;
        ++m_stringsState.generation;
        if (m_stringsState.cancel)
            m_stringsState.cancel->store(true);
        if (m_stringsState.pause)
            m_stringsState.pause->wake();
        m_stringMoreAvailable = false;
        m_stringsState.rescanRequired = true;
        m_stringsState.rescanMessage = tr("Options changed");
        m_stringNextOffset = 0;
        clearStringExportTemp();
        if (m_stringsList)
            m_stringsList->clear();
        if (m_stringsStatusRow)
            m_stringsStatusRow->hide();
        if (m_stringsOperation)
            m_stringsOperation->showRescan(m_stringsState.rescanMessage);
        requestSectionLayoutRefresh(SectionId::Strings);
    });
    connect(m_includeWhitespaceAction, &QAction::toggled, this, [this](bool) {
        m_stringsState.started = false;
        ++m_stringsState.generation;
        if (m_stringsState.cancel)
            m_stringsState.cancel->store(true);
        if (m_stringsState.pause)
            m_stringsState.pause->wake();
        m_stringMoreAvailable = false;
        m_stringsState.rescanRequired = true;
        m_stringsState.rescanMessage = tr("Options changed");
        m_stringNextOffset = 0;
        clearStringExportTemp();
        if (m_stringsList)
            m_stringsList->clear();
        if (m_stringsStatusRow)
            m_stringsStatusRow->hide();
        if (m_stringsOperation)
            m_stringsOperation->showRescan(m_stringsState.rescanMessage);
        requestSectionLayoutRefresh(SectionId::Strings);
    });
    connect(m_prefixHexOffsetAction, &QAction::toggled, this, [this](bool) {
        m_stringsState.started = false;
        ++m_stringsState.generation;
        if (m_stringsState.cancel)
            m_stringsState.cancel->store(true);
        if (m_stringsState.pause)
            m_stringsState.pause->wake();
        m_stringMoreAvailable = false;
        m_stringsState.rescanRequired = true;
        m_stringsState.rescanMessage = tr("Options changed");
        m_stringNextOffset = 0;
        clearStringExportTemp();
        if (m_stringsList)
            m_stringsList->clear();
        if (m_stringsStatusRow)
            m_stringsStatusRow->hide();
        if (m_stringsOperation)
            m_stringsOperation->showRescan(m_stringsState.rescanMessage);
        requestSectionLayoutRefresh(SectionId::Strings);
    });
    connect(m_stringOptionsButton, &QToolButton::clicked, this, [this, stringsOptionsMenu]() {
        if (stringsOptionsMenu->isVisible()) {
            stringsOptionsMenu->hide();
            return;
        }
        const QPoint cur = QCursor::pos();
        const bool same = (m_stringOptionsMenuClosePos == cur);
        m_stringOptionsMenuClosePos = {-1, -1};
        if (same)
            return;
        connect(stringsOptionsMenu, &QMenu::aboutToHide, this,
                [this]() { m_stringOptionsMenuClosePos = QCursor::pos(); },
                Qt::SingleShotConnection);
        stringsOptionsMenu->popup(smartMenuPos(m_stringOptionsButton, stringsOptionsMenu));
    });
    connect(m_stringsNextButton, &QPushButton::clicked, this, [this]() {
        if (m_stringsState.started || !m_stringMoreAvailable)
            return;
        m_stringsState.started = true;
        startStringScan(m_stringNextOffset, true);
    });
    connect(m_stringsAllButton, &QPushButton::clicked, this, [this]() {
        if (m_stringsState.started || !m_stringMoreAvailable)
            return;
        m_stringsState.started = true;
        startStringScan(m_stringNextOffset, true, true);
    });
    connect(m_stringsExportButton, &QPushButton::clicked,
            this, &FilePropertiesPanel::exportStringResults);
    auto navigateToStringItem = [this](QTreeWidgetItem *item, int) {
        if (!item || !m_hexView)
            return;
        if (item->data(0, Qt::UserRole + 2).toBool())
            return;
        const size_w offset = static_cast<size_w>(item->data(0, Qt::UserRole).toULongLong());
        const size_w length = static_cast<size_w>(item->data(0, Qt::UserRole + 1).toULongLong());
        m_hexView->scrollCenterIfOffScreen(offset, length);
        m_hexView->setCurSel(offset, qMin(offset + length, m_hexView->size()));
        m_hexView->setFocus();
    };
    connect(m_stringsList, &QTreeWidget::itemClicked, this, navigateToStringItem);
    connect(m_stringsList, &QTreeWidget::itemActivated, this, navigateToStringItem);
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &FilePropertiesPanel::updateStickyHeader);
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this]() { updateStickyHeader(); });
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);

    setMinimumWidth(260);
    resetForCurrentDocument();
    // Collapse without animation: the panel hasn't been shown yet, so
    // animateSectionBody's isVisible() guard would bail out early and leave all
    // bodies visible when the panel first appears.
    setSectionCollapsed(SectionId::Properties, true, false);
    setSectionCollapsed(SectionId::Checksums, true, false);
    setSectionCollapsed(SectionId::Strings, true, false);
}

FilePropertiesPanel::~FilePropertiesPanel()
{
    ++m_checksumState.generation;
    ++m_stringsState.generation;
    if (m_checksumState.cancel)
        m_checksumState.cancel->store(true);
    if (m_checksumState.pause)
        m_checksumState.pause->wake();
    if (m_stringsState.cancel)
        m_stringsState.cancel->store(true);
    if (m_stringsState.pause)
        m_stringsState.pause->wake();
    clearStringExportTemp();
}

bool FilePropertiesPanel::shouldAutoStartOperations() const
{
    return kAutoStartPanelOperations;
}

void FilePropertiesPanel::exportStringResults()
{
    if (!m_stringsList || m_stringsList->topLevelItemCount() == 0)
        return;

    const bool useNativeFileDialogs = AppSettings::prefNativeFileDialogs();
    QWidget *dialogParent = window();
    QFileDialog dlg(dialogParent ? dialogParent : this, tr("Export Strings"));
    dlg.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogs);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setNameFilter(tr("Text files (*.txt);;All files (*)"));
    dlg.selectFile(QStringLiteral("strings.txt"));
    dlg.setDefaultSuffix(QStringLiteral("txt"));
    if (!useNativeFileDialogs) {
        installThemedFileDialogComboPopups(&dlg);
        installDialogChrome(&dlg);
    }

    if ((useNativeFileDialogs ? dlg.exec() : execCentered(&dlg)) != QDialog::Accepted)
        return;

    const QString path = dlg.selectedFiles().value(0);
    if (path.isEmpty())
        return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    if (m_stringsExportTempComplete && !m_stringsExportTempPath.isEmpty()) {
        QFile temp(m_stringsExportTempPath);
        if (!temp.open(QIODevice::ReadOnly)) {
            file.cancelWriting();
            return;
        }
        while (!temp.atEnd()) {
            const QByteArray chunk = temp.read(1024 * 1024);
            if (chunk.isEmpty())
                break;
            file.write(chunk);
        }
    } else {
        QTextStream out(&file);
        const bool prefixHexOffset = m_prefixHexOffsetAction && m_prefixHexOffsetAction->isChecked();
        for (int i = 0; i < m_stringsList->topLevelItemCount(); ++i) {
            if (QTreeWidgetItem *item = m_stringsList->topLevelItem(i)) {
                if (item->data(0, Qt::UserRole + 2).toBool())
                    continue;
                if (prefixHexOffset) {
                    const qulonglong offset = item->data(0, Qt::UserRole).toULongLong();
                    out << QStringLiteral("%1 ")
                               .arg(offset, 8, 16, QLatin1Char('0')).toUpper();
                }
                out << item->text(0) << '\n';
            }
        }
    }
    file.commit();
}

void FilePropertiesPanel::setChecksumProgressTitle(int value)
{
    m_checksumState.progress = value;
    if (!m_checksumHeader)
        return;
    const int pct = qBound(0, value, 1000) / 10;
    m_checksumHeader->setTitle(isSectionCollapsed(SectionId::Checksums)
        ? tr("Checksums (%1% - paused)").arg(pct)
        : tr("Checksums (%1%)").arg(pct));
    syncStickyHeader();
}

void FilePropertiesPanel::setStringsProgressTitle(int value)
{
    if (!m_stringsHeader)
        return;
    const int pct = qBound(0, value, 1000) / 10;
    m_stringsHeader->setTitle(isSectionCollapsed(SectionId::Strings)
        ? tr("Strings (%1% - paused)").arg(pct)
        : tr("Strings (%1%)").arg(pct));
    syncStickyHeader();
}

void FilePropertiesPanel::resetChecksumTitle()
{
    if (m_checksumHeader)
        m_checksumHeader->setTitle(tr("Checksums"));
    syncStickyHeader();
}

void FilePropertiesPanel::resetStringsTitle()
{
    if (m_stringsHeader)
        m_stringsHeader->setTitle(tr("Strings"));
    syncStickyHeader();
}

void FilePropertiesPanel::showSection(SectionId section)
{
    setSectionCollapsed(section, false);
}

void FilePropertiesPanel::setPanelFullyOpened(bool opened)
{
    m_panelFullyOpened = opened;
    if (opened)
        for (SectionId section : std::as_const(m_sectionOrder))
            emitSectionReadyIfPossible(section);
}

void FilePropertiesPanel::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        recolorToolButtons(this);
}

void FilePropertiesPanel::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateStickyHeader();
}

void FilePropertiesPanel::animateSectionBody(QWidget *body, bool collapse, bool animate)
{
    if (!body)
        return;

    const QString animName = QStringLiteral("sectionBodyAnim");
    if (auto *existing = body->findChild<QPropertyAnimation *>(animName))
        existing->stop();

    if (!animate) {
        // Skip animation: snap directly to the target state.  Used when restoring
        // sections after a reorder so the freshly-rebuilt layout isn't asked to
        // drive a maximumHeight animation before it has settled.
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        if (collapse) {
            body->hide();
        } else {
            body->show();
            if (QWidget *p = body->parentWidget())
                if (QLayout *l = p->layout())
                    l->activate();
        }
        return;
    }

    // These bodies contain dynamic content: operation strips appear/disappear,
    // labels update while background work finishes, and sections can be
    // re-ordered mid-interaction.  Animating maximumHeight makes that live
    // layout state part of the animation state, which can strand an expanded
    // body at height 0.  Keep the visual state deterministic: collapsed means
    // hidden, expanded means shown and unconstrained.
    if (collapse) {
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        body->hide();
    } else {
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        body->show();
        body->updateGeometry();
        if (QWidget *p = body->parentWidget())
            if (QLayout *l = p->layout()) {
                l->invalidate();
                l->activate();
            }
    }
}

void FilePropertiesPanel::setSectionCollapsed(SectionId sectionId, bool collapsed, bool animate)
{
    PanelSection *section = sectionFor(sectionId);
    if (!section)
        return;

    const bool wasCollapsed = section->collapsed;
    section->collapsed = collapsed;
    animateSectionBody(section->body, collapsed, animate);
    if (section->operation)
        section->operation->setCollapsed(collapsed);
    if (section->headerGap)
        section->headerGap->changeSize(0, collapsed ? 0 : kHeaderControlGap,
                                       QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (section->header)
        section->header->setCollapsed(collapsed);
    if (section->onCollapsedChanged)
        section->onCollapsedChanged(collapsed);
    updateInterSectionGaps();
    if (!collapsed) {
        repairExpandedSectionGeometry(sectionId);
        settleContentLayout();
        requestSectionLayoutRefresh(sectionId);
    }
    if (wasCollapsed && !collapsed) {
        emit sectionExpanded(sectionId);
        emitSectionReadyIfPossible(sectionId);
        scheduleResizableSectionRepair(sectionId);
        QTimer::singleShot(170, this, [this, sectionId]() {
            repairExpandedSectionGeometry(sectionId);
            settleContentLayout();
        });
    }
    updateStickyHeader();
    QTimer::singleShot(0, this, &FilePropertiesPanel::updateStickyHeader);
}

void FilePropertiesPanel::emitSectionReadyIfPossible(SectionId section)
{
    if (!m_panelFullyOpened)
        return;

    if (!isSectionCollapsed(section)) {
        emit sectionReady(section);
        if (PanelSection *panelSection = sectionFor(section))
            if (panelSection->onExpanded)
                panelSection->onExpanded();
    }
}

void FilePropertiesPanel::syncStickyHeader()
{
    if (!m_scrollArea || !m_stickyHeader || m_sectionOrder.isEmpty())
        return;

    const int headerWidth = qMax(1, m_scrollArea->viewport()->width()
                                       - 2 * kSectionHeaderOuterMargin);

    // Hide if the first header is still visible above the fold
    const PanelSection *first = sectionFor(m_sectionOrder.first());
    if (!first || !first->header)
        return;
    const int firstY = first->header->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
    if (firstY > 0) {
        m_stickyHeader->hide();
        return;
    }

    // Find the bottommost section that has scrolled above the viewport
    SectionId activeSection = m_sectionOrder.first();
    int nextHeaderY = kSectionHeaderHeight;
    for (int i = 0; i < m_sectionOrder.size(); ++i) {
        const PanelSection *section = sectionFor(m_sectionOrder[i]);
        if (!section || !section->header)
            continue;
        SectionHeader *h = section->header;
        const int y = h->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
        if (y <= 0) {
            activeSection = m_sectionOrder[i];
            if (i + 1 < m_sectionOrder.size()) {
                if (const PanelSection *next = sectionFor(m_sectionOrder[i + 1]))
                    if (next->header)
                        nextHeaderY = next->header->mapTo(m_scrollArea->viewport(), QPoint(0, 0)).y();
            } else {
                nextHeaderY = kSectionHeaderHeight;
            }
        }
    }

    const PanelSection *active = sectionFor(activeSection);
    if (!active || !active->header)
        return;
    m_stickyHeader->setTitle(active->header->title());
    m_stickyHeader->setCollapsed(active->collapsed);
    m_stickyHeader->setClickedCallback([this, activeSection]() {
        setSectionCollapsed(activeSection, !isSectionCollapsed(activeSection));
    });

    const int y = qMin(0, nextHeaderY - kSectionHeaderHeight);
    m_stickyHeader->setGeometry(kSectionHeaderOuterMargin, y, headerWidth, kSectionHeaderHeight);
    m_stickyHeader->show();
    m_stickyHeader->raise();
}

void FilePropertiesPanel::updateStickyHeader()
{
    syncStickyHeader();
}

void FilePropertiesPanel::registerPanelSection(const PanelSectionSpec &spec)
{
    PanelSection section;
    section.id = spec.id;
    section.title = spec.title;
    section.header = spec.header;
    section.body = spec.body;
    section.headerGap = spec.headerGap;
    section.operation = spec.operation;
    section.onExpanded = spec.onExpanded;
    section.onCollapsedChanged = spec.onCollapsedChanged;
    section.onRefreshDocumentState = spec.onRefreshDocumentState;
    section.onResetForCurrentDocument = spec.onResetForCurrentDocument;
    if (spec.resizableTarget) {
        section.resize.target = spec.resizableTarget;
        section.resize.minHeight = spec.minResizableHeight;
        section.resize.currentHeight = spec.minResizableHeight;
        spec.resizableTarget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        spec.resizableTarget->setFixedHeight(spec.minResizableHeight);
    }
    m_sections.append(section);
}

FilePropertiesPanel::PanelSection *FilePropertiesPanel::sectionFor(SectionId section)
{
    for (PanelSection &candidate : m_sections)
        if (candidate.id == section)
            return &candidate;
    return nullptr;
}

const FilePropertiesPanel::PanelSection *FilePropertiesPanel::sectionFor(SectionId section) const
{
    for (const PanelSection &candidate : m_sections)
        if (candidate.id == section)
            return &candidate;
    return nullptr;
}

bool FilePropertiesPanel::isSectionCollapsed(SectionId section) const
{
    if (const PanelSection *panelSection = sectionFor(section))
        return panelSection->collapsed;
    return true;
}

void FilePropertiesPanel::updateInterSectionGaps()
{
    for (int i = 0; i + 1 < m_sectionOrder.size(); ++i) {
        const bool prevCollapsed = isSectionCollapsed(m_sectionOrder[i]);
        m_interSectionGaps[i]->changeSize(
            0, prevCollapsed ? kHeaderControlGap : kGroupTopGap,
            QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    if (m_content && m_content->layout())
        m_content->layout()->invalidate();
}

void FilePropertiesPanel::rebuildSectionLayout()
{
    // Stop any in-flight body animations and snap to a clean hidden state so the
    // subsequent re-expand always starts from a known baseline (fixes missing
    // content after reorder and the related narrow-width disappearance bug).
    const QString animName = QStringLiteral("sectionBodyAnim");
    for (PanelSection &section : m_sections) {
        if (QWidget *body = section.body) {
            if (auto *anim = body->findChild<QPropertyAnimation *>(animName))
                anim->stop();
            body->setMaximumHeight(QWIDGETSIZE_MAX);
            body->setVisible(!section.collapsed);
            if (!section.collapsed)
                applyResizableSectionHeight(section.id);
            body->updateGeometry();
        }
    }

    auto *layout = static_cast<QVBoxLayout *>(m_content->layout());
    while (layout->count())
        layout->takeAt(0);

    for (int i = 0; i < m_sectionOrder.size(); ++i) {
        PanelSection *section = sectionFor(m_sectionOrder[i]);
        if (!section)
            continue;
        if (i > 0)
            layout->addSpacerItem(m_interSectionGaps[i - 1]);
        layout->addWidget(section->header);
        layout->addSpacerItem(section->headerGap);
        if (auto *op = section->operation)
            layout->addWidget(op->widget());
        layout->addWidget(section->body);
    }
    layout->addStretch();
    updateInterSectionGaps();
    layout->activate(); // headers must be at final positions before releaseMouse() fires hover events
    settleContentLayout();
}

bool FilePropertiesPanel::applyResizableSectionHeight(SectionId sectionId)
{
    PanelSection *section = sectionFor(sectionId);
    if (!section)
        return false;
    SectionResizeState &state = section->resize;
    if (!state.target)
        return false;

    const int height = state.currentHeight > 0 ? state.currentHeight : state.minHeight;
    state.target->setFixedHeight(height);
    state.target->resize(state.target->width(), height);
    state.target->updateGeometry();

    if (QWidget *stack = state.target->parentWidget()) {
        stack->updateGeometry();
        if (QLayout *layout = stack->layout()) {
            layout->invalidate();
            layout->activate();
        }
    }

    QWidget *body = section->body;
    if (!body)
        return true;
    body->updateGeometry();
    if (QLayout *layout = body->layout()) {
        body->setMinimumHeight(0);
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        layout->invalidate();
        layout->activate();
        const int bodyHeight = layout->sizeHint().height();
        body->setFixedHeight(bodyHeight);
        body->resize(body->width(), bodyHeight);
        body->updateGeometry();
    }
    return true;
}

void FilePropertiesPanel::scheduleResizableSectionRepair(SectionId sectionId)
{
    PanelSection *section = sectionFor(sectionId);
    if (!section || !section->resize.target)
        return;

    auto repair = [this, sectionId]() {
        if (isSectionCollapsed(sectionId))
            return;
        applyResizableSectionHeight(sectionId);
        settleContentLayout();
        updateStickyHeader();
    };
    QTimer::singleShot(0, this, repair);
    QTimer::singleShot(16, this, repair);
}

void FilePropertiesPanel::resizeSection(SectionId sectionId, int dy)
{
    PanelSection *section = sectionFor(sectionId);
    if (!section)
        return;
    SectionResizeState &state = section->resize;
    if (!state.target)
        return;

    const int current = state.currentHeight > 0 ? state.currentHeight : state.target->height();
    const int maxHeight = qMax(state.minHeight, height() - kSectionHeaderHeight * 2);
    const int newHeight = qBound(state.minHeight, current + dy, maxHeight);
    if (newHeight == current)
        return;

    state.currentHeight = newHeight;
    applyResizableSectionHeight(sectionId);
    rebuildSectionLayout();

    settleContentLayout();
    updateStickyHeader();
}

void FilePropertiesPanel::repairExpandedSectionGeometry(SectionId sectionId)
{
    if (isSectionCollapsed(sectionId))
        return;

    PanelSection *section = sectionFor(sectionId);
    QWidget *body = section ? section->body : nullptr;
    if (!body)
        return;

    const QString animName = QStringLiteral("sectionBodyAnim");
    if (auto *anim = body->findChild<QPropertyAnimation *>(animName))
        if (anim->state() == QAbstractAnimation::Running)
            return;

    body->show();
    if (!applyResizableSectionHeight(sectionId)) {
        body->setMaximumHeight(QWIDGETSIZE_MAX);
        body->updateGeometry();
    }

    if (QWidget *parent = body->parentWidget()) {
        if (QLayout *layout = parent->layout()) {
            layout->invalidate();
            layout->activate();
        }
    }

    settleContentLayout();

    if (body->height() == 0)
        QTimer::singleShot(0, this, [this, sectionId]() {
            if (isSectionCollapsed(sectionId))
                return;
            if (PanelSection *section = sectionFor(sectionId)) {
                QWidget *body = section->body;
                if (!body)
                    return;
                body->show();
                if (!applyResizableSectionHeight(sectionId)) {
                    body->setMaximumHeight(QWIDGETSIZE_MAX);
                    body->updateGeometry();
                }
                if (QWidget *parent = body->parentWidget())
                    if (QLayout *layout = parent->layout()) {
                        layout->invalidate();
                        layout->activate();
                    }
                settleContentLayout();
            }
        });
}

void FilePropertiesPanel::onDragStarted(SectionId s, QPoint /*globalPos*/)
{
    m_draggedSection = s;
    m_draggingSection = true;

    for (PanelSection &section : m_sections)
        section.preDragCollapsed = section.collapsed;
    m_dragSectionsCollapsed = false;

    updateDropIndicator(QCursor::pos());
}

void FilePropertiesPanel::onDragMoved(QPoint globalPos)
{
    if (m_draggingSection) {
        collapseSectionsForDrag();
        updateDropIndicator(globalPos);
    }
}

void FilePropertiesPanel::onDragEnded(SectionId /*s*/, QPoint globalPos)
{
    const bool sectionsWereCollapsedForDrag = m_dragSectionsCollapsed;
    m_draggingSection = false;
    m_dragSectionsCollapsed = false;
    for (SectionId s : m_sectionOrder)
        if (PanelSection *section = sectionFor(s))
            section->header->setDragTarget(false);
    if (m_dropIndicator)
        m_dropIndicator->hide();

    // Find the section whose header the mouse is over, using the same logic as
    // updateDropIndicator.  Swap the dragged section with that target so the
    // dragged panel lands exactly where the user pointed, with the displaced
    // panel filling the gap — simpler and more predictable than an insert-index
    // approach, which has a large no-op zone around the middle panel.
    SectionId targetSection = m_sectionOrder.last();
    if (m_content && !m_sectionOrder.isEmpty()) {
        const int contentY = m_content->mapFromGlobal(globalPos).y();
        for (SectionId s : m_sectionOrder) {
            PanelSection *section = sectionFor(s);
            if (!section || !section->header)
                continue;
            const int bottom = section->header->mapTo(m_content, QPoint(0, 0)).y() + kSectionHeaderHeight;
            if (contentY < bottom) { targetSection = s; break; }
        }
    }

    const bool isNoOp = (targetSection == m_draggedSection);

    if (!isNoOp) {
        const int di = m_sectionOrder.indexOf(m_draggedSection);
        const int ti = m_sectionOrder.indexOf(targetSection);
        // Remove the dragged section then insert it at the target's original index.
        // When di < ti the removal shifts the array down by one, so inserting at the
        // original ti places the dragged item at exactly the target's old position,
        // with every other item sliding to fill the gap — no explicit adjustment needed.
        m_sectionOrder.removeAt(di);
        m_sectionOrder.insert(ti, m_draggedSection);
        rebuildSectionLayout();
    }

    if (!isNoOp) {
        // After a reorder: leave everything collapsed except the dragged panel,
        // and only re-expand that one if it was open before the drag started.
        // This avoids restoring animation state across a freshly-rebuilt layout.
        for (SectionId s : m_sectionOrder) {
            PanelSection *section = sectionFor(s);
            if (section)
                setSectionCollapsed(s, s == m_draggedSection ? section->preDragCollapsed : true, false);
        }
    } else if (sectionsWereCollapsedForDrag) {
        // No reorder: restore all panels to their pre-drag state with animation.
        for (SectionId s : m_sectionOrder) {
            if (PanelSection *section = sectionFor(s))
                if (!section->preDragCollapsed)
                    setSectionCollapsed(s, false);
        }
    }

    scheduleSectionHeaderHoverSync();
    updateStickyHeader();
}

void FilePropertiesPanel::collapseSectionsForDrag()
{
    if (m_dragSectionsCollapsed)
        return;

    m_dragSectionsCollapsed = true;
    bool anyWasExpanded = false;
    for (const PanelSection &section : m_sections)
        anyWasExpanded = anyWasExpanded || !section.preDragCollapsed;

    for (SectionId s : m_sectionOrder)
        setSectionCollapsed(s, true);

    QTimer::singleShot(anyWasExpanded ? 130 : 0, this, [this]() {
        if (m_draggingSection)
            updateDropIndicator(QCursor::pos());
    });
}

void FilePropertiesPanel::updateDropIndicator(QPoint globalPos)
{
    if (!m_draggingSection)
        return;

    for (SectionId s : m_sectionOrder)
        if (PanelSection *section = sectionFor(s))
            section->header->setDragTarget(false);

    if (!m_content || m_sectionOrder.isEmpty())
        return;

    const int contentY = m_content->mapFromGlobal(globalPos).y();
    SectionId target = m_sectionOrder.last();
    for (SectionId s : m_sectionOrder) {
        PanelSection *section = sectionFor(s);
        if (!section || !section->header)
            continue;
        SectionHeader *h = section->header;
        const int bottom = h->mapTo(m_content, QPoint(0, 0)).y() + kSectionHeaderHeight;
        if (contentY < bottom) {
            target = s;
            break;
        }
    }
    if (PanelSection *section = sectionFor(target))
        section->header->setDragTarget(true);
}

void FilePropertiesPanel::syncSectionHeaderHover()
{
    for (SectionId s : m_sectionOrder)
        if (PanelSection *section = sectionFor(s))
            section->header->syncHoverFromCursor();
}

void FilePropertiesPanel::scheduleSectionHeaderHoverSync()
{
    QTimer::singleShot(0, this, &FilePropertiesPanel::syncSectionHeaderHover);
    QTimer::singleShot(16, this, &FilePropertiesPanel::syncSectionHeaderHover);
    QTimer::singleShot(50, this, &FilePropertiesPanel::syncSectionHeaderHover);
}

void FilePropertiesPanel::requestSectionLayoutRefresh(SectionId sectionId)
{
    if (m_sectionLayoutRefreshPending || isSectionCollapsed(sectionId))
        return;

    m_sectionLayoutRefreshPending = true;
    QTimer::singleShot(0, this, &FilePropertiesPanel::performSectionLayoutRefresh);
    QTimer::singleShot(16, this, [this]() {
        performSectionLayoutRefresh();
        m_sectionLayoutRefreshPending = false;
    });
}

void FilePropertiesPanel::performSectionLayoutRefresh()
{
    if (!m_content || !m_content->layout())
        return;

    for (SectionId sectionId : std::as_const(m_sectionOrder)) {
        if (isSectionCollapsed(sectionId))
            continue;
        if (PanelSection *section = sectionFor(sectionId)) {
            if (section->body) {
                section->body->setMaximumHeight(QWIDGETSIZE_MAX);
                section->body->updateGeometry();
            }
            if (section->resize.target)
                applyResizableSectionHeight(sectionId);
        }
    }

    rebuildSectionLayout();
    settleContentLayout();
    updateStickyHeader();
}

void FilePropertiesPanel::settleContentLayout()
{
    if (!m_content || !m_content->layout())
        return;

    QLayout *contentLayout = m_content->layout();
    m_content->setUpdatesEnabled(false);
    contentLayout->invalidate();
    contentLayout->activate();
    const int targetHeight = qMax(m_scrollArea ? m_scrollArea->viewport()->height() : 0,
                                  contentLayout->sizeHint().height());
    m_content->resize(m_content->width(), targetHeight);
    contentLayout->setGeometry(m_content->rect());
    m_content->setUpdatesEnabled(true);
    m_content->updateGeometry();
    m_content->update();
}


SidePanelHost::SidePanelHost(HexView *hexView, QWidget *parent)
    : QWidget(parent), m_hexView(hexView)
{
    setAcceptDrops(true);
    setMinimumWidth(0);
    setMaximumWidth(0);
    hide();

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_resizeHandle = new SidePanelResizeGrip(this);
    m_resizeHandle->installEventFilter(this);
    layout->addWidget(m_resizeHandle);

    m_widthAnim = new QPropertyAnimation(this, "maximumWidth", this);
    m_widthAnim->setDuration(kFileInfoPaneAnimMs);
    m_widthAnim->setEasingCurve(QEasingCurve::OutCubic);
}

bool SidePanelHost::isOpen() const
{
    return m_panel != nullptr;
}

void SidePanelHost::toggle()
{
    if (m_panel) {
        closePanel();
        return;
    }

    openSection(FilePropertiesPanel::SectionId::Properties);
}

void SidePanelHost::openSection(FilePropertiesPanel::SectionId section)
{
    if (m_panel) {
        m_panel->showSection(section);
        if (!isVisible() || maximumWidth() < m_paneWidth)
            setExpanded(true);
        emit openChanged(true);
        return;
    }

    auto *panel = new FilePropertiesPanel(m_hexView, this);
    panel->setWindowFlags(Qt::Widget);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_panel = panel;
    if (auto *hostLayout = qobject_cast<QHBoxLayout *>(layout()))
        hostLayout->addWidget(panel);
    panel->show();

    connect(panel, &FilePropertiesPanel::closeRequested,
            this, &SidePanelHost::closePanel);

    panel->showSection(section);
    emit openChanged(true);
    setExpanded(true);
}

void SidePanelHost::closePanel()
{
    emit openChanged(false);
    setExpanded(false);
}

void SidePanelHost::refreshPanel()
{
    if (m_panel)
        m_panel->refresh();
}

void SidePanelHost::resetPanelForCurrentDocument()
{
    if (m_panel)
        m_panel->resetForCurrentDocument();
}

void SidePanelHost::setExpanded(bool expanded)
{
    if (!m_widthAnim)
        return;

    m_widthAnim->stop();
    m_widthAnim->disconnect();

    if (m_panel)
        m_panel->setPanelFullyOpened(false);

    setMinimumWidth(0);
    if (expanded)
        show();

    m_widthAnim->setEasingCurve(expanded ? QEasingCurve::OutCubic
                                         : QEasingCurve::InCubic);
    m_widthAnim->setStartValue(maximumWidth());
    m_widthAnim->setEndValue(expanded ? m_paneWidth : 0);

    if (expanded) {
        connect(m_widthAnim, &QPropertyAnimation::finished, this, [this]() {
            if (maximumWidth() == m_paneWidth) {
                setMinimumWidth(m_paneWidth);
                if (m_panel)
                    m_panel->setPanelFullyOpened(true);
            }
        }, Qt::SingleShotConnection);
    } else {
        connect(m_widthAnim, &QPropertyAnimation::finished, this, [this]() {
            if (maximumWidth() == 0) {
                if (m_panel) {
                    m_panel->deleteLater();
                    m_panel = nullptr;
                }
                hide();
            }
        }, Qt::SingleShotConnection);
    }

    m_widthAnim->start();
}

void SidePanelHost::setPaneWidth(int width)
{
    m_paneWidth = qBound(kFileInfoPaneMinWidth, width, kFileInfoPaneMaxWidth);
    setMinimumWidth(m_paneWidth);
    setMaximumWidth(m_paneWidth);
}

bool SidePanelHost::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != m_resizeHandle)
        return QWidget::eventFilter(obj, event);

    const auto type = event->type();
    if (type == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && isVisible()) {
            m_widthAnim->stop();
            m_resizing = true;
            m_resizeStartX = me->globalPosition().x();
            m_resizeStartWidth = width();
            if (m_resizeHandle)
                static_cast<SidePanelResizeGrip *>(m_resizeHandle)->setActive(true);
            m_resizeHandle->grabMouse();
            return true;
        }
    }

    if (type == QEvent::MouseMove && m_resizing) {
        auto *me = static_cast<QMouseEvent *>(event);
        const int delta = qRound(me->globalPosition().x() - m_resizeStartX);
        setPaneWidth(m_resizeStartWidth - delta);
        return true;
    }

    if ((type == QEvent::MouseButtonRelease || type == QEvent::UngrabMouse) && m_resizing) {
        if (m_resizeHandle)
            m_resizeHandle->releaseMouse();
        if (m_resizeHandle)
            static_cast<SidePanelResizeGrip *>(m_resizeHandle)->setActive(false);
        m_resizing = false;
        return type == QEvent::MouseButtonRelease;
    }

    return QWidget::eventFilter(obj, event);
}
