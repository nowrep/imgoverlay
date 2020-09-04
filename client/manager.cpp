#include "manager.h"
#include "webview.h"

#include <QDir>
#include <QTimer>
#include <QSettings>
#include <QApplication>
#include <QLocalSocket>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QTabBar>
#include <QVBoxLayout>
#include <QSystemTrayIcon>
#include <QLabel>

Manager::Manager(const QString &confFile, bool tray, QObject *parent)
    : QObject(parent)
    , m_settings(confFile.isEmpty() ? QDir::homePath() + QLatin1String("/.config/imgoverlayclient.conf") : confFile, QSettings::IniFormat)
{
    m_socketPath = m_settings.value(QStringLiteral("Socket"), QStringLiteral("/tmp/imgoverlay.socket")).toString();

    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::stateChanged, this, [this](QLocalSocket::LocalSocketState state) {
        updateStatus();
        if (state == QLocalSocket::ConnectedState) {
            QTimer::singleShot(0, this, &Manager::socketConnected);
        } else if (state == QLocalSocket::UnconnectedState) {
            emit socketDisconnected();
            m_reconnectTimer->start();
        }
    });
    connect(m_socket, &QLocalSocket::readyRead, this, [this]() {
        while (true) {
            QByteArray data = m_socket->read(REPLY_BUF_SIZE);
            if (data.isEmpty()) {
                return;
            }
            struct reply_struct *reply = reinterpret_cast<struct reply_struct*>(data.data());
            emit replyReceived(reply);
        }
    });

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(1000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [=]() {
        m_socket->connectToServer(m_socketPath);
    });

    QWebEngineProfile::defaultProfile()->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    QWebEngineProfile::defaultProfile()->setPersistentStoragePath(m_settings.value(QStringLiteral("Cache"), QStringLiteral("%1/.cache/imgoverlayclient").arg(QDir::homePath())).toString());
    QWebEngineSettings::defaultSettings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

    QVBoxLayout *layout = new QVBoxLayout;
    m_statusLabel = new QLabel;
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_tabBar = new QTabBar;
    connect(m_tabBar, &QTabBar::currentChanged, this, &Manager::showView);
    m_container = new QWidget;
    m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_tabBar);
    layout->addWidget(m_container);

    m_window = new QWidget;
    m_window->resize(600, 400);
    m_window->setLayout(layout);

    m_window->setAttribute(Qt::WA_DontShowOnScreen, tray);
    m_window->show();

    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(QIcon::fromTheme(QStringLiteral("image-x-generic")));
    m_tray->setToolTip(QStringLiteral("Imgoverlay Client"));
    m_tray->show();
    connect(m_tray, &QSystemTrayIcon::activated, this, [=](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            m_window->setAttribute(Qt::WA_DontShowOnScreen, !m_window->testAttribute(Qt::WA_DontShowOnScreen));
            m_window->hide();
            m_window->show();
        }
    });

    initWebViews();
    updateStatus();
    m_reconnectTimer->start();
}

Manager::~Manager()
{
    qDeleteAll(m_views);
}

bool Manager::isConnected() const
{
    return m_socket->state() == QLocalSocket::ConnectedState;
}

bool Manager::writeFd(int fd)
{
    if (!isConnected()) {
        qWarning() << "Not connected";
        return false;
    }

    struct msghdr msgh;
    struct iovec iov;
    union {
        struct cmsghdr cmsgh;
        // Space large enough to hold an 'int'
        char   control[CMSG_SPACE(sizeof(int))];
    } control_un;

    if (fd < 0) {
        qWarning() << "Cannot pass an invalid fd" << fd;
        return false;
    }

    // We must transmit at least 1 byte of real data in order
    // to send some other ancillary data.
    char placeholder = 'A';
    iov.iov_base = &placeholder;
    iov.iov_len = sizeof(char);

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = control_un.control;
    msgh.msg_controllen = sizeof(control_un.control);

    // Write the fd as ancillary data
    control_un.cmsgh.cmsg_len = CMSG_LEN(sizeof(int));
    control_un.cmsgh.cmsg_level = SOL_SOCKET;
    control_un.cmsgh.cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(CMSG_FIRSTHDR(&msgh))) = fd;

    int size = sendmsg(m_socket->socketDescriptor(), &msgh, 0);
    if (size < 0) {
        perror("sendsmg");
        return false;
    }
    return true;
}

bool Manager::writeMsg(struct msg_struct *msg)
{
    if (!isConnected()) {
        qWarning() << "Not connected";
        return false;
    }

    m_socket->write(reinterpret_cast<char*>(msg), MSG_BUF_SIZE);
    m_socket->flush();
    return true;
}

void Manager::initWebViews()
{
    uint8_t i = 1;
    const auto groups = m_settings.childGroups();
    for (const QString &group : groups) {
        GroupConfig conf(m_settings.fileName(), group);
        if (conf.url().isEmpty()) {
            qWarning() << "Invalid config" << group;
            continue;
        }
        WebView *view = new WebView(i++, conf, this);
        view->setParent(m_container);
        view->show();
        m_views.append(view);
        m_tabBar->addTab(group);
    }
    if (!m_views.isEmpty()) {
        showView(0);
    }
    qInfo() << "Loaded" << m_views.size() << "views";
}

void Manager::showView(int index)
{
    for (int i = 0; i < m_views.size(); ++i) {
        if (i == index) {
            m_views.at(i)->move(0, 0);
        } else {
            m_views.at(i)->move(9999, 9999);
        }
    }
}

void Manager::updateStatus()
{
    const QString s = isConnected() ? QStringLiteral("Connected") : QStringLiteral("Connecting...");
    m_statusLabel->setText(QStringLiteral("Socket: %1 | Status: %2").arg(m_socketPath, s));
}
