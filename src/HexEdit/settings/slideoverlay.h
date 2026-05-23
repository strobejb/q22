#ifndef SLIDEOVERLAY_H
#define SLIDEOVERLAY_H

#include <QMargins>
#include <QRect>
#include <QWidget>
#include <functional>

class QDialog;
class QPropertyAnimation;
class QToolButton;

// ── SlideOverlay ──────────────────────────────────────────────────────────────
// Full-size in-place panel that hosts a QDialog as an overlay over its parent,
// sliding in from the configured side when shown and back out when dismissed.
//
// The overlay owns a fixed 36 px header strip containing a back-chevron
// QToolButton; the hosted dialog fills the remaining area beneath it.
// The Cancel / Close button is hidden in any QDialogButtonBox found in the
// hosted dialog — the back button is the sole dismiss control.
//
// When the overlay opens the parent dialog is resized to accommodate the hosted
// dialog's size hint (plus the header).  The original parent size is restored
// after the slide-out animation completes.
//
// Call slideIn(dlg, callback) instead of dlg->exec():
//
//   auto *dlg = new MyDialog(this);
//   m_overlay->slideIn(dlg, [=](int result) {
//       if (result == QDialog::Accepted) { ... }
//       else { ... }
//   });
//
// The hosted dialog is owned by the overlay and deleted after slide-out.
// The onFinished callback is called with the dialog result code while the
// dialog is still alive, so its state can be read safely.

class SlideOverlay : public QWidget
{
    Q_OBJECT
public:
    enum class Direction { FromLeft, FromRight };

    explicit SlideOverlay(QWidget *parent);

    void setDirection(Direction direction);

    // Embed dlg and animate in.  dlg must not have been shown or exec()'d.
    // The overlay takes ownership of dlg.
    // resizeParent: when true (default) the parent dialog is resized to fit
    // the child's size hint and restored on close.  Pass false to leave the
    // parent at its current size (e.g. a credits panel inside a fixed dialog).
    void slideIn(QDialog *dlg, std::function<void(int)> onFinished = {},
                 bool resizeParent = true);

    // True while a dialog is hosted (animating in, open, or animating out).
    bool isActive() const;

    // Dismiss the hosted dialog immediately (equivalent to the back button).
    void dismiss();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void resizeParentToFit(const QSize &contentHint);
    void restoreParentSize();
    QWidget *resizeHostWidget() const;
    QMargins chromeMargins() const;
    QRect overlayRect() const;
    int chromeTopInset() const;
    void syncToParent();
    void startSlideIn();
    void startSlideOut();

    static constexpr int k_headerH = 36;   // height of the back-button strip
    static constexpr int k_duration = 220; // slide animation ms

    QPropertyAnimation *m_anim           = nullptr;
    QWidget            *m_slidePanel     = nullptr;
    QDialog            *m_content        = nullptr;
    QToolButton        *m_backBtn        = nullptr;
    QSize               m_savedParentSize;
    bool                m_inlineMode     = false; // back btn embedded in dialog header row
    bool                m_didResizeParent = false; // whether we resized the parent on open
    Direction           m_direction      = Direction::FromLeft;
};

#endif // SLIDEOVERLAY_H
