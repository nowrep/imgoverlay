#pragma once

#include "manager.h"
#include "groupconfig.h"

#include <QWebEngineView>

class QTimer;

class WebView : public QWebEngineView
{
    Q_OBJECT

public:
    explicit WebView(uint8_t id, const GroupConfig &conf, Manager *manager, QWidget *parent = nullptr);
    ~WebView();

private:
    bool eventFilter(QObject *o, QEvent *e) override;
    QWebEngineView *createWindow(QWebEnginePage::WebWindowType) override;

    void initMemory();

    uint8_t m_id = 0;
    GroupConfig m_conf;
    Manager *m_manager;

    QTimer *m_updateTimer;
    bool m_waitReply = false;
    bool m_activated = false;

    int m_memfd = -1;
    void *m_memory = nullptr;
    uint32_t m_memsize = 0;
    uint32_t m_buffer = 0; // 0 - front, 1 - back
};
