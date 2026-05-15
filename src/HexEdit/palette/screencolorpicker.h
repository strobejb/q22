#ifndef SCREENCOLORPICKER_H
#define SCREENCOLORPICKER_H

#include <QColor>
#include <QList>
#include <QObject>

class QDBusPendingCallWatcher;
class QCursor;
class QTimer;
class QWidget;

#ifdef HEXEDIT_HAVE_DBUS
#include <QDBusObjectPath>
#endif

class ScreenColorPicker : public QObject
{
    Q_OBJECT
public:
    explicit ScreenColorPicker(QObject *parent = nullptr);

    void start(QWidget *owner, const QColor &cursorColor);
    void cancel();

signals:
    void colorHovered(const QColor &color);
    void colorPicked(const QColor &color);
    void cancelled();
    void activeChanged(bool active);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void startLivePick(const QColor &cursorColor);
    bool startPortalPick();
    void sampleLiveColor();
    void finish(bool picked);
    void createPickerOverlays(const QCursor &cursor);
    void destroyPickerOverlays();

#ifdef HEXEDIT_HAVE_DBUS
private slots:
    void portalPickStarted(QDBusPendingCallWatcher *watcher);
    void portalResponse(uint response, const QVariantMap &results);
#endif

private:
    QWidget *m_owner   = nullptr;
    QWidget *m_preview = nullptr;
    QTimer  *m_timer   = nullptr;
    QList<QWidget *> m_overlays;
    QColor   m_lastColor;
    bool     m_active     = false;
    bool     m_liveActive = false;
#ifdef HEXEDIT_HAVE_DBUS
    QDBusObjectPath m_requestPath;
#endif
};

#endif // SCREENCOLORPICKER_H
