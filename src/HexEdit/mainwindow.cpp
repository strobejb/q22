#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "bookmarks/bookmarkstore.h"
#include "bookmarks/bookmarkcombo.h"
#include "HexView/hexview.h"
#include "HexView/seqbase.h"
#include "bookmarks/dlgbookmark.h"
#include "dialogs/dlgabout.h"
#include "dialogs/dlgcopyas.h"
#include "chrome/dialog-chrome.h"
#include "dialogs/dlgexport.h"
#include "dialogs/dlgimport.h"
#include "dialogs/dlgpastespecial.h"
#include "fileproperties.h"
#include "filestats/banner.h"
#include "panels/dockpanelhost.h"
#include "panels/findpanel.h"
#include "panels/gotopanel.h"
#include "combos/menucombobox.h"
#include "combos/datatypecombobox.h"
#include "palette/palettes.h"
#include "settings/preferences.h"
#include "settings/settings.h"
#include "statusbar.h"
#include "chrome/titlebar.h"
#include "theme.h"
#include "themepicker.h"
#include "bookmarks/bookmarkpopup.h"
#include <functional>
#include <QActionGroup>
#include <QShortcut>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <QApplication>
#include <QDataStream>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDevice>
#include <QMimeData>
#include <QAbstractButton>
#include <QComboBox>
#include <QPushButton>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QStyle>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWindow>
#include <QUrl>

namespace {

void jumpToBookmark(HexView *hv, int idx)
{
    if (!hv)
        return;
    const QList<Bookmark> &bms = hv->bookmarks();
    if (idx < 0 || idx >= bms.size())
        return;

    const Bookmark &bm = bms[idx];
    hv->expandBookmark(idx);
    hv->scrollCenterIfOffScreen(bm.offset, bm.length);
    hv->setCurSel(bm.offset + bm.length, bm.offset);
    hv->scrollHEnd();
    hv->setFocus();
}

int bookmarkAreaComboWidth(const DataTypeComboBox *combo)
{
    if (!combo)
        return 150;
    const int textW = QFontMetrics(combo->font()).horizontalAdvance(combo->displayText());
    constexpr int kIconW = 16;
    constexpr int kIconGap = 8;
    constexpr int kArrowW = 22;
    constexpr int kFrameAndSpare = 32;
    return qBound(132, textW + kIconW + kIconGap + kArrowW + kFrameAndSpare, 180);
}

void showBookmarkAreaPopup(HexView *hv, size_w referenceOffset, const QRect &globalRect, QWidget *owner)
{
    Q_UNUSED(referenceOffset);
    if (!hv || hv->bookmarks().isEmpty())
        return;

    auto *combo = new DataTypeComboBox(owner);
    combo->hide();
    BookmarkCombo::populate(combo, hv, BookmarkCombo::Mode::BookmarksOnly, /*swatches=*/true);
    combo->setFixedWidth(bookmarkAreaComboWidth(combo));
    QObject::connect(combo, &DataTypeComboBox::selectionChanged, combo, [hv, combo](int) {
        const QVariant data = combo->selectionData();
        if (data.isNull())
            return;
        jumpToBookmark(hv, data.toInt());
        combo->deleteLater();
    });
    QObject::connect(combo, &DataTypeComboBox::popupClosed, combo, &QObject::deleteLater);
    combo->popupAbove(globalRect);
}

} // namespace

static bool formatNeedsEndian(IMPEXP_FORMAT fmt)
{
    return fmt == FORMAT_CPP || fmt == FORMAT_ASM;
}


static QFileSystemWatcher *fileChangeWatcher(QObject *owner)
{
    return owner->findChild<QFileSystemWatcher *>(QStringLiteral("fileChangedWatcher"));
}

static QTimer *fileChangeTimer(QObject *owner)
{
    return owner->findChild<QTimer *>(QStringLiteral("fileChangedTimer"));
}

static QWidget *fileChangedBanner(QWidget *owner)
{
    return owner->findChild<QWidget *>(QStringLiteral("fileChangedBanner"));
}

static void refreshCursorUnderMouse(QWidget *window)
{
    QWidget *target = QApplication::widgetAt(QCursor::pos());
    if (!target || target->window() != window)
        return;

    const QPoint global = QCursor::pos();
    const QPoint local = target->mapFromGlobal(global);
    const QPoint windowLocal = window->mapFromGlobal(global);
    QEnterEvent enter(local, windowLocal, global);
    QApplication::sendEvent(target, &enter);

    QMouseEvent move(QEvent::MouseMove,
                     local,
                     windowLocal,
                     global,
                     Qt::NoButton,
                     QApplication::mouseButtons(),
                     QApplication::keyboardModifiers());
    QApplication::sendEvent(target, &move);
}

static void showFileChangedBanner(QWidget *window)
{
    if (window->property("watchedFilePath").toString().isEmpty())
        return;

    if (QWidget *banner = fileChangedBanner(window)) {
        banner->show();
        QPointer<QWidget> guard(window);
        QTimer::singleShot(0, window, [guard]() {
            if (guard)
                refreshCursorUnderMouse(guard);
        });
    }
    if (QTimer *timer = fileChangeTimer(window))
        timer->stop();
}

static void checkWatchedFileChanged(QWidget *window)
{
    const QString watchedPath = window->property("watchedFilePath").toString();
    if (watchedPath.isEmpty())
        return;

    const QFileInfo watchedInfo(watchedPath);
    const QDateTime watchedModified = window->property("watchedFileModified").toDateTime();
    if (!watchedInfo.exists() || watchedInfo.lastModified() != watchedModified)
        showFileChangedBanner(window);
}

static void updateWatchedFile(QWidget *window, const QString &path)
{
    if (QFileSystemWatcher *watcher = fileChangeWatcher(window)) {
        const QStringList watchedFiles = watcher->files();
        if (!watchedFiles.isEmpty())
            watcher->removePaths(watchedFiles);
    }

    window->setProperty("watchedFilePath", QString());
    window->setProperty("watchedFileModified", QDateTime());
    if (QWidget *banner = fileChangedBanner(window))
        banner->hide();
    if (QTimer *timer = fileChangeTimer(window))
        timer->stop();

    if (path.isEmpty())
        return;

    const QFileInfo info(path);
    if (!info.exists())
        return;

    const QString watchedPath = info.absoluteFilePath();
    window->setProperty("watchedFilePath", watchedPath);
    window->setProperty("watchedFileModified", info.lastModified());
    if (QFileSystemWatcher *watcher = fileChangeWatcher(window))
        watcher->addPath(watchedPath);
    if (QTimer *timer = fileChangeTimer(window))
        timer->start();
}

static bool importFormatNeedsAddress(IMPEXP_FORMAT fmt)
{
    return fmt == FORMAT_HEXDUMP || fmt == FORMAT_INTELHEX || fmt == FORMAT_SRECORD;
}

static uint dataFormatStyle(const QString &format)
{
    if (format == "dec") return HVS_FORMAT_DEC;
    if (format == "oct") return HVS_FORMAT_OCT;
    if (format == "bin") return HVS_FORMAT_BIN;
    return HVS_FORMAT_HEX;
}

static QString dataFormatKey(uint style)
{
    switch (style & HVS_FORMAT_MASK) {
    case HVS_FORMAT_DEC: return QStringLiteral("dec");
    case HVS_FORMAT_OCT: return QStringLiteral("oct");
    case HVS_FORMAT_BIN: return QStringLiteral("bin");
    default:             return QStringLiteral("hex");
    }
}

static int searchTypeToExportComboIndex(const QComboBox *combo, SEARCHTYPE st)
{
    const int idx = combo->findData(QVariant::fromValue(int(st)));
    return idx < 0 ? 0 : idx;
}

static bool restoreLastModifiedTime(const QString &path, const QDateTime &modified)
{
    if (!modified.isValid())
        return true;

    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return false;
    return file.setFileTime(modified, QFileDevice::FileModificationTime);
}

static QString localFileFromMimeData(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return {};

    for (const QUrl &url : mime->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty())
            return path;
    }
    return {};
}

static QString getThemedOpenFileName(QWidget *parent, const QString &caption)
{
    QFileDialog dlg(parent, caption);
    const bool useNativeFileDialogs = AppSettings::prefNativeFileDialogs();
    dlg.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogs);
    dlg.setAcceptMode(QFileDialog::AcceptOpen);
    dlg.setFileMode(QFileDialog::ExistingFile);
    if (!useNativeFileDialogs) {
        installThemedFileDialogComboPopups(&dlg);
        installDialogChrome(&dlg);
    }

    const int result = useNativeFileDialogs ? dlg.exec() : execCentered(&dlg);
    return result == QDialog::Accepted ? dlg.selectedFiles().value(0) : QString();
}

