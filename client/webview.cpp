#include "webview.h"
#include "manager.h"

#include <QTimer>
#include <QPaintEvent>
#include <QVBoxLayout>
#include <QDialog>
#include <QMenu>

#include <QQuickWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>

#define EGL_NO_X11
#include <EGL/egl.h>
#include <EGL/eglext.h>

WebView::WebView(uint8_t id, const GroupConfig &conf, Manager *manager, QWidget *parent)
    : QWebEngineView(parent)
    , m_id(id)
    , m_conf(conf)
    , m_manager(manager)
{
    setPage(new WebPage);

    page()->setBackgroundColor(Qt::transparent);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumSize(m_conf.width(), m_conf.height());
    setMaximumSize(m_conf.width(), m_conf.height());
    load(m_conf.url());

    QQuickWidget *w = qobject_cast<QQuickWidget*>(focusProxy());
    if (!m_manager->useShm() && w && w->quickWindow()) {
        QQuickWindow *window = w->quickWindow();
        connect(window, &QQuickWindow::sceneGraphInitialized, this, [=]() {
            connect(w->quickWindow(), &QQuickWindow::afterRendering, this, &WebView::initDmaBuf, Qt::DirectConnection);
            connect(m_manager, &Manager::socketConnected, this, [this]() {
                if (m_fbo) {
                    sendCreateImage();
                }
            });
        });
    } else {
        initShm();
    }

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
                m_updateTimer->start();
                return;
            }
            m_updateTimer->stop();

            uint8_t buffer = m_buffer;
            m_buffer = (m_buffer + 1) % 2;
            uchar *memory = (uchar*)m_memory + (PIXELS_SIZE(m_conf.width(), m_conf.height()) * buffer);
            QImage img(memory, m_conf.width(), m_conf.height(), QImage::Format_RGBA8888);
            img.fill(Qt::transparent);
            render(&img);

            char buf[MSG_BUF_SIZE];
            msg_struct *msg = (msg_struct*)buf;
            msg->type = MSG_UPDATE_IMAGE_CONTENTS;
            msg->update_image_contents.id = m_id;
            msg->update_image_contents.buffer = buffer;
            m_manager->writeMsg(msg);
            m_waitReply = true;
        });
    }
    return QWebEngineView::eventFilter(o, e);
}

void WebView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu;
    menu.addAction(pageAction(QWebEnginePage::Back));
    menu.addAction(pageAction(QWebEnginePage::Forward));
    menu.addAction(pageAction(QWebEnginePage::Reload));
    menu.addSeparator();
    menu.addAction(pageAction(QWebEnginePage::ViewSource));
    menu.addAction(tr("Inspect"), this, [=]() {
        if (page()->devToolsPage()) {
            triggerPageAction(QWebEnginePage::InspectElement);
        } else {
            QWebEngineView *view = createWindow(QWebEnginePage::WebDialog);
            view->page()->setInspectedPage(page());
        }
    });
    menu.exec(event->globalPos());
}

QWebEngineView *WebView::createWindow(QWebEnginePage::WebWindowType)
{
    QWebEngineView *view = new QWebEngineView;
    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(view);
    QDialog *dialog = new QDialog(parentWidget());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setLayout(layout);
    dialog->resize(parentWidget()->width() * 0.8, parentWidget()->height() * 0.8);
    dialog->show();
    return view;
}

void WebView::initShm()
{
    qDebug() << "Using SHM";

    initMemory();
    focusProxy()->installEventFilter(this);

    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(200);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() {
        focusProxy()->update();
    });

    connect(m_manager, &Manager::socketConnected, this, [this]() {
        sendCreateImage();
        m_waitReply = true;
        m_buffer = 0;
    });

    connect(m_manager, &Manager::socketDisconnected, this, [this]() {
        m_waitReply = false;
    });

    connect(m_manager, &Manager::replyReceived, this, [this](struct reply_struct *reply) {
        if (reply->id != m_id) {
            return;
        }
        m_waitReply = false;
    });
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

void WebView::initDmaBuf()
{
    QQuickWindow *w = qobject_cast<QQuickWindow*>(sender());
    Q_ASSERT(w);
    disconnect(w, &QQuickWindow::afterRendering, this, &WebView::initDmaBuf);

    if (!w->renderTarget()) {
        qCritical() << "No render target";
        QMetaObject::invokeMethod(this, &WebView::initShm, Qt::QueuedConnection);
        return;
    }

    EGLDisplay dpy = eglGetCurrentDisplay();
    m_eglImage = eglCreateImage(dpy, eglGetCurrentContext(), EGL_GL_TEXTURE_2D, reinterpret_cast<EGLClientBuffer>(w->renderTarget()->texture()), NULL);
    if (!m_eglImage) {
        qCritical() << "Failed to create EGL image";
        QMetaObject::invokeMethod(this, &WebView::initShm, Qt::QueuedConnection);
        return;
    }

    PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC eglExportDMABUFImageQueryMESA =
        (PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC) eglGetProcAddress("eglExportDMABUFImageQueryMESA");
    PFNEGLEXPORTDMABUFIMAGEMESAPROC eglExportDMABUFImageMESA =
        (PFNEGLEXPORTDMABUFIMAGEMESAPROC) eglGetProcAddress("eglExportDMABUFImageMESA");

    if (!eglExportDMABUFImageQueryMESA || !eglExportDMABUFImageMESA) {
        qCritical() << "Failed to resolve EGL functions";
        QMetaObject::invokeMethod(this, &WebView::initShm, Qt::QueuedConnection);
        eglDestroyImage(dpy, m_eglImage);
        return;
    }
    if (!eglExportDMABUFImageQueryMESA(dpy, m_eglImage, &m_format, &m_nfd, &m_modifier)) {
        qCritical() << "Failed to query DMABUF export";
        QMetaObject::invokeMethod(this, &WebView::initShm, Qt::QueuedConnection);
        eglDestroyImage(dpy, m_eglImage);
        return;
    }
    if (!eglExportDMABUFImageMESA(dpy, m_eglImage, m_dmabufs, m_strides, m_offsets)) {
        qCritical() << "Failed DMABUF export";
        QMetaObject::invokeMethod(this, &WebView::initShm, Qt::QueuedConnection);
        eglDestroyImage(dpy, m_eglImage);
        return;
    }

    m_fbo = w->renderTarget();
    QMetaObject::invokeMethod(this, [this]() {
        qDebug() << "Using DMA-BUF";
        if (m_manager->isConnected()) {
            sendCreateImage();
        }
    }, Qt::QueuedConnection);
}

void WebView::sendCreateImage()
{
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
    msg->create_image.format = m_format;
    msg->create_image.modifier = m_modifier;
    memcpy(msg->create_image.strides, m_strides, sizeof(m_strides));
    memcpy(msg->create_image.offsets, m_offsets, sizeof(m_offsets));

    int *fds = nullptr;
    if (m_memfd > 0) {
        msg->create_image.nfd = 1;
        msg->create_image.flip = 0;
        fds = &m_memfd;
    } else {
        msg->create_image.nfd = m_nfd;
        msg->create_image.flip = 1;
        fds = m_dmabufs;
    }
    m_manager->writeMsg(msg);
    m_manager->writeFds(fds, msg->create_image.nfd);
}

void WebPage::javaScriptConsoleMessage(QWebEnginePage::JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber, const QString &sourceID)
{
    Q_UNUSED(level)
    Q_UNUSED(message)
    Q_UNUSED(lineNumber)
    Q_UNUSED(sourceID)
}
