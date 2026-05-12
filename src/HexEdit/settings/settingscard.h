#ifndef SETTINGSCARD_H
#define SETTINGSCARD_H

#include <QAbstractButton>
#include <QIcon>
#include <QKeyEvent>
#include <QUrl>
#include <QWidget>

// ── SettingsCard ──────────────────────────────────────────────────────────────
// Rounded-rect card with drop shadow that hosts a vertical list of setting rows.
//
//  Compact  – rows sit edge-to-edge with 16 px side insets (About / list style).
//             Rows must carry their own vertical breathing room in sizeHint.
//  Spaced   – rows have 20 px side insets and 28 px gaps between them
//             (Preferences / form style).
//
// The card tracks hover and keyboard focus across its children and paints the
// highlight and focus ring itself; row widgets only need to paint their content.

class SettingsCard : public QWidget
{
    Q_OBJECT
public:
    enum class Style { Compact, Spaced };

    explicit SettingsCard(QList<QWidget *> rows, Style style = Style::Compact,
                          QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void paintEvent(QPaintEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    int   m_hoverIdx = -1;
    int   m_focusIdx = -1;
    Style m_style;
};

// ── NavigationRow ─────────────────────────────────────────────────────────────
// Clickable row with a label on the left, an optional right-aligned value, and
// a trailing icon (chevron or external-link).
//
// Two construction forms:
//
//   NavigationRow(label, NavigationRow::Icon::Next, parent)
//       Emits clicked().  Call setValueText() to show a secondary value.
//
//   NavigationRow(label, url, parent)
//       Loads the external-link icon and opens url when clicked.
class NavigationRow : public QAbstractButton
{
    Q_OBJECT
public:
    enum class Icon { Next, ExternalLink };

    // Chevron / next-page row.  Connect clicked() to your action.
    explicit NavigationRow(const QString &label, Icon icon = Icon::Next,
                           QWidget *parent = nullptr);

    // External-link row.  Opens url automatically when clicked.
    NavigationRow(const QString &label, const QUrl &url, QWidget *parent = nullptr);

    void    setValueText(const QString &v);
    QString valueText() const { return m_value; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_value;
    QIcon   m_icon;
};

// ── TextRow ───────────────────────────────────────────────────────────────────
// Non-interactive row showing plain text.  Same height as NavigationRow so it
// sits flush in a Compact card.
class TextRow : public QWidget
{
    Q_OBJECT
public:
    explicit TextRow(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString m_text;
};

// ── SettingsToggle ────────────────────────────────────────────────────────────
// Full-width toggle row: label on the left, pill switch on the right.
class SettingsToggle : public QAbstractButton
{
    Q_OBJECT
public:
    explicit SettingsToggle(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
};

// ── StepSpinBox ───────────────────────────────────────────────────────────────
// Full-width spin row: label + current value + [−][+] stepper buttons.
class StepSpinBox : public QWidget
{
    Q_OBJECT
public:
    explicit StepSpinBox(const QString &label, int min, int max,
                         int step = 1, QWidget *parent = nullptr);

    int  value() const { return m_value; }
    void setValue(int v);

    QSize sizeHint() const override;

signals:
    void valueChanged(int);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    enum HitZone { None, Minus, Plus };
    HitZone hitZone(const QPoint &pos) const;
    QRect   groupRect() const;
    QRect   minusRect() const;
    QRect   plusRect()  const;

    QString m_label;
    int     m_value = 0, m_min = 0, m_max = 99, m_step = 1;
    HitZone m_hover   = None;
    HitZone m_pressed = None;
};

// ── DangerButton ──────────────────────────────────────────────────────────────
// Full-width clickable row drawn in the system danger/red colour.
class DangerButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit DangerButton(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
};

#endif // SETTINGSCARD_H