static QString getThemedSaveFileName(QWidget *parent, const QString &caption)
{
    QFileDialog dlg(parent, caption);
    const bool useNativeFileDialogs = AppSettings::prefNativeFileDialogs();
    dlg.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogs);
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    if (!useNativeFileDialogs) {
        installThemedFileDialogComboPopups(&dlg);
        installDialogChrome(&dlg);
    }

    const int result = useNativeFileDialogs ? dlg.exec() : execCentered(&dlg);
    return result == QDialog::Accepted ? dlg.selectedFiles().value(0) : QString();
}

#ifdef Q_OS_WIN
#include "chrome/windows-chrome.h"
#else
#include "chrome/linux-chrome.h"
#endif

static Qt::Edges edgesFromPos(const QPoint &pos, const QRect &rect, int margin) {
    Qt::Edges edges;
    if (pos.x() <= margin)                  edges |= Qt::LeftEdge;
    if (pos.x() >= rect.width()  - margin)  edges |= Qt::RightEdge;
    if (pos.y() <= margin)                  edges |= Qt::TopEdge;
    if (pos.y() >= rect.height() - margin)  edges |= Qt::BottomEdge;
    return edges;
}

static QCursor cursorForEdges(Qt::Edges edges) {
    bool l = edges & Qt::LeftEdge, r = edges & Qt::RightEdge;
    bool t = edges & Qt::TopEdge, b = edges & Qt::BottomEdge;
    if ((l && t) || (r && b))
        return Qt::SizeFDiagCursor;
    if ((r && t) || (l && b))
        return Qt::SizeBDiagCursor;
    if (l || r)
        return Qt::SizeHorCursor;
    if (t || b)
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

void MainWindow::setupPaletteWatcher()
{
    m_paletteWatcher = new QFileSystemWatcher(this);
    m_paletteReloadTimer = new QTimer(this);
    m_paletteReloadTimer->setSingleShot(true);
    m_paletteReloadTimer->setInterval(250);

    connect(m_paletteWatcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString &) { schedulePaletteReload(); });
    connect(m_paletteReloadTimer, &QTimer::timeout,
            this, &MainWindow::reloadWatchedPalettes);

    QDir().mkpath(paletteStorageDir());
    m_paletteWatcher->addPath(paletteStorageDir());
}

void MainWindow::schedulePaletteReload()
{
    if (m_paletteReloadTimer)
        m_paletteReloadTimer->start();
}

void MainWindow::reloadWatchedPalettes()
{
    const QString currentName = !m_currentPalette.name.isEmpty()
        ? m_currentPalette.name
        : AppSettings::prefPaletteName();
    if (currentName.isEmpty()) {
        if (m_prefsDialog)
            m_prefsDialog->refreshPalettes();
        return;
    }

    PaletteInfo info;
    if (reloadPalette(QDir(paletteStorageDir()), currentName, &info)) {
        m_currentPalette = info;
        applyUiPalette(info);
        applyPalette(m_hv, info);
        m_titleBar->refreshStylesheet();
#ifdef Q_OS_WIN
        if (m_useCustomTitleBar)
            updateWinChromeColors();
#endif
    }

    if (m_prefsDialog)
        m_prefsDialog->refreshPalettes();
}

void MainWindow::createPreferencesDialog()
{
    if (m_prefsDialog)
        return;

    m_prefsDialog = new PreferencesDialog(this);
    connect(m_prefsDialog, &PreferencesDialog::fontChanged,
            this, [this](const QFont &font) {
        m_hv->setFont(font, AppSettings::prefHorizSpacing(), AppSettings::prefLineSpacing());
    });
    connect(m_prefsDialog, &PreferencesDialog::fontSpacingChanged,
            this, [this](int hSpacing, int lineSpacing) {
        m_hv->setFont(m_hv->font(), hSpacing, lineSpacing);
    });
    connect(m_prefsDialog, &PreferencesDialog::nativeMenuChanged,
            this, [this](bool native) { applyMenuMode(!native); });
    connect(m_prefsDialog, &PreferencesDialog::nativeDialogsChanged,
            this, [this](bool) {
        QTimer::singleShot(0, this, [this] {
            if (m_prefsDialog) {
                m_prefsDialog->hide();
                m_prefsDialog->deleteLater();
                m_prefsDialog = nullptr;
            }
            createPreferencesDialog();
            m_prefsDialog->prepareShow();
            m_prefsDialog->show();
            m_prefsDialog->raise();
            m_prefsDialog->activateWindow();
        });
    });
    connect(m_prefsDialog, &PreferencesDialog::menuHighlightChanged,
            this, [this](bool) { applyAdwaitaTheme(static_cast<ColorScheme>(AppSettings::prefColorScheme())); });
    connect(m_prefsDialog, &PreferencesDialog::paletteSelected,
            this, [this](const PaletteInfo &info) {
        m_currentPalette = info;
        applyUiPalette(info);
        applyPalette(m_hv, info);
        m_titleBar->refreshStylesheet();
        AppSettings::setPrefPaletteName(info.name);
    });
    connect(m_prefsDialog, &PreferencesDialog::bookmarkStyleChanged,
            this, [this](uint mask, uint styles) {
        m_hv->setStyle(mask, styles);
    });
}

// ─── MainWindow ───────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    ui->menubar->setAcceptDrops(true);
    ui->menuView->menuAction()->setVisible(false);

    // On Windows use the Segoe MDL2 FolderOpen glyph (0xED25) so the icon
    // matches the monochrome Segoe style used throughout the title bar.
    // fromTheme() can return unexpected icons from Qt's built-in fallback theme.
#ifdef Q_OS_WIN
    {
        const bool   dark = palette().window().color().lightness() < 128;
        const QColor fg   = dark ? QColor("#ffffff") : QColor("#000000");
        QIcon icon = segoeIcon(0xED25, fg, 16);   // FolderOpen, slightly larger for menu
        if (icon.isNull())
            icon = QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon);
        ui->actionOpen->setIcon(icon);
    }
#else
    ui->actionOpen->setIcon(QIcon::fromTheme("document-open-symbolic"));
