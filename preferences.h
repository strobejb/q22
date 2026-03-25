#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QAbstractButton>
#include <QDialog>
#include <QFont>

class QLabel;
class QListWidget;
class QDialogButtonBox;

// Font picker dialog: full list of monospace families/styles + live preview.
class FontPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FontPickerDialog(const QFont &current, QWidget *parent = nullptr);
    QFont selectedFont() const { return m_font; }

private:
    void updatePreview();

    QListWidget *m_list    = nullptr;
    QLabel      *m_preview = nullptr;
    QFont        m_font;
};

// Full-width toggle row: label text on the left, pill switch on the right.
// Drawn with system QPalette colours so it matches any theme.
class SettingsToggle : public QAbstractButton
{
    Q_OBJECT
public:
    explicit SettingsToggle(const QString &text, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
};

// Full-width spin row: label text on the left, value + [−][+] button pair on
// the right.  Drawn entirely in paintEvent (no child widgets) so it matches
// any theme via QPalette.
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

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

signals:
    void fontChanged(const QFont &font);          // family or size changed
    void fontSpacingChanged(int hSpacing, int lineSpacing);
    void nativeMenuChanged(bool on);

protected:
    void showEvent(QShowEvent *e) override;

private:
    QString         m_fontFamily;
    StepSpinBox    *m_fontSize     = nullptr;
    StepSpinBox    *m_horizSpacing = nullptr;
    StepSpinBox    *m_lineSpacing  = nullptr;
    SettingsToggle *m_nativeMenu   = nullptr;
};

#endif // PREFERENCES_H
