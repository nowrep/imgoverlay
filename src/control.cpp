#include "control.h"
#include "control_prot.h"
#include "overlay.h"
#include "mesa/util/os_socket.h"

#include <string.h>
#include <iostream>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x4000
#endif

#define MAX_MEM_SIZE 20 * 1024 * 1024

Control::Control(const std::string &socketPath)
    : m_socketPath(socketPath)
{
}

Control::~Control()
{
    if (m_server >= 0) {
        os_socket_close(m_server);
        unlink(m_socketPath.c_str());
        m_server = -1;
    }
}

const std::unordered_map<uint8_t, OverlayImage> &Control::images() const
{
    return m_images;
}

// https://github.com/a-darwish/memfd-examples
static int receive_fds(int socket, int fds[4])
{
    struct msghdr msgh;
    struct iovec iov;
    union {
        struct cmsghdr cmsgh;
        char control[CMSG_SPACE(sizeof(int)) * 4];
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
    msgh.msg_controllen =  sizeof(control_un.control);

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

    const size_t nfd = (cmsgh->cmsg_len - sizeof(struct cmsghdr)) / sizeof(int);
    memset(fds, -1, sizeof(int) * 4);
    for (size_t i = 0; i < nfd; ++i) {
        fds[i] = ((int*)CMSG_DATA(cmsgh))[i];
    }
    return 0;
}

void Control::processSocket()
{
    init();

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
            int fds[4];
            int ret = receive_fds(m_client, fds);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    m_waitingForFd = true;
                    return;
                }
#ifndef NDEBUG
                std::cout << "Client disconnected" << std::endl;
#endif
                closeClient();
                return;
            } else if (ret < 0) {
                std::cerr << "Error receiving fd " << ret << std::endl;
                closeClient();
                return;
            }
            OverlayImage img = m_images.at(m_waitingId);
            if (img.memsize) {
                img.memfd = fds[0];
                img.memory = mmap(NULL, img.memsize, PROT_READ, MAP_PRIVATE, img.memfd, 0);
                if (img.memory == MAP_FAILED) {
                    std::cerr << "mmap error: " << strerror(errno) << std::endl;
                    closeClient();
                    return;
                }
            } else {
                for (int i = 0; i < img.nfd; ++i) {
                    img.dmabufs[i] = fds[i];
                }
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

    if (m->memsize > 0 && (m->memsize > MAX_MEM_SIZE || m->memsize != (PIXELS_SIZE(m->width, m->height) * 2))) {
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

    if (m_images.size() >= MAX_OVERLAY_COUNT) {
        std::cerr << "Overlay count limit reached " << std::endl;
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
    img.flip = m->flip;
    img.dmabuf = m->memsize == 0;
    img.memsize = m->memsize;
    img.format = m->format;
    img.modifier = m->modifier;
    memcpy(img.strides, m->strides, sizeof(m->strides));
    memcpy(img.offsets, m->offsets, sizeof(m->offsets));
    img.nfd = m->nfd;
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

void Control::init()
{
    if (m_init) {
        return;
    }
    m_init = true;

    unlink(m_socketPath.c_str());
    m_server = os_socket_listen_abstract(m_socketPath.c_str(), 1);
    if (m_server < 0) {
        std::cerr << "Couldn't create socket at " << m_socketPath << ": " << strerror(errno) << std::endl;
        return;
    }
    os_socket_block(m_server, false);
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
    if (img.dmabuf) {
        for (int i = 0; i < img.nfd; ++i) {
            close(img.dmabufs[i]);
        }
    }
}

void Control::destroyAllImages()
{
    for (auto it : m_images) {
        destroyImage(it.second);
    }
    m_images.clear();
}
