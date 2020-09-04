#pragma once

#include "overlay_params.h"

void render_imgui(struct swapchain_data *data);
void check_keybinds(struct overlay_data& data, struct overlay_params& params);
void create_font(const overlay_params& params);
