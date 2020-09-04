#pragma once

#include <QObject>
#include <QVector>

#include "../src/control_prot.h"

class QTimer;
class QTabBar;
class QWidget;
class QLocalSocket;
class QSystemTrayIcon;
class QLabel;

class WebView;

class Manager : public QObject
{
    Q_OBJECT

public:
    explicit Manager(const QString &confFile, bool tray, QObject *parent = nullptr);
    ~Manager();

    bool isConnected() const;

    bool writeFd(int fd);
    bool writeMsg(struct msg_struct *msg);

Q_SIGNALS:
    void socketConnected();
    void socketDisconnected();
    void replyReceived(struct reply_struct *reply);

private:
    void initWebViews();
    void showView(int index);
    void updateStatus();

    QString m_confFile;
    QString m_socketPath;
    QLocalSocket *m_socket;
    QTimer *m_reconnectTimer;
    QVector<WebView*> m_views;

    QLabel *m_statusLabel;
    QTabBar *m_tabBar;
    QWidget *m_container;
    QWidget *m_window;
    QSystemTrayIcon *m_tray;
};
