#include "screencolorpicker.h"

#include "theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <utility>

class PickerPreviewWidget : public QWidget
{
public:
    explicit PickerPreviewWidget()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(96, 38);
    }

    void update(const QColor &color, const QPoint &cursorPos)
    {
        m_color = color;
        // Position bottom-right of cursor, nudged onto screen if needed
        QScreen *screen = QGuiApplication::screenAt(cursorPos);
        if (!screen) screen = QGuiApplication::primaryScreen();
        const QRect avail = screen ? screen->geometry() : QRect();
        QPoint pos = cursorPos + QPoint(20, 16);
        if (!avail.isNull()) {
            if (pos.x() + width()  > avail.right())  pos.setX(cursorPos.x() - width()  - 8);
            if (pos.y() + height() > avail.bottom()) pos.setY(cursorPos.y() - height() - 8);
        }
        move(pos);
        QWidget::update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_color.isValid()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        p.setPen(QPen(QColor(30, 30, 30), 2.0));
        p.setBrush(m_color);
        p.drawRoundedRect(r, 7, 7);

        const QColor fg = m_color.lightness() > 128 ? Qt::black : Qt::white;
        p.setPen(fg);
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, m_color.name().toUpper());
    }

private:
    QColor m_color;
};

// Build a cursor pixmap with a contrasting outline so the dropper is visible
// against any background color. The outline color is chosen automatically:
// white for a dark cursor, black for a light one.
static QPixmap makePickerCursorPixmap(const QColor &color, int sz)
{
    const QColor outlineColor = color.lightness() > 128 ? Qt::black : Qt::white;
    const QPixmap fg = recoloredIcon(QStringLiteral("color-picker"), color,        sz).pixmap(sz, sz);
    const QPixmap bg = recoloredIcon(QStringLiteral("color-picker"), outlineColor, sz).pixmap(sz, sz);

    QPixmap result(fg.size());
    result.setDevicePixelRatio(fg.devicePixelRatio());
    result.fill(Qt::transparent);
    QPainter p(&result);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) p.drawPixmap(dx, dy, bg);
    p.drawPixmap(0, 0, fg);
    return result;
}

#ifdef HEXEDIT_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

ScreenColorPicker::ScreenColorPicker(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(40);
    connect(m_timer, &QTimer::timeout, this, &ScreenColorPicker::sampleLiveColor);
}

void ScreenColorPicker::start(QWidget *owner, const QColor &cursorColor)
{
    if (!owner) return;
    cancel();

    m_owner = owner->window();
    m_active = true;
    emit activeChanged(true);

    if (QGuiApplication::platformName().contains(QLatin1String("wayland"), Qt::CaseInsensitive)
        && startPortalPick())
        return;

    startLivePick(cursorColor);
}

void ScreenColorPicker::cancel()
{
    if (!m_active) return;
    finish(false);
}

bool ScreenColorPicker::eventFilter(QObject *obj, QEvent *event)
{
    if (!m_liveActive || (obj != m_owner && !m_overlays.contains(qobject_cast<QWidget *>(obj))))
        return QObject::eventFilter(obj, event);

    if (event->type() == QEvent::MouseButtonPress) {
        sampleLiveColor();
        finish(true);
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            finish(false);
            return true;
        }
    }
    return QObject::eventFilter(obj, event);
}

void ScreenColorPicker::startLivePick(const QColor &cursorColor)
{
    const QPixmap cursorPixmap = makePickerCursorPixmap(cursorColor, 24);
    const QCursor cursor(cursorPixmap, 5, 23);
    m_liveActive = true;
    m_owner->installEventFilter(this);
#ifdef Q_OS_WIN
    createPickerOverlays(cursor);
    if (!m_overlays.isEmpty()) {
        m_overlays.first()->grabMouse(cursor);
        m_overlays.first()->grabKeyboard();
    }
    QApplication::setOverrideCursor(cursor);
#endif
#ifndef Q_OS_WIN
    m_owner->grabMouse(cursor);
    QApplication::setOverrideCursor(cursor);
#endif
    m_preview = new PickerPreviewWidget();
    m_preview->show();
    m_timer->start();
    sampleLiveColor();
}

