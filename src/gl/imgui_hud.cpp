#include <cstdlib>
#include <functional>
#include <thread>
#include <string>
#include <iostream>
#include <memory>
#include <mutex>
#include "imgui.h"
#include "font_default.h"
#include "file_utils.h"
#include "imgui_hud.h"
#include "blacklist.h"
#include "version.h"
#include "control.h"

#include <glad/glad.h>

void* get_egl_proc_address(const char* name);

static void *(*pfn_eglGetCurrentDisplay)() = nullptr;
static void *(*pfn_eglCreateImage)(void*, void*, unsigned, void*, const intptr_t*) = nullptr;
static int (*pfn_eglDestroyImage)(void*, void*) = nullptr;
static void (*pfn_glEGLImageTargetTexture2DOES)(unsigned, void*) = nullptr;

static bool init_proc_egl()
{
#define GET_PROC(x) \
    if (!pfn_##x) { \
        pfn_##x = reinterpret_cast<decltype(pfn_##x)>(get_egl_proc_address(#x)); \
        if (!pfn_##x) return false; \
    }
    GET_PROC(eglGetCurrentDisplay);
    GET_PROC(eglCreateImage);
    GET_PROC(eglDestroyImage);
    GET_PROC(glEGLImageTargetTexture2DOES);
#undef GET_PROC
    return true;
}

namespace imgoverlay { namespace GL {

struct GLVec
{
    GLint v[4];

    GLint operator[] (size_t i)
    {
        return v[i];
    }

    bool operator== (const GLVec& r)
    {
        return v[0] == r.v[0]
            && v[1] == r.v[1]
            && v[2] == r.v[2]
            && v[3] == r.v[3];
    }

    bool operator!= (const GLVec& r)
    {
        return !(*this == r);
    }
};

struct state {
    bool glx = false;
    ImGuiContext *imgui_ctx = nullptr;
    Control *control = nullptr;

    struct image_data {
        GLuint texture = 0;
        uint8_t *uploaded_pixels = nullptr;
        void *image = nullptr;
    };
    std::unordered_map<uint8_t, image_data> images_data;
};

std::mutex mutex;
static GLVec last_vp {}, last_sb {};
static state state;

bool open = false;
static bool cfg_inited = false;
static bool inited = false;
struct overlay_params params;

void imgui_init()
{
    if (cfg_inited)
        return;
    cfg_inited = true;

    if (!is_blacklisted(true)) {
        std::cout << "imgoverlay " << IMGOVERLAY_VERSION << std::endl;
        parse_overlay_config(&params, getenv("IMGOVERLAY_CONFIG"));
        state.control = new Control(params.socket);
    }
}

//static
void imgui_create(void *ctx, bool glx)
{
    if (inited)
        return;
    inited = true;

    if (!ctx)
        return;

    state.glx = glx;
    imgui_init();

    gladLoadGL();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGuiContext *saved_ctx = ImGui::GetCurrentContext();
    state.imgui_ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    glGetIntegerv (GL_VIEWPORT, last_vp.v);
    glGetIntegerv (GL_SCISSOR_BOX, last_sb.v);

    ImGui::GetIO().IniFilename = NULL;
    ImGui::GetIO().DisplaySize = ImVec2(last_vp[2], last_vp[3]);

    ImGui_ImplOpenGL3_Init();
    // Make a dummy GL call (we don't actually need the result)
    // IF YOU GET A CRASH HERE: it probably means that you haven't initialized the OpenGL function loader used by this code.
    // Desktop OpenGL 3/4 need a function loader. See the IMGUI_IMPL_OPENGL_LOADER_xxx explanation above.
    GLint current_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

    create_font(params);

    // Restore global context or ours might clash with apps that use Dear ImGui
    ImGui::SetCurrentContext(saved_ctx);
}

void imgui_shutdown()
{
#ifndef NDEBUG
    std::cerr << __func__ << std::endl;
#endif

    if (state.imgui_ctx) {
        ImGui::SetCurrentContext(state.imgui_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(state.imgui_ctx);
        state.imgui_ctx = nullptr;
    }
    if (!is_blacklisted()) {
        delete state.control;
        state.control = nullptr;
    }
    inited = false;
}

static GLuint create_dmabuf_texture_glx(const OverlayImage &img, void *&image)
{
    return 0;
}

static GLuint create_dmabuf_texture_egl(const OverlayImage &img, void *&image)
{
    if (!init_proc_egl()) {
        return 0;
    }

    unsigned int atti = 0;
    intptr_t attribs[50];
    attribs[atti++] = 0x3057; // EGL_WIDTH
    attribs[atti++] = img.width;
    attribs[atti++] = 0x3056; // EGL_HEIGHT
    attribs[atti++] = img.height;
    attribs[atti++] = 0x3271; // EGL_LINUX_DRM_FOURCC_EXT;
    attribs[atti++] = img.format;

    struct {
        intptr_t fd;
        intptr_t offset;
        intptr_t pitch;
        intptr_t mod_lo;
        intptr_t mod_hi;
    } attr_names[4] = {
        {
            0x3272, // EGL_DMA_BUF_PLANE0_FD_EXT
            0x3273, // EGL_DMA_BUF_PLANE0_OFFSET_EXT
            0x3274, // EGL_DMA_BUF_PLANE0_PITCH_EXT
            0x3443, // EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT
            0x3444, // EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT
        }, {
            0x3275, // EGL_DMA_BUF_PLANE1_FD_EXT
            0x3276, // EGL_DMA_BUF_PLANE1_OFFSET_EXT
            0x3277, // EGL_DMA_BUF_PLANE1_PITCH_EXT
            0x3445, // EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT
            0x3446, // EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT
        }, {
            0x3278, // EGL_DMA_BUF_PLANE2_FD_EXT
            0x3279, // EGL_DMA_BUF_PLANE2_OFFSET_EXT
            0x327A, // EGL_DMA_BUF_PLANE2_PITCH_EXT
            0x3447, // EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT
            0x3448, // EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT
        }, {
            0x3440, // EGL_DMA_BUF_PLANE3_FD_EXT
            0x3441, // EGL_DMA_BUF_PLANE3_OFFSET_EXT
            0x3442, // EGL_DMA_BUF_PLANE3_PITCH_EXT
            0x3449, // EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT
            0x344A, // EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT
        }
    };

    for (int i = 0; i < img.nfd; i++) {
        attribs[atti++] = attr_names[i].fd;
        attribs[atti++] = img.dmabufs[i];
        attribs[atti++] = attr_names[i].offset;
        attribs[atti++] = img.offsets[i];
        attribs[atti++] = attr_names[i].pitch;
        attribs[atti++] = img.strides[i];
        attribs[atti++] = attr_names[i].mod_lo;
        attribs[atti++] = img.modifier & 0xFFFFFFFF;
        attribs[atti++] = attr_names[i].mod_hi;
        attribs[atti++] = img.modifier >> 32;
    }

    attribs[atti++] = 0x3038; // EGL_NONE

    image = pfn_eglCreateImage(pfn_eglGetCurrentDisplay(), nullptr, 0x3270 /*EGL_LINUX_DMA_BUF_EXT*/, nullptr, attribs);
    if (!image) {
        std::cerr << "Failed to create image" << std::endl;
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    pfn_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    return texture;
}

static GLuint create_dmabuf_texture(const OverlayImage &img, void *&image)
{
    if (state.glx) {
        return create_dmabuf_texture_glx(img, image);
    } else {
        return create_dmabuf_texture_egl(img, image);
    }
}

static GLuint create_update_texture(GLuint texture, int width, int height, uint8_t *pixels)
{
    if (texture > 0) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    } else {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    return texture;
}

static void destroy_image_glx(void *image)
{
}

static void destroy_image_egl(void *image)
{
    if (image) {
        pfn_eglDestroyImage(pfn_eglGetCurrentDisplay(), image);
    }
}

static void destroy_texture(GLuint texture, void *image)
{
    if (state.glx) {
        destroy_image_glx(image);
    } else {
        destroy_image_egl(image);
    }
    glDeleteTextures(1, &texture);
}

static void update_images()
{
    const std::unordered_map<uint8_t, OverlayImage> &images = state.control->images();

    // Created
    for (auto it : images) {
        const uint8_t id = it.first;
        if (state.images_data.find(id) != state.images_data.end()) {
            continue;
        }
        state::image_data img_data;
        if (it.second.dmabuf) {
            img_data.texture = create_dmabuf_texture(it.second, img_data.image);
        }
        state.images_data.insert({id, img_data});
    }

    // Destroyed
    std::vector<uint8_t> to_erase;
    for (auto it : state.images_data) {
        const uint8_t id = it.first;
        if (images.find(id) != images.end()) {
            continue;
        }
        destroy_texture(it.second.texture, it.second.image);
        to_erase.push_back(id);
    }
    for (uint8_t id : to_erase) {
        state.images_data.erase(id);
    }

    // Updated
    for (auto it : images) {
        const uint8_t id = it.first;
        const OverlayImage &img = it.second;
        state::image_data &img_data = state.images_data[id];
        if (img.dmabuf || img.pixels == img_data.uploaded_pixels) {
            continue;
        }
        img_data.texture = create_update_texture(img_data.texture, img.width, img.height, img.pixels);
        img_data.uploaded_pixels = img.pixels;
    }
}

static void render_imgui()
{
    state.control->processSocket();
    check_keybinds(params);

    update_images();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const std::unordered_map<uint8_t, OverlayImage> &images = state.control->images();

    for (auto it : images) {
        const uint8_t id = it.first;
        const OverlayImage &img = it.second;
        state::image_data &img_data = state.images_data[id];
        if (!img.visible || params.no_display) {
            img_data.uploaded_pixels = nullptr;
            continue;
        }
        if (!img.dmabuf && !img.pixels) {
            continue;
        }
        ImGui::SetNextWindowBgAlpha(0.0);
        ImGui::SetNextWindowPos(ImVec2(img.x, img.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(img.width, img.height), ImGuiCond_Always);
        char name[4];
        snprintf(name, 4, "%u", (unsigned)id);
        ImGui::Begin(name, &open, ImGuiWindowFlags_NoDecoration);
        if (img.flip) {
            ImGui::Image((VkDescriptorSet)(uint64_t)img_data.texture, ImVec2(img.width, img.height), ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::Image((VkDescriptorSet)(uint64_t)img_data.texture, ImVec2(img.width, img.height));
        }
        ImGui::End();
    }

    ImGui::PopStyleVar(3);
}

void imgui_render(unsigned int width, unsigned int height)
{
    if (!state.imgui_ctx)
        return;

    ImGuiContext *saved_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(state.imgui_ctx);
    ImGui::GetIO().DisplaySize = ImVec2(width, height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    {
        std::lock_guard<std::mutex> lk(mutex);
        render_imgui();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(saved_ctx);
}

}} // namespaces