#endif

    setWindowTitle(QApplication::applicationDisplayName());

    // Custom title bar
    m_titleBar = new TitleBar(this);

    // Recent files submenu — attached to actionRecent before the hamburger
    // menu is built so the shared QAction already carries its submenu.
    m_recentMenu = new QMenu(this);
    themeMenu(m_recentMenu);
    ui->actionRecent->setMenu(m_recentMenu);
    updateRecentMenu();

    // Ctrl+O, R — open most recently used file
    auto *recentShortcut = new QShortcut(QKeySequence("Ctrl+Shift+R"), this);
    connect(recentShortcut, &QShortcut::activated, this, [this] {
        const QStringList files = AppSettings::recentFiles();
        if (!files.isEmpty())
            openFile(files.first());
    });

    // Build a standalone copy of the File menu that shares the same QAction
    // objects (so shortcuts and connections remain intact) but is not a child
    // of the QMenuBar, which would interfere with popup display.
    auto *fileMenu = new QMenu(m_titleBar);
    for (QAction *a : ui->menuFile->actions()) {
        if (a->isSeparator())
            fileMenu->addSeparator();
        else
            fileMenu->addAction(a);
    }
    m_titleBar->setHamburgerMenu(fileMenu);

    // Search menu — same standalone-copy pattern.
    auto *searchMenu = new QMenu(m_titleBar);
    for (QAction *a : ui->menuSearh->actions())
        if (a->isSeparator())
            searchMenu->addSeparator();
        else
            searchMenu->addAction(a);
    m_titleBar->setSearchMenu(searchMenu);

    // Theme picker — inserted at the top of both the native menubar Tools menu
    // and the titlebar viewMenu copy.
    const auto currentScheme = static_cast<ColorScheme>(AppSettings::prefColorScheme());
    auto themeCb = [this](ColorScheme s) {
        AppSettings::setPrefColorScheme(static_cast<int>(s));
        applyAdwaitaTheme(s);
        QTimer::singleShot(0, this, [this, s] {
            if (AppSettings::prefColorScheme() != static_cast<int>(s))
                return;
            if (!m_currentPalette.name.isEmpty()) {
                applyUiPalette(m_currentPalette);
                applyPalette(m_hv, m_currentPalette);
            }
            if (m_hv)
                m_hv->refreshWindow();
            if (m_titleBar)
                m_titleBar->refreshStylesheet();
#ifdef Q_OS_WIN
            if (m_useCustomTitleBar)
                updateWinChromeColors();
#endif
        });
    };
    auto *pickerAction = new ThemePickerAction(currentScheme, themeCb, this);

    ui->menuTools->insertAction(ui->menuTools->actions().first(), pickerAction);
    ui->menuTools->insertSeparator(ui->menuTools->actions().at(1));

    m_titleBar->viewMenu()->addAction(pickerAction);
    m_titleBar->viewMenu()->addSeparator();

    // Populate the view (right-side) menu with Tools actions.
    // Submenus need standalone QMenu copies (not children of the QMenuBar)
    // so Qt can display them as popups outside the menubar system.
    for (QAction *a : ui->menuTools->actions()) {
        if (a->isSeparator())
            m_titleBar->viewMenu()->addSeparator();
        else if (QMenu *sub = a->menu()) {
            auto *copy = new QMenu(sub->title(), m_titleBar);
            themeMenu(copy);
            for (QAction *sa : sub->actions())
                if (sa->isSeparator())
                    copy->addSeparator();
                else
                    copy->addAction(sa);
            m_titleBar->viewMenu()->addMenu(copy);
        } else {
            m_titleBar->viewMenu()->addAction(a);
        }
    }

    m_hv = new HexView(this);
    m_hv->setObjectName("HexView");
    m_hv->setFrameShape(QFrame::NoFrame);
    m_hv->setBookmarkButtonLayout({
        HexView::BookmarkButtonAction::Close,
        HexView::BookmarkButtonAction::None,
    });
    {
        // Base flags (always applied)
        uint mask   = HVS_RESIZEBAR | HVS_SHOWMODS | HVS_INVERTSELECTION |
                      HVS_ENABLEDRAGDROP | HVS_SEARCH_HIGHLIGHT_ALL;
        uint styles = HVS_RESIZEBAR | HVS_SHOWMODS | HVS_ENABLEDRAGDROP |
                      HVS_SEARCH_HIGHLIGHT_ALL;
        // Bookmark flags — restore persisted values
        mask   |= HVS_BOOKMARK_EXPAND_LONE | HVS_BOOKMARK_EXPAND_CURSOR |
                  HVS_BOOKMARK_NESTED | HVS_BOOKMARK_SELECTION_HIGHLIGHTS |
                  HVS_BOOKMARK_EXPAND_ALWAYS;
        if (AppSettings::prefBookmarkAutoExpand()) {
            styles |= HVS_BOOKMARK_EXPAND_LONE | HVS_BOOKMARK_EXPAND_CURSOR | HVS_BOOKMARK_EXPAND_ALWAYS;
        }
        if (AppSettings::prefBookmarkNested())              styles |= HVS_BOOKMARK_NESTED;
        if (AppSettings::prefBookmarkSelectionHighlights()) styles |= HVS_BOOKMARK_SELECTION_HIGHLIGHTS;
        m_hv->setStyle(mask, styles);
    }
    m_hv->setHexColour(HVC_HEXEVEN, QColor(0, 0, 255));
    m_hv->setHexColour(HVC_HEXODD,  QColor(0, 0, 128));
    m_hv->setStyle(HVS_FORMAT_MASK, dataFormatStyle(AppSettings::prefDataFormat()));
    m_hv->setGrouping(AppSettings::prefBytesPerGroup());
    m_hv->setLineLen(AppSettings::prefBytesPerLine());
    m_hv->setPadding(3, 3);
    setAcceptDrops(true);
    // Container: HexView fills available space; FindPanel sits flush above
    // the status bar, hidden until activated.
    auto *central = new QWidget(this);
    central->setAcceptDrops(true);
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);
    vlay->addWidget(m_titleBar);   // sits at the top of the content area in custom-titlebar mode
    // HexView updates its viewport cursor during hit-testing.  Give surrounding
    // chrome/panel widgets an explicit arrow cursor so stale I-beam/resize
    // cursors do not visually leak into non-text UI regions.
    m_titleBar->setCursor(Qt::ArrowCursor);
    m_titleBar->setAcceptDrops(true);
    m_titleHairline = new Hairline(central, Hairline::Edge::Bottom, m_titleBar);  // bgSource swapped in applyMenuMode
    m_titleHairline->setCursor(Qt::ArrowCursor);
    m_titleHairline->setAcceptDrops(true);
    vlay->addWidget(m_titleHairline);

    auto *contentRow = new QWidget(central);
    contentRow->setAcceptDrops(true);
    auto *contentLay = new QHBoxLayout(contentRow);
    contentLay->setContentsMargins(0, 0, 0, 0);
    contentLay->setSpacing(0);

    auto *hexColumn = new QWidget(contentRow);
    hexColumn->setAcceptDrops(true);
    auto *hexColumnLay = new QVBoxLayout(hexColumn);
    hexColumnLay->setContentsMargins(0, 0, 0, 0);
    hexColumnLay->setSpacing(0);

    auto *fileChanged = new filestats::ActionBanner(
        tr("Reload"),
        [this]() {
            const QString path = property("watchedFilePath").toString();
            if (!path.isEmpty() && maybeSave())
                openFile(path);
        },
        hexColumn,
        []{}
    );
    fileChanged->setObjectName(QStringLiteral("fileChangedBanner"));
    fileChanged->setMessage(tr("File has changed on disk"));
    hexColumnLay->addWidget(fileChanged);
    hexColumnLay->addWidget(m_hv, 1);
    contentLay->addWidget(hexColumn, 1);

    m_sidePanelHost = new SidePanelHost(m_hv, contentRow);
    contentLay->addWidget(m_sidePanelHost, 0);

    vlay->addWidget(contentRow, 1);
    m_bookmarkDialog = new BookmarkDialog(this);
    auto *dockPanelHost = new DockPanelHost(m_hv, central);
    dockPanelHost->setCursor(Qt::ArrowCursor);
    dockPanelHost->setAcceptDrops(true);
    m_findDialog = new FindPanel(dockPanelHost);
    m_findDialog->setObjectName("FindPanel");
    m_findDialog->setCursor(Qt::ArrowCursor);
    m_findDialog->setAcceptDrops(true);
    m_findDialog->setWindowFlags(Qt::Widget); // embedded panel — no native QWidgetWindow
    m_gotoDialog = new GotoPanel(m_hv, dockPanelHost);
    m_gotoDialog->setObjectName("GotoPanel");
    m_gotoDialog->setCursor(Qt::ArrowCursor);
    m_gotoDialog->setAcceptDrops(true);
    m_gotoDialog->setWindowFlags(Qt::Widget); // embedded panel — no native QWidgetWindow
    dockPanelHost->addPanel(m_findDialog);
    dockPanelHost->addPanel(m_gotoDialog);
    vlay->addWidget(dockPanelHost, 0);
    auto *statusHairline = new Hairline(central);
    statusHairline->setCursor(Qt::ArrowCursor);
    statusHairline->setAcceptDrops(true);
    vlay->addWidget(statusHairline);  // separator between content and status bar
    setCentralWidget(central);

    connect(m_titleBar, &TitleBar::fileInfoToggled,
            this, &MainWindow::toggleSidePanel);
    connect(m_sidePanelHost, &SidePanelHost::openChanged,
            m_titleBar, &TitleBar::setFileInfoPanelOpen);
    auto *fileWatcher = new QFileSystemWatcher(this);
    fileWatcher->setObjectName(QStringLiteral("fileChangedWatcher"));
    connect(fileWatcher, &QFileSystemWatcher::fileChanged,
            this, [this]() { showFileChangedBanner(this); });
    auto *fileChangeTimer = new QTimer(this);
    fileChangeTimer->setObjectName(QStringLiteral("fileChangedTimer"));
    fileChangeTimer->setInterval(1000);
    connect(fileChangeTimer, &QTimer::timeout,
            this, [this]() { checkWatchedFileChanged(this); });
    connect(ui->actionProperties, &QAction::triggered,
            this, [this]() { openSidePanelSection(FilePropertiesPanel::SectionId::Properties); });
    ui->actionChecksum->setEnabled(true);
    connect(ui->actionChecksum, &QAction::triggered,
            this, [this]() { openSidePanelSection(FilePropertiesPanel::SectionId::Checksums); });
    ui->actionStrings->setEnabled(true);
    connect(ui->actionStrings, &QAction::triggered,
            this, [this]() { openSidePanelSection(FilePropertiesPanel::SectionId::Strings); });

    // Build a standalone Edit menu for the HexView context menu, sharing the
    // same QActions so any connections added later apply automatically.
    auto *editMenu = new QMenu(this);
    themeMenu(editMenu);
    for (QAction *a : ui->menuEdit->actions()) {
        if (a->isSeparator()) {
            editMenu->addSeparator();
        } else if (QMenu *sub = a->menu()) {
            auto *copy = new QMenu(sub->title(), editMenu);
            themeMenu(copy);
            for (QAction *sa : sub->actions())
                if (sa->isSeparator()) copy->addSeparator();
                else                   copy->addAction(sa);
            editMenu->addMenu(copy);
        } else {
            editMenu->addAction(a);
        }
    }
    m_hv->setContextMenu(editMenu);
    connect(editMenu, &QMenu::aboutToShow, this, &MainWindow::updateEditActions);

    // Actions in nested submenus of the replaced QMenuBar lose their shortcut
    // registration when the menubar is hidden. Re-register them on the window.
    addAction(ui->actionBookmark_here);
    addAction(ui->actionSelect_All);
    addAction(ui->actionHighlight_All_Occurances);

    // View submenu: exclusive action groups (radio behaviour within each section)
    QActionGroup *fmtGroup = new QActionGroup(this);
    fmtGroup->setExclusive(true);
    for (QAction *a : {ui->actionHexadecimal_2, ui->actionDecimal_2,
                       ui->actionOctal_2, ui->actionBinary_2})
        fmtGroup->addAction(a);

    QActionGroup *sizeGroup = new QActionGroup(this);
    sizeGroup->setExclusive(true);
    for (QAction *a : {ui->action8_bit_Byte, ui->action16_bit_Word,
                       ui->action32_bit_Dword_2, ui->action64_bit_Qword})
        sizeGroup->addAction(a);

    switch (m_hv->getStyle(HVS_FORMAT_MASK)) {
    case HVS_FORMAT_DEC: ui->actionDecimal_2->setChecked(true); break;
    case HVS_FORMAT_OCT: ui->actionOctal_2->setChecked(true); break;
    case HVS_FORMAT_BIN: ui->actionBinary_2->setChecked(true); break;
    default:             ui->actionHexadecimal_2->setChecked(true); break;
    }

    switch (m_hv->getGrouping()) {
    case 1:  ui->action8_bit_Byte->setChecked(true); break;
    case 4:  ui->action32_bit_Dword_2->setChecked(true); break;
    case 8:  ui->action64_bit_Qword->setChecked(true); break;
    default: ui->action16_bit_Word->setChecked(true); break;
    }

    // Format → HexView::setStyle
    connect(ui->actionHexadecimal_2, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_hv->setStyle(HVS_FORMAT_MASK, HVS_FORMAT_HEX);
            AppSettings::setPrefDataFormat(dataFormatKey(HVS_FORMAT_HEX));
        }
    });
    connect(ui->actionDecimal_2, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_hv->setStyle(HVS_FORMAT_MASK, HVS_FORMAT_DEC);
            AppSettings::setPrefDataFormat(dataFormatKey(HVS_FORMAT_DEC));
        }
    });
    connect(ui->actionOctal_2, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_hv->setStyle(HVS_FORMAT_MASK, HVS_FORMAT_OCT);
            AppSettings::setPrefDataFormat(dataFormatKey(HVS_FORMAT_OCT));
        }
    });
    connect(ui->actionBinary_2, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_hv->setStyle(HVS_FORMAT_MASK, HVS_FORMAT_BIN);
            AppSettings::setPrefDataFormat(dataFormatKey(HVS_FORMAT_BIN));
        }
    });

    // Size → HexView::setGrouping
    connect(ui->action8_bit_Byte, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_hv->setGrouping(1);
            AppSettings::setPrefBytesPerGroup(1);
        }
    });
    connect(ui->action16_bit_Word, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_hv->setGrouping(2);
            AppSettings::setPrefBytesPerGroup(2);
        }
    });
    connect(ui->action32_bit_Dword_2, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_hv->setGrouping(4);
            AppSettings::setPrefBytesPerGroup(4);
        }
    });
    connect(ui->action64_bit_Qword, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_hv->setGrouping(8);
            AppSettings::setPrefBytesPerGroup(8);
        }
    });
    connect(m_hv, &HexView::lineLengthChanged, this, [](uint bytesPerLine) {
        AppSettings::setPrefBytesPerLine((int)bytesPerLine);
    });

    m_statusBar = new StatusBar(m_hv, ui->statusbar, this);
    ui->statusbar->setCursor(Qt::ArrowCursor);
    ui->statusbar->setAcceptDrops(true);
    ui->statusbar->setContentsMargins(0, 0, 0, 2);
    ui->statusbar->setSizeGripEnabled(false);

    // ── Edit menu ─────────────────────────────────────────────────────────────
    // Shortcuts not set in the .ui file are assigned here so they are also
    // registered on the window (and therefore work even when the menu is hidden).
    ui->actionC_ut->setShortcut(QKeySequence::Cut);
    ui->action_Copy->setShortcut(QKeySequence::Copy);
    // Paste / Delete already have Ctrl+V / Del in the .ui but we add the actions
    // to the window so the shortcuts fire even when the menu bar is hidden.
    addAction(ui->actionC_ut);
    addAction(ui->action_Copy);
    addAction(ui->action_Paste);
    addAction(ui->action_Delete);
    addAction(ui->actionUndo);
    addAction(ui->actionRedo);

    connect(ui->actionUndo,     &QAction::triggered, this, [this] { m_hv->undo();      });
    connect(ui->actionRedo,     &QAction::triggered, this, [this] { m_hv->redo();      });
    connect(ui->actionC_ut,     &QAction::triggered, this, [this] { m_hv->cut();       });
    connect(ui->action_Copy,    &QAction::triggered, this, [this] { m_hv->copy();      });
    connect(ui->action_Paste,   &QAction::triggered, this, [this] { m_hv->paste();     });
    connect(ui->action_Delete,  &QAction::triggered, this, [this] { m_hv->clear();     });
    connect(ui->actionSelect_All, &QAction::triggered, this, [this] { m_hv->selectAll(); });
    connect(ui->actionCopy_As,      &QAction::triggered, this, [this] { CopyAsDlg(m_hv, this); });
    connect(ui->actionPaste_Special,&QAction::triggered, this, [this] { HexPasteSpecialDlg(m_hv, this); });

    // Keep Edit action enabled-states in sync with HexView and clipboard state.
    connect(m_hv, &HexView::selectionChanged, this, [this](size_w, size_w) { updateEditActions(); });
    connect(m_hv, &HexView::contentChanged,   this, [this](size_w, size_w, uint) { updateEditActions(); });
    connect(m_hv, &HexView::contentChanged,   this, [this](size_w, size_w, uint) { refreshSidePanel(); });
    connect(m_hv, &HexView::editModeChanged,  this, [this](uint) {
        m_statusBar->update();
        updateEditActions();
    });
    auto refreshClipboardState = [this]() {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        m_canPaste        = mime && (mime->hasFormat("application/x-hexview-snapshot")
                                     || mime->hasFormat("application/octet-stream")
                                     || mime->hasText());
        m_canPasteSpecial = mime && !mime->formats().isEmpty();
        updateEditActions();
    };
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, refreshClipboardState);
    connect(ui->menuEdit, &QMenu::aboutToShow, this, &MainWindow::updateEditActions);
    // Defer the initial clipboard check until the event loop is running so that
    // X11/Wayland clipboard queries can complete (they need event processing).
    QTimer::singleShot(0, this, refreshClipboardState);
    updateEditActions(); // set initial state (flags still false, but enables other actions)

    connect(m_hv, &HexView::cursorChanged, m_statusBar, &StatusBar::update);
    connect(m_hv, &HexView::selectionChanged, this,
            [this](size_w, size_w) { m_statusBar->update(); });
    connect(m_hv, &HexView::lengthChanged, this,
            [this](size_w) {
        m_statusBar->update();
    });
    connect(m_hv, &HexView::layoutChanged, this,
            [this]() {
        m_statusBar->update();
    });

    auto populateBookmarkDialog = [this]() {
        QVector<QColor> swatchColours;
        for (int i = 0; i < MAX_BOOKMARK_COLORS; ++i)
            swatchColours.append(QColor(m_hv->getHexColour(HvColorSlot(HVC_BOOKMARK1 + i))));
        m_bookmarkDialog->setSwatchColours(swatchColours);
        m_bookmarkDialog->setForegroundColour(m_hv->palette().text().color());
    };

    connect(ui->actionBookmark_here, &QAction::triggered, m_hv, &HexView::addBookmarkInline);

    auto editBookmark = [this, populateBookmarkDialog](int idx) {
        const QList<Bookmark> &bms = m_hv->bookmarks();
        if (idx < 0 || idx >= bms.size()) return;
        const Bookmark &existing = bms[idx];
        m_bookmarkDialog->setOffset(existing.offset);
        m_bookmarkDialog->setLength(existing.length);
        populateBookmarkDialog();
        m_bookmarkDialog->setEditMode(idx, existing.text, existing.colourIndex);
        if (execCentered(m_bookmarkDialog) == QDialog::Accepted) {
            Bookmark bm = existing;
            bm.text        = m_bookmarkDialog->bookmarkName();
            bm.colourIndex = qMax(0, m_bookmarkDialog->selectedColourIndex());
            m_hv->replaceBookmark(idx, bm);
        }
    };

    connect(m_hv, &HexView::bookmarkEditRequested, this, editBookmark);
    connect(m_hv, &HexView::bookmarkSettingsRequested, this,
            [editBookmark](int idx, QRect) { editBookmark(idx); });

    connect(m_bookmarkDialog, &BookmarkDialog::deleteRequested,
            this, [this](int idx) { m_hv->removeBookmark(idx); });

    connect(m_hv, &HexView::bookmarkContextRequested,
            this, [this](int idx, QRect btnGlobal) {
        showBookmarkContextPopup(m_hv, idx, btnGlobal);
    });
    connect(m_hv, &HexView::bookmarkAreaContextRequested,
            this, [this](size_w referenceOffset, QRect globalRect) {
        showBookmarkAreaPopup(m_hv, referenceOffset, globalRect, this);
    });
    m_hv->setBookmarkContextMenuExternallyHandled(true);

    connect(m_gotoDialog, &GotoPanel::bookmarkRequested, m_hv, &HexView::addBookmarkInline);

    connect(m_hv, &HexView::bookmarksChanged,
            m_gotoDialog, &GotoPanel::refreshBookmarks);

    connect(m_hv, &HexView::bookmarksChanged, this, [this]() {
        BookmarkStore::save(m_hv->filePath(), m_hv->bookmarks());
    });

    connect(m_hv, &HexView::paneFocusRequested, this, [this]() {
        if (m_findDialog->isVisible())
            m_findDialog->activate();
        else if (m_gotoDialog->isVisible())
            m_gotoDialog->activate();
    });

    connect(ui->actionFind, &QAction::triggered, this, [this]() {
        QWidget *fw = QApplication::focusWidget();
        if (m_findDialog->isVisible() && fw && m_findDialog->isAncestorOf(fw))
            m_findDialog->hide();
        else
            m_findDialog->activate({}, m_hv->activePane());
    });

    connect(ui->action_Goto, &QAction::triggered, this, [this]() {
        QWidget *fw = QApplication::focusWidget();
        if (m_gotoDialog->isVisible() && fw && m_gotoDialog->isAncestorOf(fw))
            m_gotoDialog->hide();
        else
            m_gotoDialog->activate();
    });

    connect(m_findDialog, &FindPanel::searchRequested, this,
            [this](const QByteArray &pattern, uint flags) {
                m_hv->findInit(reinterpret_cast<const uint8_t *>(pattern.constData()),
                               (size_t)pattern.size());
                execFind(pattern, flags);
            });

    connect(ui->actionFind_Next,          &QAction::triggered,  this, [this] { runFind(true);  });
    connect(ui->actionFind_Previous,      &QAction::triggered,  this, [this] { runFind(false); });
    connect(m_findDialog, &FindPanel::findNext,     this, [this] { runFind(true);  });
    connect(m_findDialog, &FindPanel::findPrevious, this, [this] { runFind(false); });

    connect(m_hv, &HexView::findProgress, m_statusBar, &StatusBar::onFindProgress);
