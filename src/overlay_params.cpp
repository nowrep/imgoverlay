#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/sysinfo.h>
#include <wordexp.h>
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>

#include "overlay_params.h"
#include "overlay.h"
#include "config.h"
#include "string_utils.h"

#include "mesa/util/os_socket.h"

#ifdef HAVE_X11
#include <X11/keysym.h>
#include "loaders/loader_x11.h"
#endif

static float
parse_float(const char *str)
{
   float val = 0;
   std::stringstream ss(str);
   ss.imbue(std::locale::classic());
   ss >> val;
   return val;
}

static std::string
parse_string(const char *str)
{
    return str;
}

#ifdef HAVE_X11
static std::vector<KeySym>
parse_string_to_keysym_vec(const char *str)
{
   std::vector<KeySym> keys;
   if(g_x11->IsLoaded())
   {
      std::stringstream keyStrings(str);
      std::string ks;
      while (std::getline(keyStrings, ks, '+')) {
         trim(ks);
         KeySym xk = g_x11->XStringToKeysym(ks.c_str());
         if (xk)
            keys.push_back(xk);
         else
            std::cerr << "imgoverlay: Unrecognized key: '" << ks << "'\n";
      }
   }
   return keys;
}

static std::vector<KeySym>
parse_toggle_overlay(const char *str)
{
   return parse_string_to_keysym_vec(str);
}

#else
#define parse_toggle_overlay(x)      {}
#endif

#define parse_control(s) parse_string(s)
#define parse_font_scale(s) parse_float(s)
#define parse_font_size(s) parse_float(s)

static bool
parse_no_display(const char *str)
{
   return strtol(str, NULL, 0) != 0;
}

static bool is_delimiter(char c)
{
   return c == 0 || c == ',' || c == ':' || c == ';' || c == '=';
}

static int
parse_string(const char *s, char *out_param, char *out_value)
{
   int i = 0;

   for (; !is_delimiter(*s); s++, out_param++, i++)
      *out_param = *s;

   *out_param = 0;

   if (*s == '=') {
      s++;
      i++;
      for (; !is_delimiter(*s); s++, out_value++, i++) {
         *out_value = *s;
         // Consume escaped delimiter, but don't escape null. Might be end of string.
         if (*s == '\\' && *(s + 1) != 0 && is_delimiter(*(s + 1))) {
            s++;
            i++;
            *out_value = *s;
         }
      }
   } else
      *(out_value++) = '1';
   *out_value = 0;

   if (*s && is_delimiter(*s)) {
      s++;
      i++;
   }

   if (*s && !i) {
      fprintf(stderr, "mesa-overlay: syntax error: unexpected '%c' (%i) while "
              "parsing a string\n", *s, *s);
      fflush(stderr);
   }

   return i;
}

const char *overlay_param_names[] = {
#define OVERLAY_PARAM_BOOL(name) #name,
#define OVERLAY_PARAM_CUSTOM(name)
   OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
};

void
parse_overlay_env(struct overlay_params *params,
                  const char *env)
{
   uint32_t num;
   char key[256], value[256];
   while ((num = parse_string(env, key, value)) != 0) {
      env += num;
      if (!strcmp("full", key)) {
#define OVERLAY_PARAM_BOOL(name) \
         params->enabled[OVERLAY_PARAM_ENABLED_##name] = 1;
#define OVERLAY_PARAM_CUSTOM(name)
         OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
      }
#define OVERLAY_PARAM_BOOL(name)                                       \
      if (!strcmp(#name, key)) {                                       \
         params->enabled[OVERLAY_PARAM_ENABLED_##name] =               \
            strtol(value, NULL, 0);                                    \
         continue;                                                     \
      }
#define OVERLAY_PARAM_CUSTOM(name)                                     \
      if (!strcmp(#name, key)) {                                       \
         params->name = parse_##name(value);                           \
         continue;                                                     \
      }
      OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
      fprintf(stderr, "Unknown option '%s'\n", key);
   }
}

void
parse_overlay_config(struct overlay_params *params,
                  const char *env)
{

   *params = {};

   params->control = "/tmp/imgoverlay.socket";
   params->font_scale = 1.0f;

#ifdef HAVE_X11
   params->toggle_overlay = { XK_Shift_R, XK_F12 };
#endif

   // first pass with env var
   if (env)
      parse_overlay_env(params, env);

   if (!env) {

      // Get config options
      parseConfigFile(*params);
      if (params->options.find("full") != params->options.end() && params->options.find("full")->second != "0") {
#define OVERLAY_PARAM_BOOL(name) \
            params->enabled[OVERLAY_PARAM_ENABLED_##name] = 1;
#define OVERLAY_PARAM_CUSTOM(name)
            OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
         params->options.erase("full");
      }
      for (auto& it : params->options) {
#define OVERLAY_PARAM_BOOL(name)                                       \
         if (it.first == #name) {                                      \
            params->enabled[OVERLAY_PARAM_ENABLED_##name] =            \
               strtol(it.second.c_str(), NULL, 0);                     \
            continue;                                                  \
         }
#define OVERLAY_PARAM_CUSTOM(name)                                     \
         if (it.first == #name) {                                      \
            params->name = parse_##name(it.second.c_str());            \
            continue;                                                  \
         }
         OVERLAY_PARAMS
#undef OVERLAY_PARAM_BOOL
#undef OVERLAY_PARAM_CUSTOM
         fprintf(stderr, "Unknown option '%s'\n", it.first.c_str());
      }

   }

   // second pass, override config file settings with IMGOVERLAY_CONFIG
   if (env)
      parse_overlay_env(params, env);

   if (!params->font_size) {
      params->font_size = 24;
   }
}
