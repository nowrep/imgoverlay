#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/memfd.h>

#define MSG_BUF_SIZE 128 // XXX
#define REPLY_BUF_SIZE 16
#define PIXELS_SIZE(w, h) ((w) * (h) * sizeof(uint32_t))

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

struct msg_create_image {
    uint8_t id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t visible;
    uint8_t flip;
    uint8_t nfd;
    // shmem
    uint32_t memsize;
    // dmabuf
    int32_t format;
    uint64_t modifier;
    int32_t strides[4];
    int32_t offsets[4];
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
