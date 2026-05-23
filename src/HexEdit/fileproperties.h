#ifndef FILEPROPERTIES_H
#define FILEPROPERTIES_H

#include <QDialog>

class HexView;
class QLabel;

class FilePropertiesPanel : public QDialog
{
    Q_OBJECT
public:
    explicit FilePropertiesPanel(HexView *hexView, QWidget *parent = nullptr);

signals:
    void closeRequested();

public slots:
    void refresh();

private:
    static QString formatSize(qulonglong bytes);

    HexView *m_hexView = nullptr;
    QLabel  *m_nameValue = nullptr;
    QLabel  *m_locationValue = nullptr;
    QLabel  *m_sizeValue = nullptr;
    QLabel  *m_stateValue = nullptr;
};

#endif // FILEPROPERTIES_H