bool ScreenColorPicker::startPortalPick()
{
#ifdef HEXEDIT_HAVE_DBUS
    QDBusInterface iface(QStringLiteral("org.freedesktop.portal.Desktop"),
                         QStringLiteral("/org/freedesktop/portal/desktop"),
                         QStringLiteral("org.freedesktop.portal.Screenshot"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return false;

    static int requestCounter = 0;
    const QString token = QStringLiteral("hexedit_color_%1_%2")
                              .arg(QCoreApplication::applicationPid())
                              .arg(++requestCounter);
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);

    auto *watcher = new QDBusPendingCallWatcher(
        iface.asyncCall(QStringLiteral("PickColor"), QString(), options), this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &ScreenColorPicker::portalPickStarted);
    return true;
#else
    return false;
#endif
}

void ScreenColorPicker::sampleLiveColor()
{
    const QPoint globalPos = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QPoint localPos = globalPos - screen->geometry().topLeft();
        const QPixmap pixmap = screen->grabWindow(0, localPos.x(), localPos.y(), 1, 1);
        const QImage image = pixmap.toImage();
        if (!image.isNull()) {
            const QColor color = image.pixelColor(0, 0);
            if (color.isValid()) {
                m_lastColor = color;
                if (m_preview)
                    static_cast<PickerPreviewWidget *>(m_preview)->update(color, globalPos);
                emit colorHovered(color);
                return;
            }
        }
    }

    QWidget *widget = QApplication::widgetAt(globalPos);
    if (!widget)
        return;

    const QPoint widgetPos = widget->mapFromGlobal(globalPos);
    if (!widget->rect().contains(widgetPos))
        return;

    const QImage image = widget->grab(QRect(widgetPos, QSize(1, 1))).toImage();
    if (image.isNull())
        return;

    const QColor color = image.pixelColor(0, 0);
    if (color.isValid()) {
        m_lastColor = color;
        if (m_preview)
            static_cast<PickerPreviewWidget *>(m_preview)->update(color, globalPos);
        emit colorHovered(color);
    }
}

void ScreenColorPicker::finish(bool picked)
{
    const QColor pickedColor = m_lastColor;
    const bool hadActive = m_active;
    const bool hadLive = m_liveActive;

    m_timer->stop();
    if (m_owner) {
        m_owner->removeEventFilter(this);
#ifndef Q_OS_WIN
        if (hadLive)
            m_owner->releaseMouse();
#endif
    }
#ifdef Q_OS_WIN
    if (!m_overlays.isEmpty()) {
        m_overlays.first()->releaseKeyboard();
        m_overlays.first()->releaseMouse();
    }
    destroyPickerOverlays();
#endif
    if (hadLive)
        QApplication::restoreOverrideCursor();

    if (m_preview) {
        m_preview->hide();
        m_preview->deleteLater();
        m_preview = nullptr;
    }

    m_active = false;
    m_liveActive = false;
    m_owner = nullptr;
    emit activeChanged(false);

    if (picked && pickedColor.isValid())
        emit colorPicked(pickedColor);
    else if (hadActive)
        emit cancelled();
}

void ScreenColorPicker::createPickerOverlays(const QCursor &cursor)
{
#ifdef Q_OS_WIN
    destroyPickerOverlays();

    QRect virtualGeometry;
    for (QScreen *screen : QGuiApplication::screens()) {
        virtualGeometry = virtualGeometry.isNull()
                              ? screen->geometry()
                              : virtualGeometry.united(screen->geometry());
    }
    if (virtualGeometry.isNull())
        return;

    auto *overlay = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint |
                                         Qt::WindowStaysOnTopHint);
    // Do NOT use WA_TranslucentBackground: it uses per-pixel alpha compositing,
    // so fully-transparent pixels fail hit-testing and WM_SETCURSOR falls
    // through to the window behind, resetting the cursor to the arrow.
    // setWindowOpacity uses LWA_ALPHA instead — hit-testing is based on window
    // geometry only and ignores the alpha value, as long as alpha >= 1/255.
    // (alpha=0 is also skipped by Windows hit-testing, so 1/255 is the minimum.)
    overlay->setAttribute(Qt::WA_ShowWithoutActivating);
    overlay->setWindowOpacity(1.0 / 255.0);
    overlay->setCursor(cursor);
    overlay->setMouseTracking(true);
    overlay->installEventFilter(this);
    overlay->setGeometry(virtualGeometry);
    overlay->show();
    overlay->raise();
    if (QWindow *window = overlay->windowHandle())
        window->setFlag(Qt::WindowStaysOnTopHint, true);
    m_overlays.append(overlay);
#else
    Q_UNUSED(cursor);
#endif
}

void ScreenColorPicker::destroyPickerOverlays()
{
#ifdef Q_OS_WIN
    for (QWidget *overlay : std::as_const(m_overlays)) {
        if (!overlay)
            continue;
        overlay->removeEventFilter(this);
        overlay->hide();
        overlay->deleteLater();
    }
    m_overlays.clear();
#endif
}

#ifdef HEXEDIT_HAVE_DBUS
void ScreenColorPicker::portalPickStarted(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    watcher->deleteLater();
    if (!m_active)
        return;
    if (reply.isError()) {
        startLivePick(QApplication::palette().buttonText().color());
        return;
    }

    m_requestPath = reply.value();
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        m_requestPath.path(),
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"),
        this,
        SLOT(portalResponse(uint,QVariantMap)));
}

void ScreenColorPicker::portalResponse(uint response, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        m_requestPath.path(),
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"),
        this,
        SLOT(portalResponse(uint,QVariantMap)));

    if (!m_active)
        return;

    QColor color;
    const QVariant value = results.value(QStringLiteral("color"));
    if (response == 0 && value.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        double r = 0.0, g = 0.0, b = 0.0;
        arg.beginStructure();
        arg >> r >> g >> b;
        arg.endStructure();
        color = QColor::fromRgbF(qBound(0.0, r, 1.0),
                                 qBound(0.0, g, 1.0),
                                 qBound(0.0, b, 1.0));
    }

    m_lastColor = color;
    finish(color.isValid());
}
#endif
