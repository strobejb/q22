#include "screencolorpicker.h"

#include "theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#ifdef QEXED_HAVE_DBUS
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

    m_owner = owner;
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
    if (!m_liveActive || obj != m_owner)
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
    const QPixmap cursorPixmap =
        recoloredIcon(QStringLiteral("color-picker"), cursorColor, 24).pixmap(24, 24);
    const QCursor cursor(cursorPixmap, 5, 23);
    m_liveActive = true;
    m_owner->installEventFilter(this);
    m_owner->grabMouse(cursor);
    QApplication::setOverrideCursor(cursor);
    m_timer->start();
    sampleLiveColor();
}

bool ScreenColorPicker::startPortalPick()
{
#ifdef QEXED_HAVE_DBUS
    QDBusInterface iface(QStringLiteral("org.freedesktop.portal.Desktop"),
                         QStringLiteral("/org/freedesktop/portal/desktop"),
                         QStringLiteral("org.freedesktop.portal.Screenshot"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid())
        return false;

    static int requestCounter = 0;
    const QString token = QStringLiteral("qexed_color_%1_%2")
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
        if (hadLive)
            m_owner->releaseMouse();
    }
    if (hadLive)
        QApplication::restoreOverrideCursor();

    m_active = false;
    m_liveActive = false;
    m_owner = nullptr;
    emit activeChanged(false);

    if (picked && pickedColor.isValid())
        emit colorPicked(pickedColor);
    else if (hadActive)
        emit cancelled();
}

#ifdef QEXED_HAVE_DBUS
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
