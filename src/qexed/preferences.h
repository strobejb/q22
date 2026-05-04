#ifndef PREFERENCES_H
#define PREFERENCES_H

#include "palettes.h"
#include "settingscard.h"
#include "slideoverlay.h"

#include <QAbstractButton>

#include <memory>

class QButtonGroup;
class QFileSystemWatcher;
class QGridLayout;
class QWidget;
class ViewMoreButton;

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

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    // Call before show() on Windows to pre-create the HWND at the correct
    // position, preventing the 0,0 flash.  Idempotent — safe to call twice.
    void prepareShow();

signals:
    void fontChanged(const QFont &font);
    void fontSpacingChanged(int hSpacing, int lineSpacing);
    void nativeMenuChanged(bool on);
    void menuHighlightChanged(bool on);
    void paletteSelected(const PaletteInfo &info);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void setVisible(bool visible) override;

private:
    void addCustomSwatch(const PaletteInfo &);
    void rebuildCustomSwatches();
    void showPaletteListOverlay();
    void openAddPaletteEditor();
    void openEditPaletteEditor(const std::shared_ptr<PaletteInfo> &sharedInfo);
    PaletteSwatch *createPaletteSwatch(const PaletteInfo &info, QWidget *parent);
    void populateMainSwatches();
    // Move the keyboard cursor ring to idx; clears the ring on the old swatch.
    void syncCursorToSwatch(int idx);

    QString         m_fontFamily;
    PaletteInfo     m_currentPalette;
    QWidget        *m_swatchWidget = nullptr;
    QGridLayout    *m_swatchLayout = nullptr;
    QButtonGroup   *m_swatchGroup  = nullptr;
    QAbstractButton    *m_addBtn   = nullptr;
    ViewMoreButton     *m_viewMore = nullptr;
    QFileSystemWatcher *m_watcher  = nullptr;
    QList<PaletteInfo> m_palettes;
    int             m_swatchCount   = 0;
    int             m_swatchCursor  = 0;
    bool            m_hiddenByModal      = false;
    bool            m_suppressRingOnFocus = false;
    QPoint          m_savedPos;
    NavigationRow  *m_fontNav      = nullptr;
    StepSpinBox    *m_fontSize     = nullptr;
    StepSpinBox    *m_horizSpacing = nullptr;
    StepSpinBox    *m_lineSpacing  = nullptr;
    SettingsToggle *m_nativeMenu       = nullptr;
    SettingsToggle *m_menuHighlight    = nullptr;
    SlideOverlay   *m_overlay      = nullptr;
};

#endif // PREFERENCES_H
