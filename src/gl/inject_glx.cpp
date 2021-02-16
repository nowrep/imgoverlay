#include <X11/Xlib.h>
#include <iostream>
#include <array>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstring>
#include "real_dlsym.h"
#include "loaders/loader_glx.h"
#include "loaders/loader_x11.h"
#include "mesa/util/macros.h"
#include "mesa/util/os_time.h"
#include "blacklist.h"

#include <chrono>
#include <iomanip>

#include "imgui_hud.h"

using namespace imgoverlay::GL;

#define EXPORT_C_(type) extern "C" __attribute__((__visibility__("default"))) type

EXPORT_C_(void *) glXGetProcAddress(const unsigned char* procName);
EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName);

#ifndef GLX_WIDTH
#define GLX_WIDTH   0x801D
#define GLX_HEIGTH  0x801E
#endif

static glx_loader glx;

static std::vector<std::thread::id> gl_threads;

void* get_glx_proc_address(const char* name) {
    glx.Load();

    void *func = nullptr;
    if (glx.GetProcAddress)
        func = glx.GetProcAddress( (const unsigned char*) name );

    if (!func && glx.GetProcAddressARB)
        func = glx.GetProcAddressARB( (const unsigned char*) name );

    if (!func)
        func = get_proc_address( name );

    if (!func) {
        std::cerr << "imgoverlay: Failed to get function '" << name << "'" << std::endl;
    }

    return func;
}

EXPORT_C_(void *) glXCreateContext(void *dpy, void *vis, void *shareList, int direct)
{
    glx.Load();
    void *ctx = glx.CreateContext(dpy, vis, shareList, direct);
#ifndef NDEBUG
    std::cerr << __func__ << ":" << ctx << std::endl;
#endif
    return ctx;
}

static void do_imgui_swap(void *dpy, void *drawable)
{
    if (!is_blacklisted()) {
        imgui_create(glx.GetCurrentContext());

        unsigned int width = -1, height = -1;

        glx.QueryDrawable(dpy, drawable, GLX_WIDTH, &width);
        glx.QueryDrawable(dpy, drawable, GLX_HEIGTH, &height);

        /*GLint vp[4]; glGetIntegerv (GL_VIEWPORT, vp);
        width = vp[2];
        height = vp[3];*/

        imgui_render(width, height);
    }
}

EXPORT_C_(void) glXSwapBuffers(void* dpy, void* drawable) {
    glx.Load();

    do_imgui_swap(dpy, drawable);
    glx.SwapBuffers(dpy, drawable);
}

EXPORT_C_(int64_t) glXSwapBuffersMscOML(void* dpy, void* drawable, int64_t target_msc, int64_t divisor, int64_t remainder)
{
    glx.Load();

    do_imgui_swap(dpy, drawable);
    return glx.SwapBuffersMscOML(dpy, drawable, target_msc, divisor, remainder);
}

struct func_ptr {
   const char *name;
   void *ptr;
};

static std::array<const func_ptr, 5> name_to_funcptr_map = {{
#define ADD_HOOK(fn) { #fn, (void *) fn }
   ADD_HOOK(glXGetProcAddress),
   ADD_HOOK(glXGetProcAddressARB),
   ADD_HOOK(glXCreateContext),
   ADD_HOOK(glXSwapBuffers),
   ADD_HOOK(glXSwapBuffersMscOML),
#undef ADD_HOOK
}};

EXPORT_C_(void *) imgoverlay_find_glx_ptr(const char *name)
{
  if (is_blacklisted())
      return nullptr;

   for (auto& func : name_to_funcptr_map) {
      if (strcmp(name, func.name) == 0)
         return func.ptr;
   }

   return nullptr;
}

EXPORT_C_(void *) glXGetProcAddress(const unsigned char* procName) {
    //std::cerr << __func__ << ":" << procName << std::endl;

    void* func = imgoverlay_find_glx_ptr( (const char*)procName );
    if (func)
        return func;

    return get_glx_proc_address((const char*)procName);
}

EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName) {
    //std::cerr << __func__ << ":" << procName << std::endl;

    void* func = imgoverlay_find_glx_ptr( (const char*)procName );
    if (func)
        return func;

    return get_glx_proc_address((const char*)procName);
}
