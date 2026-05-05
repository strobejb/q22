#ifndef PALETTESWATCH_H
#define PALETTESWATCH_H

#include "palettes.h"

#include <QAbstractButton>
#include <QWidget>

class QButtonGroup;
class QGridLayout;

// Swatch size constants shared by PaletteSwatch and PaletteSwatchGrid.
inline constexpr int SW_SHADOW =  3;  // transparent margin for drop shadow
inline constexpr int SW_PAD_X  = 10;  // horizontal padding inside card
inline constexpr int SW_RADIUS = 10;
inline constexpr int SW_BORDER =  1;

// Checkable button that renders a colour preview for one palette.
// Constructed without a PaletteInfo it draws as an "Add palette" button instead.
class PaletteSwatch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit PaletteSwatch(const PaletteInfo &info, QWidget *parent = nullptr);
    explicit PaletteSwatch(QWidget *parent = nullptr);

    void setKeyboardCursor(bool on);

signals:
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    PaletteInfo m_info;
    bool m_addMode = false;
    bool m_keyboardCursor = false;
};

class PaletteSwatchGrid : public QWidget
{
    Q_OBJECT
public:
    explicit PaletteSwatchGrid(QWidget *parent = nullptr);

    void setPalettes(const QList<PaletteInfo> &palettes,
                     const QString &currentName,
                     int maxPaletteCards = -1);
    void setCurrentPaletteName(const QString &name);
    void setAllowFocusEscape(bool on) { m_allowFocusEscape = on; }
    void setGridContentsMargins(int left, int top, int right, int bottom);
    void focusCurrent(Qt::FocusReason reason = Qt::OtherFocusReason);

    int gridWidthForColumns(int columns) const;

    // Swatches are real Qt tab stops.  FocusNavigation uses this ordered list
    // to place the whole grid as one contiguous block in the dialog tab chain.
    QList<QWidget *> tabOrderWidgets() const;

signals:
    void paletteSelected(const PaletteInfo &info);
    void paletteEditRequested(const PaletteInfo &info);
    void addRequested();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void clear();
    void clearCursorRing();
    bool focusAdjacentControl(bool forward);
    bool handleTabKey(QKeyEvent *e);
    bool handleButtonKey(QKeyEvent *e);
    void setCursorIndex(int idx, bool showRing);
    void ensureButtonVisible(QAbstractButton *button);
    QList<QAbstractButton *> allButtons() const;
    int checkedIndex() const;

    QGridLayout *m_layout = nullptr;
    QButtonGroup *m_group = nullptr;
    PaletteSwatch *m_addBtn = nullptr;
    QList<QAbstractButton *> m_buttons;
    QList<PaletteInfo> m_paletteInfos;
    int m_columns = 3;
    int m_cursor = 0;
    bool m_allowFocusEscape = false;
};

#endif // PALETTESWATCH_H
