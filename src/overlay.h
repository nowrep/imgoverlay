#pragma once
#ifndef MANGOHUD_OVERLAY_H
#define MANGOHUD_OVERLAY_H

#include "overlay_params.h"

class Control;

void render_imgui(struct swapchain_data *data);
void check_keybinds(struct overlay_data& data, struct overlay_params& params);
void create_font(const overlay_params& params);

#endif //MANGOHUD_OVERLAY_H
