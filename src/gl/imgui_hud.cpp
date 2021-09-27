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
    ImGuiContext *imgui_ctx = nullptr;
    Control *control = nullptr;

    struct image_data {
        GLuint texture = 0;
        uint8_t *uploaded_pixels = nullptr;
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
void imgui_create(void *ctx)
{
    if (inited)
        return;
    inited = true;

    if (!ctx)
        return;

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

void imgui_set_context(void *ctx)
{
    if (!ctx) {
        imgui_shutdown();
        return;
    }
#ifndef NDEBUG
    std::cerr << __func__ << ": " << ctx << std::endl;
#endif
    imgui_create(ctx);
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

static void destroy_texture(GLuint texture)
{
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
        state.images_data.insert({id, state::image_data()});
    }

    // Destroyed
    std::vector<uint8_t> to_erase;
    for (auto it : state.images_data) {
        const uint8_t id = it.first;
        if (images.find(id) != images.end()) {
            continue;
        }
        destroy_texture(it.second.texture);
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
        if (img.dmabuf) {
            continue;
        }
        if (img.pixels != img_data.uploaded_pixels) {
            img_data.texture = create_update_texture(img_data.texture, img.width, img.height, img.pixels);
            img_data.uploaded_pixels = img.pixels;
        }
    }
}

static void render_imgui()
{
    state.control->processSocket();
    check_keybinds(params);

    if (params.no_display) {
        return;
    }

    update_images();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const std::unordered_map<uint8_t, OverlayImage> &images = state.control->images();

    for (auto it : images) {
        const uint8_t id = it.first;
        const OverlayImage &img = it.second;
        state::image_data &img_data = state.images_data[id];
        if (!img.visible) {
            continue;
        }
        ImGui::SetNextWindowBgAlpha(0.0);
        ImGui::SetNextWindowPos(ImVec2(img.x, img.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(img.width, img.height), ImGuiCond_Always);
        char name[4];
        snprintf(name, 4, "%u", (unsigned)id);
        ImGui::Begin(name, &open, ImGuiWindowFlags_NoDecoration);
        ImGui::Image((VkDescriptorSet)(uint64_t)img_data.texture, ImVec2(img.width, img.height));
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
