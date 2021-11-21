#pragma once

#include "overlay.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

namespace imgoverlay { namespace GL {

extern overlay_params params;
void imgui_init();
void imgui_create(void *ctx, bool glx);
void imgui_shutdown();
void imgui_render(unsigned int width, unsigned int height);

}} // namespace
