#include "control.h"
#include "overlay.h"
#include "mesa/util/os_socket.h"

#include <unistd.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <iostream>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x4000
#endif

enum status {
    STATUS_OK = 0,
    STATUS_ERROR = 1,
};

enum msg_type {
    MSG_INVALID             = 0xFFFFFFFF,
    MSG_CREATE_IMAGE        = 1,
    MSG_UPDATE_IMAGE        = 2,
    MSG_DESTROY_IMAGE       = 3,
};

struct msg_create_image {
    uint8_t id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_size; // width * height * 4 bytes
    uint8_t pixels[0];
};

struct msg_update_image {
    uint8_t id;
    uint32_t x;
    uint32_t y;
    uint32_t pixels_size; // width * height * 4 bytes
    uint8_t pixels[0];
};

struct msg_destroy_image {
    uint8_t id;
};

struct msg_struct {
    uint32_t type;
    union {
        msg_create_image create_image;
        msg_update_image update_image;
        msg_destroy_image destroy_image;
    };
};

Control::Control(const std::string &socketPath)
    : m_socketPath(socketPath)
{
    m_thread = std::thread(&Control::run, this);
}

Control::~Control()
{
    m_quit = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

const std::unordered_map<uint8_t, OverlayImage> &Control::images() const
{
    return m_images;
}

void Control::processSocket()
{
    m_mutex.lock();

    uint8_t *buf = m_buffer;
    size_t size = m_bufferPos;
    while (size > 0) {
        // Read message size
        if (m_msgSize == 0 || m_msgSizePos > 0) {
            size_t remain = std::min(sizeof(uint32_t) - m_msgSizePos, size);
            memcpy(&reinterpret_cast<uint8_t*>(&m_msgSize)[m_msgSizePos], buf, remain);
            m_msgSizePos += remain;
            buf += remain;
            size -= remain;
            if (m_msgSizePos != sizeof(uint32_t)) {
                continue;
            }
            m_msgSizePos = 0;
        }
        // XXX: Validate msg size
        if (!m_msg) {
            m_msg = static_cast<uint8_t*>(malloc(m_msgSize));
        }
        size_t remain = std::min(m_msgSize - m_msgPos, size);
        memcpy(&m_msg[m_msgPos], buf, remain);
        m_msgPos += remain;
        buf += remain;
        size -= remain;
        if (m_msgPos == m_msgSize) {
            uint32_t ret = processMsg(reinterpret_cast<struct msg_struct*>(m_msg));
            os_socket_send(m_client, &ret, sizeof(uint32_t), MSG_NOSIGNAL);
            m_msgSize = 0;
            m_msgPos = 0;
            m_msg = nullptr;
        }
    }
    m_bufferPos = 0;

    m_mutex.unlock();
}

uint32_t Control::processMsg(struct msg_struct *msg)
{
    switch (msg->type) {
    case MSG_CREATE_IMAGE:
        return processCreateImageMsg(msg);
    case MSG_UPDATE_IMAGE:
        return processUpdateImageMsg(msg);
    case MSG_DESTROY_IMAGE:
        return processDestroyImageMsg(msg);
    default:
        std::cerr << "Invalid msg type " << msg->type << std::endl;
        return STATUS_ERROR;
    }
}

uint32_t Control::processCreateImageMsg(struct msg_struct *msg)
{
    struct msg_create_image *m = &msg->create_image;

    if (m->pixels_size != (m->width * m->height * sizeof(uint32_t))) {
        std::cerr << "Invalid pixels_size: " << m->pixels_size << std::endl;
        free(msg);
        return STATUS_ERROR;
    }

    // XXX: Validate pixels_size <> m_msgSize

    auto it = m_images.find(m->id);
    if (it != m_images.end()) {
        std::cerr << "Already have image with id " << m->id << std::endl;
        return STATUS_ERROR;
    }

    OverlayImage img;
    img.x = m->x;
    img.y = m->y;
    img.width = m->width;
    img.height = m->height;
    img.pixels = m->pixels;
    img.to_free = msg;
    m_images.insert({m->id, img});

#ifndef NDEBUG
    std::cout << "::Create image " << (unsigned)m->id << std::endl;
#endif

    return STATUS_OK;
}

uint32_t Control::processUpdateImageMsg(struct msg_struct *msg)
{
    struct msg_update_image *m = &msg->update_image;

    auto it = m_images.find(m->id);
    if (it == m_images.end()) {
        std::cerr << "Unknown id " << m->id << std::endl;
        free(msg);
        return STATUS_ERROR;
    }

    if (m->pixels_size != (it->second.width * it->second.height * sizeof(uint32_t))) {
        std::cerr << "Invalid pixels_size: " << m->pixels_size << std::endl;
        free(msg);
        return STATUS_ERROR;
    }

    // XXX: Validate pixels_size <> m_msgSize

    free(it->second.to_free);
    it->second.x = m->x;
    it->second.y = m->y;
    it->second.pixels = m->pixels;
    it->second.to_free = msg;

#ifndef NDEBUG
    std::cout << "::Update image " << (unsigned)m->id << std::endl;
#endif

    return STATUS_OK;
}

uint32_t Control::processDestroyImageMsg(struct msg_struct *msg)
{
    struct msg_destroy_image *m = &msg->destroy_image;

    auto it = m_images.find(m->id);
    if (it == m_images.end()) {
        std::cerr << "Unknown id " << m->id << std::endl;
        free(msg);
        return STATUS_ERROR;
    }

    free(it->second.to_free);
    m_images.erase(it);

#ifndef NDEBUG
    std::cout << "::Destroy image " << (unsigned)m->id << std::endl;
#endif

    free(msg);
    return STATUS_OK;
}

// static
void Control::run(Control *c)
{
    unlink(c->m_socketPath.c_str());
    int listen_socket = os_socket_listen_abstract(c->m_socketPath.c_str(), 1);
    if (listen_socket < 0) {
        std::cerr << "Couldn't create socket at " << c->m_socketPath << ": " << strerror(errno) << std::endl;
        return;
    }
    os_socket_block(listen_socket, false);

    c->m_mutex.lock();
    c->m_buffer = new uint8_t[c->m_bufferAlloc];
    c->m_mutex.unlock();

    while (true) {
        if (c->m_quit) {
            break;
        }
        // Wait for client
        if (c->m_client < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            int socket = os_socket_accept(listen_socket);
            if (socket < 0) {
#ifndef NDEBUG
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED) {
                    std::cerr << "Socket error: " << strerror(errno) << std::endl;
                }
#endif
                continue;
            }
            os_socket_block(socket, false);
            c->m_mutex.lock();
            c->m_client = socket;
            c->m_mutex.unlock();
#ifndef NDEBUG
            std::cout << "Client connected" << std::endl;
#endif
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        c->m_mutex.lock();
        ssize_t n = os_socket_recv(c->m_client, c->m_buffer + c->m_bufferPos, c->m_bufferAlloc - c->m_bufferPos, MSG_NOSIGNAL);
        if (n > 0) {
            c->m_bufferPos += n;
        }
        c->m_mutex.unlock();
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
#ifndef NDEBUG
            if (errno != ECONNRESET) {
                std::cerr << "Socket recv error: " << strerror(errno) << std::endl;
            }
#endif
        }
        if (n <= 0) {
            c->m_mutex.lock();
            os_socket_close(c->m_client);
            c->m_client = -1;
            c->m_bufferPos = 0;
            c->m_msgSize = 0;
            c->m_msgSizePos = 0;
            free(c->m_msg);
            c->m_msg = nullptr;
            c->m_msgPos = 0;
#ifndef NDEBUG
            std::cout << "Client disconnected" << std::endl;
#endif
            c->m_mutex.unlock();
        }
    }

    delete[] c->m_buffer;
    os_socket_close(listen_socket);
}
