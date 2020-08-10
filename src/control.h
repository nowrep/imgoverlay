#pragma once

#include <thread>
#include <mutex>

struct control_thread {
    std::thread thread;
    std::mutex mutex;
    std::string socket_path;
    int client = -1;
    bool thread_quit = false;
    uint8_t *buffer = nullptr;
    size_t buffer_pos = 0;
    bool client_disconnected = false;

    uint32_t msg_size = 0;
    uint8_t msg_size_pos = 0;
    uint8_t *current_msg = nullptr;
    size_t current_msg_pos = 0;

    struct overlay_data *ov_data = nullptr;

    void reset_buf() {
        buffer_pos = 0;
        msg_size = 0;
        msg_size_pos = 0;
        free(current_msg);
        current_msg = nullptr;
        current_msg_pos = 0;
    }
};

void control_process_socket(struct control_thread &data, struct overlay_data *ov_data);

void control_start(struct control_thread &data);
void control_terminate(struct control_thread &data);
