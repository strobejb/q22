#ifndef PREFERENCES_H
#define PREFERENCES_H

#include "palette/palettes.h"
#include "palette/paletteswatch.h"
#include "settingscard.h"
#include "slideoverlay.h"

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
    void refreshPalettes();

signals:
    void fontChanged(const QFont &font);
    void fontSpacingChanged(int hSpacing, int lineSpacing);
    void nativeMenuChanged(bool on);
    void nativeDialogsChanged(bool on);
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
    void openEditPaletteEditor(const PaletteInfo &info);
    void populateMainSwatches();
    bool hasActiveOverlay() const;
    SlideOverlay *activeOverlay() const;
    SlideOverlay *nextOverlay();

    QString         m_fontFamily;
    PaletteInfo     m_currentPalette;
    PaletteSwatchGrid *m_swatchGrid = nullptr;
    ViewMoreButton     *m_viewMore = nullptr;
    QList<PaletteInfo> m_palettes;
    bool            m_hiddenByModal      = false;
    QPoint          m_savedPos;
    NavigationRow  *m_fontNav      = nullptr;
    QWidget        *m_lastFocusWidget = nullptr;
    StepSpinBox    *m_fontSize     = nullptr;
    StepSpinBox    *m_horizSpacing = nullptr;
    StepSpinBox    *m_lineSpacing  = nullptr;
    SettingsToggle *m_nativeMenu       = nullptr;
    SettingsToggle *m_nativeDialogs    = nullptr;
    SettingsToggle *m_nativeFileDialogs = nullptr;
    SettingsToggle *m_menuHighlight    = nullptr;
    SlideOverlay   *m_overlay      = nullptr;
};

#endif // PREFERENCES_H