#ifdef Q_OS_WIN
    connect(m_hv, &HexView::findProgress, this, [this](size_w, size_w, double) {
        if (m_findRunning && (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
            m_hv->cancelFind();
    });
#endif
    connect(m_findDialog, &FindPanel::searchHexChanged, m_statusBar, &StatusBar::showSearchHex);
    connect(m_findDialog, &FindPanel::highlightAllOccurrencesChanged,
            this, [this](bool on) {
        m_hv->setStyle(HVS_SEARCH_HIGHLIGHT_ALL, on ? HVS_SEARCH_HIGHLIGHT_ALL : 0);
    });

    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);

    connect(ui->actionSave, &QAction::triggered, this, [this]() {
        const QString path = m_hv->filePath();
        if (path.isEmpty()) {
            // No path yet — fall through to Save As behaviour.
            const QString dest = getThemedSaveFileName(this, tr("Save As"));
            if (dest.isEmpty()) return;
            if (!m_hv->saveFile(dest)) return;
            openFile(dest);         // reopen: clears undo, sets filePath, updates title/recent
        } else {
            if (!m_hv->saveFile(path)) return;
            openFile(path);         // reopen: clears undo, resets view state
        }
    });

    connect(ui->actionSave_As, &QAction::triggered, this, [this]() {
        static bool s_preserveLastModified = false;

        const QString sourcePath = m_hv->filePath();
        const QDateTime sourceModified = QFileInfo(sourcePath).lastModified();
        const bool canPreserveModified = !sourcePath.isEmpty() && sourceModified.isValid();
        const bool useNativeFileDialogs = AppSettings::prefNativeFileDialogs();

        QFileDialog dlg(this, tr("Save As"));
        dlg.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogs);
        dlg.setAcceptMode(QFileDialog::AcceptSave);
        if (!useNativeFileDialogs)
            installThemedFileDialogComboPopups(&dlg);

        QCheckBox *preserveModifiedCheck = nullptr;
        if (!useNativeFileDialogs) {
            preserveModifiedCheck = new QCheckBox(tr("Preserve last-modify time"), &dlg);
            preserveModifiedCheck->setEnabled(canPreserveModified);
            preserveModifiedCheck->setChecked(canPreserveModified && s_preserveLastModified);

            if (auto *grid = qobject_cast<QGridLayout *>(dlg.layout())) {
                int comboCol = 1, comboColSpan = 1;
                if (auto *ftCombo = dlg.findChild<QComboBox *>("fileTypeCombo")) {
                    int itemIdx = grid->indexOf(ftCombo);
                    if (itemIdx >= 0) {
                        int row, col, rspan, cspan;
                        grid->getItemPosition(itemIdx, &row, &col, &rspan, &cspan);
                        comboCol     = col;
                        comboColSpan = cspan;
                    }
                }
                int baseRow = grid->rowCount();
                grid->setRowMinimumHeight(baseRow, 8);
                baseRow++;
                grid->addWidget(preserveModifiedCheck, baseRow, comboCol, 1, comboColSpan);
            }
        }

        if (!useNativeFileDialogs)
            installDialogChrome(&dlg);

        if ((useNativeFileDialogs ? dlg.exec() : execCentered(&dlg)) != QDialog::Accepted)
            return;
        const QString dest = dlg.selectedFiles().value(0);
        if (dest.isEmpty())
            return;

        const bool preserveModified = preserveModifiedCheck && preserveModifiedCheck->isChecked();
        s_preserveLastModified = preserveModified;
        if (!m_hv->saveFile(dest)) return;
        if (preserveModified)
            restoreLastModifiedTime(dest, sourceModified);
        openFile(dest);             // reopen: clears undo, sets filePath, updates title/recent
    });

    connect(ui->actionNew, &QAction::triggered, this, [this]() {
        if (!maybeSave()) return;
        m_hv->clearFile();
        setWindowTitle(QApplication::applicationDisplayName());
        updateWatchedFile(this, QString());
        resetSidePanel();
    });

    connect(ui->actionOpen, &QAction::triggered, this, [this]() {
        const QString path = getThemedOpenFileName(this, tr("Open File"));
        if (!path.isEmpty())
            openFile(path);
    });

    connect(ui->actionImport, &QAction::triggered, this, [this]() {
        static const QStringList kImportFilters = {
            tr("Raw binary (*.*)"),
            tr("Plain text (*.txt)"),
            tr("Hex string (*.txt)"),
            tr("HTML (*.htm *.html)"),
            tr("C/C++ source (*.c *.cpp *.h)"),
            tr("Assembler source (*.asm *.s)"),
            tr("Intel Hex Records (*.hex)"),
            tr("Motorola S-Records (*.s19 *.s28 *.s37)"),
            tr("Base64 (*.b64 *.txt)"),
            tr("UUEncode (*.uue *.txt)"),
        };

        QFileDialog dlg(this, tr("Import"));
        const bool useNativeFileDialogs = AppSettings::prefNativeFileDialogs();
        dlg.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogs);
        dlg.setAcceptMode(QFileDialog::AcceptOpen);
        dlg.setFileMode(QFileDialog::ExistingFile);
        dlg.setNameFilters(kImportFilters);
        dlg.selectNameFilter(kImportFilters.value((int)g_ImportOptions.format));
        if (!useNativeFileDialogs)
            installThemedFileDialogComboPopups(&dlg);

        QCheckBox *bigEndianCheck = nullptr;
        QCheckBox *useAddressCheck = nullptr;

        if (!useNativeFileDialogs) {
            bigEndianCheck = new QCheckBox(tr("Big-endian byte order"), &dlg);
            bigEndianCheck->setChecked(g_ImportOptions.fBigEndian);
            useAddressCheck = new QCheckBox(tr("Use address information"), &dlg);
            useAddressCheck->setChecked(g_ImportOptions.fUseAddress);

            if (auto *grid = qobject_cast<QGridLayout *>(dlg.layout())) {
                int comboCol = 1, comboColSpan = 1;
                if (auto *ftCombo = dlg.findChild<QComboBox *>("fileTypeCombo")) {
                    int itemIdx = grid->indexOf(ftCombo);
                    if (itemIdx >= 0) {
                        int row, col, rspan, cspan;
                        grid->getItemPosition(itemIdx, &row, &col, &rspan, &cspan);
                        comboCol     = col;
                        comboColSpan = cspan;
                    }
                }
                int baseRow = grid->rowCount();
                grid->setRowMinimumHeight(baseRow, 8);
                baseRow++;
                grid->addWidget(bigEndianCheck,  baseRow,     comboCol, 1, comboColSpan);
                grid->addWidget(useAddressCheck, baseRow + 1, comboCol, 1, comboColSpan);
            }

            auto updateImportOptionsEnabled = [&]() {
                const int idx = kImportFilters.indexOf(dlg.selectedNameFilter());
                const auto fmt = idx >= 0 ? IMPEXP_FORMAT(idx) : g_ImportOptions.format;
                bigEndianCheck->setEnabled(formatNeedsEndian(fmt));
                useAddressCheck->setEnabled(importFormatNeedsAddress(fmt));
            };
            connect(&dlg, &QFileDialog::filterSelected, &dlg, updateImportOptionsEnabled);
            updateImportOptionsEnabled();
        }

        if (!useNativeFileDialogs)
            installDialogChrome(&dlg);

        if ((useNativeFileDialogs ? dlg.exec() : execCentered(&dlg)) != QDialog::Accepted) return;
        const QString path = dlg.selectedFiles().value(0);
        if (path.isEmpty()) return;

        const int idx = kImportFilters.indexOf(dlg.selectedNameFilter());
        if (idx >= 0)
            g_ImportOptions.format = (IMPEXP_FORMAT)idx;
        if (bigEndianCheck && useAddressCheck) {
            g_ImportOptions.fBigEndian = bigEndianCheck->isChecked();
            g_ImportOptions.fUseAddress = useAddressCheck->isChecked();
        }

        ImportFile(path, m_hv, &g_ImportOptions, this);
    });

    connect(ui->actionExport, &QAction::triggered, this, [this]() {
        // Filter order must match IMPEXP_FORMAT enum values (0–9)
        static const QStringList kExportFilters = {
            tr("Raw binary (*.*)"),
            tr("Plain text (*.txt)"),
            tr("Hex string (*.txt)"),
            tr("HTML (*.htm *.html)"),
            tr("C/C++ source (*.c *.cpp *.h)"),
            tr("Assembler source (*.asm *.s)"),
            tr("Intel Hex Records (*.hex)"),
            tr("Motorola S-Records (*.s19 *.s28 *.s37)"),
            tr("Base64 (*.b64 *.txt)"),
            tr("UUEncode (*.uue *.txt)"),
        };

        QFileDialog dlg(this, tr("Export"));
        const bool useNativeFileDialogs = AppSettings::prefNativeFileDialogs();
        dlg.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogs);
        dlg.setAcceptMode(QFileDialog::AcceptSave);
        dlg.setNameFilters(kExportFilters);
        dlg.selectNameFilter(kExportFilters.value((int)g_ExportOptions.format));
        if (!useNativeFileDialogs)
            installThemedFileDialogComboPopups(&dlg);

        MenuComboBox *dataTypeCombo = nullptr;
        QCheckBox *bigEndianCheck = nullptr;
        QCheckBox *appendCheck = nullptr;

        if (!useNativeFileDialogs) {
            auto *dataTypeLabel = new QLabel(tr("Data type:"), &dlg);
            dataTypeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            dataTypeCombo = new MenuComboBox(&dlg);
            dataTypeCombo->addItem(tr("8-bit Byte"), QVariant::fromValue(int(SEARCHTYPE_BYTE)));
            dataTypeCombo->addItem(tr("16-bit Word"), QVariant::fromValue(int(SEARCHTYPE_WORD)));
            dataTypeCombo->addItem(tr("32-bit Dword"), QVariant::fromValue(int(SEARCHTYPE_DWORD)));
            dataTypeCombo->addItem(tr("64-bit Qword"), QVariant::fromValue(int(SEARCHTYPE_QWORD)));
            dataTypeCombo->addItem(tr("Float (32-bit IEEE)"), QVariant::fromValue(int(SEARCHTYPE_FLOAT)));
            dataTypeCombo->addItem(tr("Double (64-bit IEEE)"), QVariant::fromValue(int(SEARCHTYPE_DOUBLE)));
            dataTypeCombo->setCurrentIndex(searchTypeToExportComboIndex(dataTypeCombo, g_ExportOptions.basetype));
            if (auto *ref = dlg.findChild<QWidget *>("fileNameEdit"))
                dataTypeCombo->setFixedHeight(ref->sizeHint().height());

            bigEndianCheck = new QCheckBox(tr("Big-endian byte order"), &dlg);
            bigEndianCheck->setChecked(g_ExportOptions.fBigEndian);
            appendCheck = new QCheckBox(tr("Append to file"), &dlg);
            appendCheck->setChecked(g_ExportOptions.fAppend);

            if (auto *grid = qobject_cast<QGridLayout *>(dlg.layout())) {
                // Determine column layout by inspecting the built-in "Files of type:" combo so
                // our label/combo/checkboxes share the same column edges.
                int labelCol = 0, comboCol = 1, comboColSpan = 1;
                if (auto *ftCombo = dlg.findChild<QComboBox *>("fileTypeCombo")) {
                    int itemIdx = grid->indexOf(ftCombo);
                    if (itemIdx >= 0) {
                        int row, col, rspan, cspan;
                        grid->getItemPosition(itemIdx, &row, &col, &rspan, &cspan);
                        labelCol     = col > 0 ? col - 1 : 0;
                        comboCol     = col;
                        comboColSpan = cspan;
                    }
                }
                int baseRow = grid->rowCount();
                grid->setRowMinimumHeight(baseRow, 8);
                baseRow++;
                grid->addWidget(dataTypeLabel,  baseRow,     labelCol);
                grid->addWidget(dataTypeCombo,  baseRow,     comboCol, 1, comboColSpan);
                grid->addWidget(bigEndianCheck, baseRow + 1, comboCol, 1, comboColSpan);
                grid->addWidget(appendCheck,    baseRow + 2, comboCol, 1, comboColSpan);
            }

            auto updateExportOptionsEnabled = [&, dataTypeLabel]() {
                const int idx = kExportFilters.indexOf(dlg.selectedNameFilter());
                const auto fmt = idx >= 0 ? IMPEXP_FORMAT(idx) : g_ExportOptions.format;
                const bool enable = formatNeedsEndian(fmt);
                dataTypeLabel->setEnabled(enable);
                dataTypeCombo->setEnabled(enable);
                bigEndianCheck->setEnabled(enable);
            };
            connect(&dlg, &QFileDialog::filterSelected, &dlg, updateExportOptionsEnabled);
            updateExportOptionsEnabled();
        }

        if (!useNativeFileDialogs)
            installDialogChrome(&dlg);

        if ((useNativeFileDialogs ? dlg.exec() : execCentered(&dlg)) != QDialog::Accepted)
            return;

        const QString path = dlg.selectedFiles().value(0);
        if (path.isEmpty())
            return;

        const int idx = kExportFilters.indexOf(dlg.selectedNameFilter());
        if (idx >= 0)
            g_ExportOptions.format = (IMPEXP_FORMAT)idx;
        if (dataTypeCombo && bigEndianCheck && appendCheck) {
            g_ExportOptions.basetype = SEARCHTYPE(dataTypeCombo->currentData().toInt());
            g_ExportOptions.fBigEndian = bigEndianCheck->isChecked();
            g_ExportOptions.fAppend = appendCheck->isChecked();
        }

        Export(path, m_hv, &g_ExportOptions, this);
    });

    createPreferencesDialog();
    connect(ui->actionPreferences, &QAction::triggered, this, [this]() {
        createPreferencesDialog();
        m_prefsDialog->prepareShow(); // pre-create HWND at correct pos before show()
        m_prefsDialog->show();
        m_prefsDialog->raise();
        m_prefsDialog->activateWindow();
    });
    connect(ui->actionAbout, &QAction::triggered, this, [this]() {
        ShowAboutDlg(this);
    });

    // Apply configured font, or Qt's platform fixed font when no family is set.
    m_hv->setFont(AppSettings::hexFont(),
                  AppSettings::prefHorizSpacing(), AppSettings::prefLineSpacing());

    // Apply saved palette
    const QString savedPalette = AppSettings::prefPaletteName();
    if (!savedPalette.isEmpty()) {
        QList<PaletteInfo> palettes = reloadPalettes(QDir(paletteStorageDir()));
        for (const PaletteInfo &info : palettes) {
            if (info.name == savedPalette) {
                m_currentPalette = info;
                applyUiPalette(info);
                applyPalette(m_hv, info);
                m_titleBar->refreshStylesheet();
                break;
            }
        }
    }
    setupPaletteWatcher();

    // Apply rounded corners / DWM shadow / compact style to the native QMenuBar
    // drop-down menus so they match the custom-titlebar menus.  Must happen
    // before first show so the window flags take effect.
    {
        std::function<void(QMenu *)> themeRecursive = [&](QMenu *m) {
            themeMenu(m);
            for (QAction *a : m->actions())
                if (QMenu *sub = a->menu())
                    themeRecursive(sub);
        };
        for (QAction *a : ui->menubar->actions())
            if (QMenu *m = a->menu())
                themeRecursive(m);
    }

    // Apply saved menu mode (may switch away from the default custom titlebar)
    applyMenuMode(!AppSettings::prefNativeMenu());

    if (AppSettings::prefRestoreWindowGeometry()) {
        const QByteArray ba = AppSettings::windowGeometry();
        if (!ba.isEmpty()) {
            QDataStream ds(ba);
            QRect r;
            ds >> r;
            const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
            if (ds.status() == QDataStream::Ok && r.width() >= 200 && r.height() >= 150
                    && screen.intersects(r))
                setGeometry(r);
        }
    }

    // Edge-resize event filter: catches mouse events on any child widget
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::updateEditActions()
{
    const bool hasSel     = m_hv->selectionSize() > 0;
    const bool insertMode = m_hv->editMode() == HVMODE_INSERT;
    const bool readOnly   = m_hv->editMode() == HVMODE_READONLY;

    // Undo / Redo reflect the sequence's event stack.
    ui->actionUndo->setEnabled(m_hv->canUndo());
    ui->actionRedo->setEnabled(m_hv->canRedo());

    // Cut requires a selection AND insert mode (overwrite can't remove bytes).
    ui->actionC_ut->setEnabled(hasSel && insertMode);

    // Delete: insert mode — always on; overwrite — selection required; read-only — off.
    ui->action_Delete->setEnabled(!readOnly && (insertMode || hasSel));

    // Copy / Copy As only need a selection.
    ui->action_Copy->setEnabled(hasSel);
    ui->actionCopy_As->setEnabled(hasSel);

    // Paste / Paste Special: use cached clipboard state (m_clipboardReady), updated
    // from dataChanged where clipboard queries are safe.  Querying mimeData() here
    // (from aboutToShow) can fail on X11/Wayland because the popup-window event
    // grab blocks the clipboard round-trip.
    const bool writable = m_hv->editMode() != HVMODE_READONLY;
    ui->action_Paste->setEnabled(writable && m_canPaste);
    ui->actionPaste_Special->setEnabled(writable && m_canPasteSpecial);
}

