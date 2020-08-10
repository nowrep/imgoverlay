#pragma once

#include <thread>
#include <mutex>
#include <unordered_map>

struct OverlayImage
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool visible = false;
    uint8_t *pixels = nullptr;
    void *to_free = nullptr;
};

class Control
{
public:
    explicit Control(const std::string &socketPath);
    ~Control();

    const std::unordered_map<uint8_t, OverlayImage> &images() const;

    void processSocket();

private:
    uint32_t processMsg(struct msg_struct *msg);
    uint32_t processCreateImageMsg(struct msg_struct *msg);
    uint32_t processUpdateImageMsg(struct msg_struct *msg);
    uint32_t processDestroyImageMsg(struct msg_struct *msg);

    static void run(Control *c);

    std::string m_socketPath;
    std::unordered_map<uint8_t, OverlayImage> m_images;

    std::thread m_thread;
    std::mutex m_mutex;
    bool m_quit = false;
    int m_client = -1;

    uint8_t *m_buffer = nullptr;
    size_t m_bufferPos = 0;
    size_t m_bufferAlloc = 5 * 1024 * 1024;

    uint32_t m_msgSize = 0;
    uint8_t m_msgSizePos = 0;
    uint8_t *m_msg = nullptr;
    size_t m_msgPos = 0;
};
