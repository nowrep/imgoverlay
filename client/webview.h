#pragma once

#include "manager.h"
#include "groupconfig.h"

#include <QWebEngineView>

class QTimer;
class QOpenGLFramebufferObject;

class WebView : public QWebEngineView
{
    Q_OBJECT

public:
    explicit WebView(uint8_t id, const GroupConfig &conf, Manager *manager, QWidget *parent = nullptr);
    ~WebView();

private:
    bool eventFilter(QObject *o, QEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    QWebEngineView *createWindow(QWebEnginePage::WebWindowType) override;

    void initShm();
    void initMemory();
    void initDmaBuf();
    void sendCreateImage();

    uint8_t m_id = 0;
    GroupConfig m_conf;
    Manager *m_manager;

    QTimer *m_updateTimer;
    bool m_waitReply = false;

    int m_memfd = -1;
    void *m_memory = nullptr;
    uint32_t m_memsize = 0;
    uint32_t m_buffer = 0; // 0 - front, 1 - back

    int m_dmabufs[4] = {-1};
    int32_t m_format = 0;
    int32_t m_strides[4] = {0};
    int32_t m_offsets[4] = {0};
    uint64_t m_modifier = 0;
    int m_nfd = 0;
    void *m_eglImage = nullptr;
    QOpenGLFramebufferObject *m_fbo = nullptr;
};

class WebPage : public QWebEnginePage
{
    Q_OBJECT

private:
    void javaScriptConsoleMessage(QWebEnginePage::JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber, const QString &sourceID);
};