bool MainWindow::maybeSave()
{
    if (!m_hv->canUndo())
        return true;

    const QString name = m_hv->filePath().isEmpty()
                         ? tr("Untitled")
                         : QFileInfo(m_hv->filePath()).fileName();

    QMessageBox mb(QMessageBox::Warning,
                   tr("Unsaved changes"),
                   tr("Save changes to \"%1\"?").arg(name),
                   QMessageBox::NoButton,
                   this);
    QPushButton*saveBtn    = mb.addButton(tr("Save"),    QMessageBox::AcceptRole);
    QPushButton*discardBtn = mb.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    QPushButton*cancelBtn  = mb.addButton(tr("Cancel"),  QMessageBox::RejectRole);
    mb.setDefaultButton(saveBtn);
    styleMessageBox(&mb);

    mb.exec();
    QPushButton *clicked = qobject_cast<QPushButton*>(mb.clickedButton());

    if (!clicked || clicked == cancelBtn)
        return false;                       // Cancel (or dialog closed via Esc)

    if (clicked == saveBtn) {
        const QString path = m_hv->filePath();
        if (!path.isEmpty()) {
            // Real file on disk — overwrite directly
            if (!m_hv->saveFile(path))
                return false;
        } else {
            // Untitled / memory-backed — ask for a filename
            const QString dest = getThemedSaveFileName(this, tr("Save As"));
            if (dest.isEmpty())
                return false;
            if (!m_hv->saveFile(dest))
                return false;
        }
    }
    Q_UNUSED(discardBtn); // fall-through: discard changes and continue

    return true;
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    if (AppSettings::prefRestoreWindowGeometry()) {
        const QRect r = isMaximized() ? normalGeometry() : geometry();
        QByteArray ba;
        QDataStream ds(&ba, QIODevice::WriteOnly);
        ds << r;
        AppSettings::setWindowGeometry(ba);
    }
    QMainWindow::closeEvent(e);
}

