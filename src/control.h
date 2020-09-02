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
    int memfd = -1;
    void *memory = nullptr;
    size_t memsize = 0;
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
    uint32_t processUpdateImageContentsMsg(struct msg_struct *msg);
    uint32_t processDestroyImageMsg(struct msg_struct *msg);
    uint32_t processDestroyAllImagesMsg(struct msg_struct *msg);

    void closeClient();
    void destroyImage(const OverlayImage &img);
    void destroyAllImages();

    std::string m_socketPath;
    std::unordered_map<uint8_t, OverlayImage> m_images;

    int m_client = -1;
    int m_server = -1;
    uint8_t m_waitingId = 0;
    bool m_waitingForFd = false;
};
