#include "control.h"
#include "overlay.h"
#include "mesa/util/os_socket.h"

#include <unistd.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <sys/socket.h>
#include <linux/memfd.h>
#include <sys/mman.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x4000
#endif

enum status {
    STATUS_OK = 0,
    STATUS_ERROR = 1,
};

enum msg_type {
    MSG_INVALID                = 0xFFFFFFFF,
    MSG_CREATE_IMAGE           = 1,
    MSG_UPDATE_IMAGE           = 2,
    MSG_UPDATE_IMAGE_CONTENTS  = 3,
    MSG_DESTROY_IMAGE          = 4,
    MSG_DESTROY_ALL_IMAGES     = 5,
};

#define MSG_BUF_SIZE 32
#define REPLY_BUF_SIZE 16
#define PIXELS_SIZE(w, h) ((w) * (h) * sizeof(uint32_t))
#define MAX_MEM_SIZE 20 * 1024 * 1024

struct msg_create_image {
    uint8_t id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t visible;
    uint32_t memsize;
};

struct msg_update_image {
    uint8_t id;
    uint32_t x;
    uint32_t y;
    uint8_t visible;
};

struct msg_update_image_contents {
    uint8_t id;
    uint8_t buffer; // 0 - front, 1 - back
};

struct msg_destroy_image {
    uint8_t id;
};

struct msg_struct {
    uint32_t type;
    union {
        msg_create_image create_image;
        msg_update_image update_image;
        msg_update_image_contents update_image_contents;
        msg_destroy_image destroy_image;
    };
};

struct reply_struct {
    uint32_t status;
    uint32_t msgtype;
    uint8_t id;
    uint8_t buffer;
};

Control::Control(const std::string &socketPath)
    : m_socketPath(socketPath)
{
    unlink(m_socketPath.c_str());
    m_server = os_socket_listen_abstract(m_socketPath.c_str(), 1);
    if (m_server < 0) {
        std::cerr << "Couldn't create socket at " << m_socketPath << ": " << strerror(errno) << std::endl;
    } else {
        os_socket_block(m_server, false);
    }
}

Control::~Control()
{
    os_socket_close(m_server);
    m_server = -1;
}

const std::unordered_map<uint8_t, OverlayImage> &Control::images() const
{
    return m_images;
}

// https://github.com/a-darwish/memfd-examples
static int receive_fd(int socket)
{
    struct msghdr msgh;
    struct iovec iov;
    union {
        struct cmsghdr cmsgh;
        // Space large enough to hold an 'int' */
        char control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr *cmsgh;

    // The sender must transmit at least 1 byte of real data
    // in order to send some other ancillary data (the fd).
    char placeholder;
    iov.iov_base = &placeholder;
    iov.iov_len = sizeof(char);

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = control_un.control;
    msgh.msg_controllen = sizeof(control_un.control);

    int size = recvmsg(socket, &msgh, 0);
    if (size == -1) {
#ifndef NDEBUG
        std::cerr << "recvmsg error " << strerror(errno) << std::endl;
#endif
        return -1;
    }

    if (size != 1) {
        std::cerr << "Expected a placeholder message data of length 1, got " << size << std::endl;
        return -2;
    }

    cmsgh = CMSG_FIRSTHDR(&msgh);
    if (!cmsgh) {
        std::cerr << "Expected one recvmsg() header with a passed memfd fd. Got zero headers!" << std::endl;
        return -3;
    }

    if (cmsgh->cmsg_level != SOL_SOCKET) {
        std::cerr << "invalid cmsg_level " << cmsgh->cmsg_level << std::endl;
        return -4;
    }

    if (cmsgh->cmsg_type != SCM_RIGHTS) {
        std::cerr << "invalid cmsg_type " << cmsgh->cmsg_type << std::endl;
        return -5;
    }

    return *((int *) CMSG_DATA(cmsgh));
}

void Control::processSocket()
{
    if (m_server < 0) {
        return;
    }

    // Wait for client
    if (m_client < 0) {
        m_client = os_socket_accept(m_server);
        if (m_client >= 0) {
#ifndef NDEBUG
            std::cout << "Client connected" << std::endl;
#endif
            os_socket_block(m_client, false);
        } else {
#ifndef NDEBUG
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED) {
                std::cerr << "Socket error: " << strerror(errno) << std::endl;
            }
#endif
        }
        return;
    }

    // Read from client
    while (true) {
        // Get memfd
        if (m_waitingForFd) {
            m_waitingForFd = false;
            int fd = receive_fd(m_client);
            if (fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    m_waitingForFd = true;
                    return;
                }
#ifndef NDEBUG
                std::cout << "Client disconnected" << std::endl;
#endif
                closeClient();
                return;
            } else if (fd < 0) {
                std::cout << "Error receiving fd " << fd << std::endl;
                closeClient();
                return;
            }
            OverlayImage img = m_images.at(m_waitingId);
            img.memfd = fd;
            img.memory = mmap(NULL, img.memsize, PROT_READ, MAP_PRIVATE, img.memfd, 0);
            if (img.memory == MAP_FAILED) {
                std::cerr << "mmap error: " << strerror(errno) << std::endl;
                closeClient();
                return;
            }
            m_images[m_waitingId] = img;
        }
        // Get message
        char buf[MSG_BUF_SIZE];
        ssize_t n = os_socket_recv(m_client, buf, MSG_BUF_SIZE, 0);
        if (n == MSG_BUF_SIZE) {
            char rbuf[REPLY_BUF_SIZE];
            memset(rbuf, 0, REPLY_BUF_SIZE);
            struct reply_struct *reply = reinterpret_cast<struct reply_struct*>(rbuf);
            processMsg(reinterpret_cast<struct msg_struct*>(buf), reply);
            os_socket_send(m_client, reply, REPLY_BUF_SIZE, MSG_NOSIGNAL);
            if (reply->status == STATUS_ERROR) {
                closeClient();
                return;
            }
        }
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
#ifndef NDEBUG
            if (errno != ECONNRESET) {
                std::cerr << "Socket recv error: " << strerror(errno) << std::endl;
            }
#endif
        }
        if (n <= 0) {
#ifndef NDEBUG
            std::cout << "Client disconnected" << std::endl;
#endif
            closeClient();
            return;
        }
    }
}