void MainWindow::openFile(const QString &path) {
    m_hv->openFile(path);
    m_hv->setBookmarks(BookmarkStore::load(path));
    AppSettings::addRecentFile(path);
    updateRecentMenu();
    setWindowTitle(QFileInfo(path).fileName() + " \u2013 " + QApplication::applicationDisplayName());
    updateWatchedFile(this, path);
    resetSidePanel();
}

void MainWindow::toggleSidePanel()
{
    if (m_sidePanelHost)
        m_sidePanelHost->toggle();
}

void MainWindow::openSidePanelSection(FilePropertiesPanel::SectionId section)
{
    if (m_sidePanelHost)
        m_sidePanelHost->openSection(section);
}

void MainWindow::refreshSidePanel()
{
    if (m_sidePanelHost)
        m_sidePanelHost->refreshPanel();
}

void MainWindow::resetSidePanel()
{
    if (m_sidePanelHost)
        m_sidePanelHost->resetPanelForCurrentDocument();
}

void MainWindow::updateRecentMenu() {
    m_recentMenu->clear();
    const QStringList files = AppSettings::recentFiles();
    if (files.isEmpty()) {
        QAction *empty = m_recentMenu->addAction(tr("No recent files"));
        empty->setEnabled(false);
        return;
    }
    const QString home = QDir::homePath();
    for (const QString &path : files) {
        QString display =
            path.startsWith(home + '/') ? "~/" + path.mid(home.size() + 1) : path;
        QAction *a = m_recentMenu->addAction(display);
        a->setToolTip(path);
        connect(a, &QAction::triggered, this, [this, path] { openFile(path); });
    }
}

