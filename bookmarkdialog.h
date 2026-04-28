//#ifndef BOOKMARKDIALOG_H
//#define BOOKMARKDIALOG_H

#pragma once

#include "theme.h"
#include <QDialog>
#include <QColor>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVector>

// ── AnnotationEdit ────────────────────────────────────────────────────────────
// QPlainTextEdit with a reserved bottom strip that permanently shows a QLabel
// (e.g. a byte-range summary) inside the frame in placeholder-text style.
//
// setViewportMargins() shrinks the editable viewport so that the cursor and
// typed text can never reach the strip.  The label sits as a sibling of the
// viewport, occupying exactly the reserved strip area.

class AnnotationEdit : public QPlainTextEdit
{
    Q_OBJECT
    static constexpr int kStripH = 22;   // height of the reserved strip in px

public:
    explicit AnnotationEdit(QWidget *parent = nullptr) : QPlainTextEdit(parent)
    {
        // Shrink the editable viewport, leaving a strip at the bottom.
        setViewportMargins(0, 0, 0, kStripH);

        m_label = new QLabel(this);
        m_label->setContentsMargins(4, 0, 4, 0);
        syncLabelStyle();
        repositionLabel();
    }

    void setAnnotation(const QString &text)
    {
        m_label->setText(text);
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QPlainTextEdit::resizeEvent(e);
        repositionLabel();
    }

    void changeEvent(QEvent *e) override
    {
        QPlainTextEdit::changeEvent(e);
        if (e->type() == QEvent::PaletteChange)
            syncLabelStyle();
    }

private:
    // Place the label in the bottom kStripH rows of contentsRect().
    void repositionLabel()
    {
        const QRect cr = contentsRect();
        m_label->setGeometry(cr.left(), cr.bottom() - kStripH + 1,
                             cr.width(), kStripH);
    }

    // Stylesheet beats palette inheritance: set both background and text colour
    // explicitly so the platform style cannot override them.
    void syncLabelStyle()
    {
        const QColor base = palette().color(QPalette::Base);
        const QColor text = palette().color(QPalette::Text);
        // Blend Base→Text at 55 %: always contrasts with the background in
        // both light mode (white Base / black Text → mid grey) and dark mode
        // (dark Base / light Text → lighter grey).
        auto lerp = [](int a, int b) { return a + (b - a) * 55 / 100; };
        const QColor annot(lerp(base.red(),   text.red()),
                           lerp(base.green(), text.green()),
                           lerp(base.blue(),  text.blue()));
        m_label->setStyleSheet(
            QString("QLabel { background-color: %1; color: %2; }")
                .arg(base.name(), annot.name()));
    }

    QLabel *m_label = nullptr;
};

namespace Ui { class BookmarkDialog; }

class BookmarkDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BookmarkDialog(QWidget *parent = nullptr);
    ~BookmarkDialog();

    void setOffset(quint64 offset);
    void setLength(quint64 length);
    void setForegroundColour(const QColor &fg);
    void setSwatchColours(const QVector<QColor> &colours);

    quint64 offset() const { return m_offset; }
    quint64 length() const { return m_length; }
    QString bookmarkName() const;
    QColor  foregroundColour()    const { return m_foreground; }
    QColor  selectedColour()      const;
    int     selectedColourIndex() const;

private slots:
    //void onAccepted();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void relayoutDynamicControls();
    void updateRangeLabel();

    Ui::BookmarkDialog *ui;
    quint64 m_offset = 0;
    quint64 m_length = 0;
    int     m_buttonTopGap = 0;
    QColor  m_foreground;
};

//#endif // BOOKMARKDIALOG_H
