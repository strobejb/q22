#ifndef FILESTATS_BANNER_H
#define FILESTATS_BANNER_H

#include <QFrame>
#include <functional>

class QLabel;
class QPushButton;

namespace filestats {

class ActionBanner : public QFrame
{
public:
    explicit ActionBanner(const QString &buttonText,
                          const std::function<void()> &onClicked,
                          QWidget *parent = nullptr,
                          const std::function<void()> &onClose = {},
                          const QString &actionIconResourceName = "actions/help-about-symbolic");

    void setMessage(const QString &message);
    void setButtonText(const QString &text);

private:
    QLabel *m_message = nullptr;
    QPushButton *m_button = nullptr;
};

} // namespace filestats

#endif // FILESTATS_BANNER_H