void MainWindow::execFind(const QByteArray &pattern, uint flags)
{
    if (m_findRunning) return;
    m_findRunning   = true;
    m_lastPattern   = pattern;
    m_lastFindFlags = flags & ~HVFF_BACKWARD; // strip direction; stored flags are always "forward"
    size_w result   = 0;
    uint searchFlags = flags;
    if (m_findDialog->isWrapAround())
        searchFlags |= HVFF_WRAP_AROUND;

    const HvFindResult findResult = m_hv->findNextEx(&result, searchFlags);
    if (findResult == HVFR_Found || findResult == HVFR_FoundWrapped) {
        m_hv->setCurSel(result, result + (size_t)pattern.size());
        m_hv->scrollTo(result);
        if (findResult == HVFR_FoundWrapped) {
            m_statusBar->showMessage((flags & HVFF_BACKWARD)
                ? tr("Search wrapped to end of file")
                : tr("Search wrapped to start of file"));
        } else {
            m_statusBar->showMessage({});
        }
    } else if (findResult == HVFR_NotFound) {
        if (m_findDialog->isWrapAround()) {
            m_statusBar->showMessage(tr("No matches found"));
        } else {
            m_statusBar->showMessage((flags & HVFF_BACKWARD)
                ? tr("Past start of file")
                : tr("Past end of file"));
        }
    } else {
        m_statusBar->showMessage({});
    }
    m_statusBar->onFindDone();
    m_findRunning   = false;
}

void MainWindow::runFind(bool forward)
{
    if (m_lastPattern.isEmpty()) {
        // No prior search — initialise from whatever is in the find field.
        const QByteArray pat = m_findDialog->buildPattern();
        if (pat.isEmpty())
            return;
        m_hv->findInit(reinterpret_cast<const uint8_t *>(pat.constData()),
                       (size_t)pat.size());
        execFind(pat, forward ? 0 : HVFF_BACKWARD);
        return;
    }
    uint flags = m_lastFindFlags | (forward ? 0 : HVFF_BACKWARD);
    execFind(m_lastPattern, flags);
}

