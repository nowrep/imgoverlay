#pragma once
#ifndef MANGOHUD_OVERLAY_H
#define MANGOHUD_OVERLAY_H

#include <string>
#include <stdint.h>
#include <vector>
#include "imgui.h"
#include "overlay_params.h"

struct overlay_data {
    int control_client = -1;
};

void position_layer(struct overlay_data& data, struct overlay_params& params, ImVec2 window_size);
void render_imgui(struct overlay_data& data, struct overlay_params& params, bool is_vulkan);
void check_keybinds(struct overlay_data& data, struct overlay_params& params);
void create_font(const overlay_params& params, ImFont*& small_font, ImFont*& text_font);

#endif //MANGOHUD_OVERLAY_H
