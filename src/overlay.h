#pragma once
#ifndef MANGOHUD_OVERLAY_H
#define MANGOHUD_OVERLAY_H

#include <string>
#include <stdint.h>
#include <vector>

#include "imgui.h"
#include "overlay_params.h"

struct overlay_data {
    struct image {
        int x;
        int y;
        int width;
        int height;
        uint8_t *pixels;
        ImTextureID texture = 0;
        void *to_free;
    };
    enum changes {
        image_created,
        image_updated,
        image_destroyed
    };
    std::unordered_map<uint8_t, image> images;
    std::vector<std::pair<uint8_t, changes>> images_changes;
};

void render_imgui(struct overlay_data& data, struct overlay_params& params, bool is_vulkan);
void check_keybinds(struct overlay_data& data, struct overlay_params& params);
void create_font(const overlay_params& params);

#endif //MANGOHUD_OVERLAY_H