void MainWindow::applyMenuMode(bool useCustomTitleBar)
{
    m_useCustomTitleBar = useCustomTitleBar;

    // TitleBar lives inside the central-widget layout; the standard QMenuBar
    // lives in its normal QMainWindow position.  We never swap menu widgets at
    // runtime (doing so causes Qt to reparent and orphan widgets, which crashes).
    // Instead we just show/hide each side and toggle the window frame flag.
    if (useCustomTitleBar) {
        ui->menubar->hide();
        m_titleBar->show();
        if (m_titleHairline) {
            m_titleHairline->show();
            m_titleHairline->setBgSource(m_titleBar);
        }
    } else {
        m_titleBar->hide();
        ui->menubar->show();
        if (m_titleHairline) {
            m_titleHairline->show();
            m_titleHairline->setBgSource(ui->menubar);
        }
    }

#ifndef Q_OS_WIN
    // Changing FramelessWindowHint requires the platform plugin to destroy and
    // recreate the native window.  On Linux (xcb / Wayland) doing this at
    // runtime while a child dialog is open causes a hard platform crash that
    // the C++ debugger cannot intercept.  Apply the flag only before the window
    // is first shown; at runtime the frame stays as-is and takes full effect on
    // the next launch.
    //
    // WA_TranslucentBackground must be set together with FramelessWindowHint:
    // it lets transparent corner pixels show through.  paintEvent() fills only
    // a rounded rect within the shadow margin, so the corner pixels stay
    // transparent — giving smooth antialiased corners on any ARGB compositor.
    if (!isVisible()) {
        setWindowFlag(Qt::FramelessWindowHint, useCustomTitleBar);
        setAttribute(Qt::WA_TranslucentBackground, useCustomTitleBar);
    }

    // KDE only: expand the shadow margin and manage the CornerClipper overlay.
    // On non-KDE (GNOME, …) the compositor provides the shadow — applyShadowMargin()
    // is a no-op there and we skip the CornerClipper entirely.
    applyShadowMargin();
    if (isKDE()) {
        if (useCustomTitleBar && !m_cornerClipper)
            m_cornerClipper = new CornerClipper(this);
        if (m_cornerClipper)
            m_cornerClipper->setVisible(useCustomTitleBar);
    }
#else
    // On Windows the window is never recreated; we just tell DWM to
    // re-evaluate the NC area so nativeEvent() starts or stops collapsing it.
    if (isVisible())
        SetWindowPos(reinterpret_cast<HWND>(winId()), nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
#endif
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w || w->window() != this)
        return false;

    const auto type = event->type();

    if (m_findRunning && type == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            m_hv->cancelFind();
            return true;
        }
    }

    auto isDropOnHexView = [this](const QPoint &pos) {
        if (!m_hv)
            return false;

        QWidget *target = QApplication::widgetAt(mapToGlobal(pos));
        return target && (target == m_hv || m_hv->isAncestorOf(target));
    };

    if (type == QEvent::DragEnter || type == QEvent::DragMove) {
        auto *de = static_cast<QDragMoveEvent *>(event);
        const QPoint windowPos = mapFromGlobal(w->mapToGlobal(de->position().toPoint()));
        if (isDropOnHexView(windowPos))
            return false;

        if (!localFileFromMimeData(de->mimeData()).isEmpty()) {
            de->acceptProposedAction();
            return true;
        }
        de->ignore();
        return true;
    }

    if (type == QEvent::Drop) {
        auto *de = static_cast<QDropEvent *>(event);
        const QPoint windowPos = mapFromGlobal(w->mapToGlobal(de->position().toPoint()));
        const QString path = localFileFromMimeData(de->mimeData());
        if (isDropOnHexView(windowPos))
            return false;

        if (!path.isEmpty()) {
            de->acceptProposedAction();
            if (maybeSave())
                openFile(path);
            return true;
        }
        de->ignore();
        return true;
    }

    // Edge-resize is handled by the custom titlebar only; the native window
    // frame takes care of it in standard menu mode.
    if (!m_useCustomTitleBar)
        return false;

    // ── Cursor feedback on hover ─────────────────────────────────────────────
    auto syncResizeCursor = [&](QPointF globalPos) {
        Qt::Edges edges =
            isMaximized()
            ? Qt::Edges{}
#ifndef Q_OS_WIN
            : edgesFromPos(mapFromGlobal(globalPos.toPoint()), rect(),
                           isKDE() ? kShadowSize : kResizeMargin);
#else
            : edgesFromPos(mapFromGlobal(globalPos.toPoint()), rect(), RESIZE_MARGIN);
#endif

        if (edges) {
            if (!m_inResizeZone) {
                m_inResizeZone = true;
                QApplication::setOverrideCursor(cursorForEdges(edges));
            } else {
                QApplication::changeOverrideCursor(cursorForEdges(edges));
            }
        } else if (m_inResizeZone) {
            m_inResizeZone = false;
            QApplication::restoreOverrideCursor();
        }
    };

    if (type == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        syncResizeCursor(me->globalPosition());
        return false; // don't consume — just update cursor
    }

    // Enter events fire regardless of mouse-tracking, so they catch transitions
    // to widgets like TitleBar buttons where MouseMove is never generated.
    // Without this, a stale override cursor persists after the mouse leaves a
    // resize-zone edge and enters a non-tracked widget.
    if (type == QEvent::Enter) {
        auto *ee = static_cast<QEnterEvent *>(event);
        syncResizeCursor(ee->globalPosition());
        return false;
    }

    // Restore cursor if mouse leaves the window entirely.
    // startSystemResize() calls ReleaseCapture() internally, which causes a
    // synthetic QEvent::Leave before the OS drag begins.  Guard against this by
    // checking that the cursor is physically outside the window before clearing
    // the override — if it's still inside, the Leave is spurious and we ignore it.
    if (type == QEvent::Leave && obj == this && m_inResizeZone) {
        if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
            m_inResizeZone = false;
            QApplication::restoreOverrideCursor();
        }
        return false;
    }

    // Clear any stale override cursor when the main window regains focus
    // (e.g. after a dialog closes without the mouse having left the resize zone).
    if (type == QEvent::WindowActivate && obj == this) {
        if (m_inResizeZone) {
            m_inResizeZone = false;
            QApplication::restoreOverrideCursor();
        }
        return false;
    }

    // ── Start resize on click at window edge ─────────────────────────────────
    if (type == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton || isMaximized())
            return false;

        Qt::Edges edges =
#ifndef Q_OS_WIN
            edgesFromPos(mapFromGlobal(me->globalPosition().toPoint()), rect(),
                         isKDE() ? kShadowSize : kResizeMargin);
#else
            edgesFromPos(mapFromGlobal(me->globalPosition().toPoint()), rect(),
                         RESIZE_MARGIN);
#endif
        if (!edges)
            return false;

        windowHandle()->startSystemResize(edges);
        return true;
    }

    return false;
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
#ifdef Q_OS_WIN
    if (m_useCustomTitleBar) {
        bool dark = palette().window().color().lightness() < 128;
        applyWindows11Styling(reinterpret_cast<HWND>(winId()), dark);
        updateWinChromeColors();
    }
    // Set icons directly from the Win32 ICON resource so DWM and the taskbar
    // get properly-rendered glyphs at native sizes rather than Qt downscaling
    // a single large PNG.  ICON_BIG drives the taskbar; ICON_SMALL drives the
    // titlebar (or the system-menu corner on classic chrome).
    /*{
        HWND hwnd = reinterpret_cast<HWND>(winId());
        HICON hBig = static_cast<HICON>(
            LoadImage(GetModuleHandle(nullptr), MAKEINTRESOURCE(1),
                      IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
        HICON hSmall = static_cast<HICON>(
            LoadImage(GetModuleHandle(nullptr), MAKEINTRESOURCE(1),
                      IMAGE_ICON,
                      GetSystemMetrics(SM_CXSMICON),
                      GetSystemMetrics(SM_CYSMICON),
                      LR_DEFAULTCOLOR | LR_SHARED));
        if (hBig)   SendMessage(hwnd, WM_SETICON, ICON_BIG,   reinterpret_cast<LPARAM>(hBig));
        if (hSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
    }*/
#else
    applyShadowMargin();
    // Force the CornerClipper to repaint after the initial layout pass so it
    // punches the corners on first display.  The deferred timer lets all child
    // widgets finish their first paint before we clear.
    if (m_cornerClipper) {
        m_cornerClipper->raise();
        QTimer::singleShot(0, m_cornerClipper, [this] {
            if (m_cornerClipper) { m_cornerClipper->raise(); m_cornerClipper->update(); }
        });
    }
#endif
}

// MainWindow::paintEvent and MainWindow::applyShadowMargin are
// implemented in chrome/linux-chrome.cpp (non-Windows builds only).

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::ActivationChange && m_useCustomTitleBar) {
        // Refresh title bar so it picks up the new focused/unfocused colour.
        m_titleBar->refreshStylesheet();
#ifdef Q_OS_WIN
        updateWinChromeColors();
#endif
    }
#ifndef Q_OS_WIN
    if (e->type() == QEvent::WindowStateChange && m_useCustomTitleBar) {
        applyShadowMargin();
        if (m_cornerClipper)
            m_cornerClipper->update();
    }
#endif
}

// MainWindow::updateWinChromeColors and MainWindow::nativeEvent are
// implemented in chrome/windows-chrome.cpp (Windows builds only).
