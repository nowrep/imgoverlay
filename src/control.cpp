#include "control.h"
#include "overlay.h"
#include "mesa/util/os_socket.h"

#include <unistd.h>
#include <thread>
#include <chrono>

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

static int create_image(struct overlay_data *data, struct msg_struct *msg)
{
    struct msg_create_image *m = &msg->create_image;

    if (m->pixels_size != (m->width * m->height * sizeof(uint32_t))) {
        printf("Invalid pixels size %i\n", m->pixels_size);
        return STATUS_ERROR;
    }

    auto it = data->images.find(m->id);
    if (it != data->images.end()) {
        printf("Already have id %i\n", (int)m->id);
        return STATUS_ERROR;
    }

    struct overlay_data::image img;
    img.x = m->x;
    img.y = m->y;
    img.width = m->width;
    img.height = m->height;
    img.pixels = m->pixels;
    img.to_free = msg;

    data->images.insert({m->id, img});
    data->images_changes.push_back({m->id, overlay_data::image_created});
    data->images_changes.push_back({m->id, overlay_data::image_updated});

    return STATUS_OK;
};

static int update_image(struct overlay_data *data, struct msg_struct *msg)
{
    struct msg_update_image *m = &msg->update_image;

    auto it = data->images.find(m->id);
    if (it == data->images.end()) {
        printf("Invalid id %i\n", (int)m->id);
        return STATUS_ERROR;
    }

    if (m->pixels_size != (it->second.width * it->second.height * sizeof(uint32_t))) {
        printf("Invalid pixels size %i\n", m->pixels_size);
        return STATUS_ERROR;
    }

    free(it->second.to_free);
    it->second.x = m->x;
    it->second.y = m->y;
    it->second.pixels = m->pixels;
    it->second.to_free = msg;

    data->images_changes.push_back({m->id, overlay_data::image_updated});

    return STATUS_OK;
};

static int destroy_image(struct overlay_data *data, struct msg_struct *msg)
{
    struct msg_destroy_image *m = &msg->destroy_image;

    auto it = data->images.find(m->id);
    if (it == data->images.end()) {
        printf("Invalid id %i\n", (int)m->id);
        return STATUS_ERROR;
    }

    free(it->second.to_free);
    data->images.erase(it);
    data->images_changes.push_back({m->id, overlay_data::image_destroyed});

    free(msg);

    return STATUS_OK;
};

static int process_msg(struct overlay_data *data, struct msg_struct *msg)
{
    switch (msg->type) {
    case MSG_CREATE_IMAGE:
        return create_image(data, msg);
    case MSG_UPDATE_IMAGE:
        return update_image(data, msg);
    case MSG_DESTROY_IMAGE:
        return destroy_image(data, msg);
    default:
        printf("Invalid msg type %i\n", msg->type);
        return STATUS_ERROR;
    }
}

static void process_buf(struct control_thread &data, int client, uint8_t *buf, size_t size)
{
    while (size > 0) {
        // Read message size
        if (data.msg_size == 0 || data.msg_size_pos > 0) {
            size_t remain = std::min(sizeof(uint32_t) - data.msg_size_pos, size);
            memcpy(&reinterpret_cast<uint8_t*>(&data.msg_size)[data.msg_size_pos], buf, remain);
            data.msg_size_pos += remain;
            buf += remain;
            size -= remain;
            if (data.msg_size_pos != sizeof(uint32_t)) {
                continue;
            }
            data.msg_size_pos = 0;
        }
        // Allocate message buffer
        if (!data.current_msg) {
            data.current_msg = (uint8_t*)malloc(data.msg_size);
        }
        size_t remain = std::min(data.msg_size - data.current_msg_pos, size);
        memcpy(&data.current_msg[data.current_msg_pos], buf, remain);
        data.current_msg_pos += remain;
        buf += remain;
        size -= remain;
        if (data.current_msg_pos == data.msg_size) {
            uint32_t ret = process_msg(data.ov_data, (msg_struct*)data.current_msg);
            os_socket_send(client, &ret, sizeof(uint32_t), 0x4000); // MSG_NOSIGNAL
            data.msg_size = 0;
            data.current_msg_pos = 0;
            data.current_msg = nullptr;
        }
    }
}

static void destroy_images(struct overlay_data *data)
{
    for (auto it = data->images.cbegin(); it != data->images.cend(); ++it) {
        free(it->second.to_free);
        data->images_changes.push_back({it->first, overlay_data::image_destroyed});
    }
    data->images.clear();
}

static void control_thread_run(void *params)
{
    static const size_t BUFSIZE = 5 * 1024 * 1024;

    struct control_thread *data = (struct control_thread*) params;

    data->mutex.lock();
    data->buffer = (uint8_t*) malloc(BUFSIZE);
    data->buffer_pos = 0;
    data->mutex.unlock();

    unlink(data->socket_path.c_str());
    int listen_socket = os_socket_listen_abstract(data->socket_path.c_str(), 1);
    if (listen_socket < 0) {
        fprintf(stderr, "ERROR: Couldn't create socket pipe at '%s'\n", data->socket_path.c_str());
        fprintf(stderr, "ERROR: '%s'\n", strerror(errno));
        return;
    }
    os_socket_block(listen_socket, false);

    while (true) {
        if (data->thread_quit) {
            break;
        }
        if (data->client < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            int socket = os_socket_accept(listen_socket);
            if (socket < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED)
                    fprintf(stderr, "ERROR on socket: %s\n", strerror(errno));
                continue;
            }
            os_socket_block(socket, false);
            data->mutex.lock();
            data->client = socket;
            printf("connected\n");
            data->mutex.unlock();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        data->mutex.lock();
        ssize_t n = os_socket_recv(data->client, data->buffer + data->buffer_pos, BUFSIZE - data->buffer_pos, 0x4000); // MSG_NOSIGNAL
        if (n > 0) {
            data->buffer_pos += n;
        }
        data->mutex.unlock();
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // nothing to read, try again later
                continue;
            }
            if (errno != ECONNRESET)
                fprintf(stderr, "ERROR on connection: %s\n", strerror(errno));
        }
        if (n <= 0) { // recv() returns 0 when the client disconnects
            data->mutex.lock();
            os_socket_close(data->client);
            data->client = -1;
            data->reset_buf();
            data->client_disconnected = true;
            printf("discon\n");
            data->mutex.unlock();
        }
    }

    os_socket_close(listen_socket);
}

void control_process_socket(struct control_thread &data, struct overlay_data *ov_data)
{
    data.mutex.lock();
    if (data.ov_data != ov_data) {
        if (data.ov_data) {
            os_socket_close(data.client);
            data.client = -1;
            data.client_disconnected = false;
            data.reset_buf();
            printf("force discon\n");
        }
        data.ov_data = ov_data;
    }
    if (data.client_disconnected) {
        data.client_disconnected = false;
        destroy_images(ov_data);
    }
    const size_t inc = 7123;
    for (size_t i = 0; i < data.buffer_pos; i += inc) {
        process_buf(data, data.client, &data.buffer[i], std::min(inc, data.buffer_pos - i));
    }
    data.buffer_pos = 0;
    data.mutex.unlock();
}

void control_start(struct control_thread &data)
{
    if (data.thread.joinable()) {
        data.thread.join();
    }
    data.thread = std::thread(control_thread_run, &data);
}

void control_terminate(struct control_thread &data)
{
    data.thread_quit = true;
    if (data.thread.joinable()) {
        data.thread.join();
    }
}