void Control::processMsg(struct msg_struct *msg, struct reply_struct *reply)
{
    reply->msgtype = msg->type;

    switch (msg->type) {
    case MSG_CREATE_IMAGE:
        processCreateImageMsg(msg, reply);
        break;
    case MSG_UPDATE_IMAGE:
        processUpdateImageMsg(msg, reply);
        break;
    case MSG_UPDATE_IMAGE_CONTENTS:
        processUpdateImageContentsMsg(msg, reply);
        break;
    case MSG_DESTROY_IMAGE:
        processDestroyImageMsg(msg, reply);
        break;
    case MSG_DESTROY_ALL_IMAGES:
        processDestroyAllImagesMsg(msg, reply);
        break;
    default:
        std::cerr << "Invalid msg type " << msg->type << std::endl;
        reply->status = STATUS_ERROR;
        break;
    }
}

void Control::processCreateImageMsg(struct msg_struct *msg, struct reply_struct *reply)
{
    struct msg_create_image *m = &msg->create_image;

    reply->id = m->id;

    if (m->width == 0 || m->height == 0) {
        std::cerr << "Invalid size: " << m->width << "x" << m->height << std::endl;
        reply->status = STATUS_ERROR;
        return;
    }

    if (m->memsize > MAX_MEM_SIZE || m->memsize != (PIXELS_SIZE(m->width, m->height) * 2)) {
        std::cerr << "Invalid memsize: " << m->memsize << std::endl;
        reply->status = STATUS_ERROR;
        return;
    }

    auto it = m_images.find(m->id);
    if (it != m_images.end()) {
        std::cerr << "Already have image with id " << m->id << std::endl;
        reply->status = STATUS_ERROR;
        return;
    }

#ifndef NDEBUG
    std::cout << "::Create image " << (unsigned)m->id << std::endl;
#endif

    OverlayImage img;
    img.x = m->x;
    img.y = m->y;
    img.width = m->width;
    img.height = m->height;
    img.visible = m->visible == 1;
    img.memsize = m->memsize;
    m_images.insert({m->id, img});

    m_waitingId = m->id;
    m_waitingForFd = true;

    reply->status = STATUS_OK;
}

void Control::processUpdateImageMsg(struct msg_struct *msg, struct reply_struct *reply)
{
    struct msg_update_image *m = &msg->update_image;

    reply->id = m->id;

    auto it = m_images.find(m->id);
    if (it == m_images.end()) {
        std::cerr << "Unknown id " << m->id << std::endl;
        reply->status = STATUS_ERROR;
        return;
    }

#ifndef NDEBUG
    std::cout << "::Update image " << (unsigned)m->id << std::endl;
#endif

    it->second.x = m->x;
    it->second.y = m->y;
    it->second.visible = m->visible;

    reply->status = STATUS_OK;
}

void Control::processUpdateImageContentsMsg(struct msg_struct *msg, struct reply_struct *reply)
{
    struct msg_update_image_contents *m = &msg->update_image_contents;

    reply->id = m->id;

    auto it = m_images.find(m->id);
    if (it == m_images.end()) {
        std::cerr << "Unknown id " << m->id << std::endl;
        reply->status = STATUS_ERROR;
        return;
    }

    if (m->buffer < 0 || m->buffer > 1) {
        std::cerr << "Invalid buffer id " << m->buffer << std::endl;
        reply->status = STATUS_ERROR;
        return;
    }

    it->second.pixels = static_cast<uint8_t*>(it->second.memory) + (PIXELS_SIZE(it->second.width, it->second.height) * m->buffer);

    reply->status = STATUS_OK;
    reply->buffer = m->buffer;
}

void Control::processDestroyImageMsg(struct msg_struct *msg, struct reply_struct *reply)
{
    struct msg_destroy_image *m = &msg->destroy_image;

    reply->id = m->id;

    auto it = m_images.find(m->id);
    if (it == m_images.end()) {
        std::cerr << "Unknown id " << m->id << std::endl;
        reply->status = STATUS_ERROR;
        return;
    }

#ifndef NDEBUG
    std::cout << "::Destroy image " << (unsigned)m->id << std::endl;
#endif

    destroyImage(it->second);
    m_images.erase(it);

    reply->status = STATUS_OK;
}

void Control::processDestroyAllImagesMsg(struct msg_struct *msg, struct reply_struct *reply)
{
#ifndef NDEBUG
    std::cout << "::Destroy all images " << std::endl;
#endif

    destroyAllImages();

    reply->status = STATUS_OK;
}

void Control::closeClient()
{
    os_socket_close(m_client);
    m_client = -1;
    m_waitingForFd = false;

    destroyAllImages();
}

void Control::destroyImage(OverlayImage &img)
{
    if (img.memory) {
        munmap(img.memory, img.memsize);
        img.memory = nullptr;
    }
    if (img.memfd >= 0) {
        close(img.memfd);
        img.memfd = -1;
    }
}

void Control::destroyAllImages()
{
    for (auto it : m_images) {
        destroyImage(it.second);
    }
    m_images.clear();
}
