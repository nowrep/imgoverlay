#include "webview.h"
#include "manager.h"

#include <QTimer>
#include <QPaintEvent>
#include <QVBoxLayout>
#include <QDialog>

WebView::WebView(uint8_t id, const GroupConfig &conf, Manager *manager, QWidget *parent)
    : QWebEngineView(parent)
    , m_id(id)
    , m_conf(conf)
    , m_manager(manager)
{
    page()->setBackgroundColor(Qt::transparent);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumSize(m_conf.width(), m_conf.height());
    setMaximumSize(m_conf.width(), m_conf.height());
    load(m_conf.url());
    focusProxy()->installEventFilter(this);

    initMemory();

    connect(m_manager, &Manager::socketConnected, this, [this]() {
        char buf[MSG_BUF_SIZE];
        memset(buf, 0, MSG_BUF_SIZE);
        msg_struct *msg = (msg_struct*)buf;
        msg->type = MSG_CREATE_IMAGE;
        msg->create_image.id = m_id;
        msg->create_image.x = m_conf.x();
        msg->create_image.y = m_conf.y();
        msg->create_image.width = m_conf.width();
        msg->create_image.height = m_conf.height();
        msg->create_image.visible = 1;
        msg->create_image.memsize = m_memsize;
        m_manager->writeMsg(msg);
        m_manager->writeFd(m_memfd);
        m_waitReply = true;
    });

    connect(m_manager, &Manager::socketDisconnected, this, [this]() {
        m_waitReply = false;
    });

    connect(m_manager, &Manager::replyReceived, this, [this](struct reply_struct *reply) {
        if (reply->id != m_id) {
            return;
        }
        if (reply->msgtype == MSG_CREATE_IMAGE) {
            focusProxy()->update();
        }
        m_waitReply = false;
    });

    connect(this, &WebView::loadFinished, this, [this](bool ok) {
        if (!ok || m_conf.injectScript().isEmpty()) {
            return;
        }
        page()->runJavaScript(QStringLiteral("(function(){%1}());").arg(m_conf.injectScript()));
    });
}

WebView::~WebView()
{
    if (m_memory) {
        munmap(m_memory, m_memsize);
    }
    if (m_memfd >= 0) {
        ::close(m_memfd);
    }
}

bool WebView::eventFilter(QObject *o, QEvent *e)
{
    if (o == focusProxy() && e->type() == QEvent::Paint) {
        QTimer::singleShot(0, this, [=]() {
            if (m_waitReply || !m_manager->isConnected()) {
                return;
            }

            m_buffer = (m_buffer + 1) % 2;
            uchar *memory = (uchar*)m_memory + (PIXELS_SIZE(m_conf.width(), m_conf.height()) * m_buffer);
            QImage img(memory, m_conf.width(), m_conf.height(), QImage::Format_RGBA8888);
            img.fill(Qt::transparent);
            render(&img);

            char buf[MSG_BUF_SIZE];
            msg_struct *msg = (msg_struct*)buf;
            msg->type = MSG_UPDATE_IMAGE_CONTENTS;
            msg->update_image_contents.id = m_id;
            msg->update_image_contents.buffer = m_buffer;
            m_manager->writeMsg(msg);
            m_waitReply = true;
        });
    }
    return QWebEngineView::eventFilter(o, e);
}

QWebEngineView *WebView::createWindow(QWebEnginePage::WebWindowType)
{
    QWebEngineView *view = new QWebEngineView;
    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(view);
    QDialog *dialog = new QDialog(parentWidget());
    dialog->setLayout(layout);
    dialog->resize(parentWidget()->width() * 0.8, parentWidget()->height() * 0.8);
    dialog->show();
    return view;
}

void WebView::initMemory()
{
    m_memsize = PIXELS_SIZE(m_conf.width(), m_conf.height()) * 2;

    m_memfd = memfd_create("imgoverlay", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (m_memfd < 0) {
        perror("memfd_create");
        return;
    }

    if (ftruncate(m_memfd, m_memsize) < 0) {
        perror("ftruncate");
        ::close(m_memfd);
        m_memfd = -1;
        return;
    }

    m_memory = mmap(NULL, m_memsize, PROT_READ | PROT_WRITE, MAP_SHARED, m_memfd, 0);
    if (m_memory == MAP_FAILED) {
        perror("mmap");
        ::close(m_memfd);
        m_memfd = -1;
        return;
    }

    fcntl(m_memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW);
    fcntl(m_memfd, F_ADD_SEALS, F_SEAL_SEAL);
}
