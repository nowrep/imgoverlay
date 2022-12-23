#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef KeySym
typedef unsigned long KeySym;
#endif

#define OVERLAY_PARAMS                               \
   OVERLAY_PARAM_CUSTOM(no_display)                  \
   OVERLAY_PARAM_CUSTOM(socket)                      \
   OVERLAY_PARAM_CUSTOM(font_size)                   \
   OVERLAY_PARAM_CUSTOM(font_scale)                  \
   OVERLAY_PARAM_CUSTOM(toggle_overlay)              \

enum overlay_param_enabled {
#define OVERLAY_PARAM_BOOL(name) OVERLAY_PARAM_ENABLED_##name,
#define OVERLAY_PARAM_CUSTOM(name)
   OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
   OVERLAY_PARAM_ENABLED_MAX
};

struct overlay_params {
   bool enabled[OVERLAY_PARAM_ENABLED_MAX];
   bool no_display = false;
   std::string socket;
   std::vector<KeySym> toggle_overlay;
   float font_size = 0.0, font_scale = 0.0;
   std::unordered_map<std::string,std::string> options;
};

const extern char *overlay_param_names[];

void parse_overlay_env(struct overlay_params *params,
                       const char *env);
void parse_overlay_config(struct overlay_params *params,
                       const char *env);

#ifdef __cplusplus
}
#endif
