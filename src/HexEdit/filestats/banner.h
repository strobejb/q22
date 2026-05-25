#ifndef FILESTATS_BANNER_H
#define FILESTATS_BANNER_H

#include <QFrame>
#include <functional>

class QLabel;

namespace filestats {

class ActionBanner : public QFrame
{
public:
    explicit ActionBanner(const QString &buttonText,
                          const std::function<void()> &onClicked,
                          QWidget *parent = nullptr);

    void setMessage(const QString &message);

private:
    QLabel *m_message = nullptr;
};

} // namespace filestats

#endif // FILESTATS_BANNER_H
