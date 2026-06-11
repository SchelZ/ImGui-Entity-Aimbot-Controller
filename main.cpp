#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <GL/GL.h>
#  ifndef GL_CLAMP_TO_EDGE
#    define GL_CLAMP_TO_EDGE 0x812F
#  endif
#  ifndef GL_UNPACK_ROW_LENGTH
#    define GL_UNPACK_ROW_LENGTH 0x0CF2
#  endif
#endif
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"
#include "agent_img.h"

#include <algorithm>
#include <vector>
#include <string>

static bool LoadTextureFromMemory(const unsigned char* buf, int buf_size, GLuint* out_tex, int* out_w, int* out_h)
{
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load_from_memory(buf, buf_size, &w, &h, &ch, 4);
    if (!px) { fprintf(stderr, "stbi: %s\n", stbi_failure_reason()); return false; }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);

    *out_tex = tex;  *out_w = w;  *out_h = h;
    return true;
}

static ImVec2 PointScreenPos(int col, int row,
    int cols, int rows,
    ImVec2 img_tl, float dw, float dh)
{
    float x = img_tl.x + (float)col / (float)(cols - 1) * dw;
    float y = img_tl.y + (float)row / (float)(rows - 1) * dh;
    return { x, y };
}

static int HitTestPoint(ImVec2 mouse,
    int cols, int rows,
    ImVec2 img_tl, float dw, float dh,
    float snap_radius)
{
    int   best_idx = -1;
    float best_dist = snap_radius * snap_radius;

    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            ImVec2 pt = PointScreenPos(c, r, cols, rows, img_tl, dw, dh);
            float dx = mouse.x - pt.x;
            float dy = mouse.y - pt.y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best_dist) { best_dist = d2; best_idx = r * cols + c; }
        }
    return best_idx;
}

static void glfw_error_callback(int e, const char* d) { fprintf(stderr, "GLFW %d: %s\n", e, d); }

int main()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1100, 820, "Image Grid Viewer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    GLuint tex_id = 0;
    int    img_w = 0, img_h = 0;
    bool   img_ok = LoadTextureFromMemory(rawData, (int)sizeof(rawData), &tex_id, &img_w, &img_h);

    int    h_lines = 5;
    int    v_lines = 5;
    int    prev_h = h_lines;
    int    prev_v = v_lines;
    bool   show_grid = true;

    ImVec4 grid_color = ImVec4(1.0f, 0.42f, 0.10f, 0.85f);
    ImVec4 pt_color = ImVec4(0.20f, 0.80f, 1.00f, 1.00f);
    ImVec4 sel_color = ImVec4(0.10f, 1.00f, 0.30f, 1.00f);
    float  grid_thick = 1.2f;
    float  dot_radius = 5.0f;
    float  snap_px = 14.0f;

    const float MAX_SIDE = 512.0f;
    float scale = img_ok
        ? std::min(MAX_SIDE / (float)img_w, MAX_SIDE / (float)img_h)
        : 1.0f;

    std::vector<int> selected_points;
    int hovered_point = -1;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (h_lines != prev_h || v_lines != prev_v)
        {
            selected_points.clear();
            prev_h = h_lines;
            prev_v = v_lines;
        }

        const int   cols = v_lines + 2;
        const int   rows = h_lines + 2;
        const float dw = (float)img_w * scale;
        const float dh = (float)img_h * scale;

        ImGui::SetNextWindowPos({ 10, 10 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 320, 0 }, ImGuiCond_Always);
        ImGui::Begin("Grid Controls", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

        if (img_ok) ImGui::TextDisabled("Image : %d x %d px", img_w, img_h);
        else        ImGui::TextColored({ 1,0.3f,0.3f,1 }, "Image failed to decode!");

        ImGui::TextDisabled("Grid  : %d cols x %d rows  (%d points)", cols, rows, cols * rows);
        ImGui::Separator(); ImGui::Spacing();

        ImGui::Checkbox("Show Grid", &show_grid);
        ImGui::Spacing();

        ImGui::BeginDisabled(!show_grid);
        ImGui::SetNextItemWidth(230); ImGui::SliderInt("Vertical lines", &v_lines, 0, 30);
        ImGui::SetNextItemWidth(230); ImGui::SliderInt("Horizontal lines", &h_lines, 0, 30);
        ImGui::Spacing();
        ImGui::SetNextItemWidth(230); ImGui::SliderFloat("Thickness", &grid_thick, 0.5f, 5.0f, "%.1f px");
        ImGui::ColorEdit4("Grid colour", (float*)&grid_color, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
        ImGui::EndDisabled();

        ImGui::Separator(); ImGui::Spacing();

        ImGui::SetNextItemWidth(230); ImGui::SliderFloat("Dot radius", &dot_radius, 2.0f, 20.0f, "%.0f px");
        ImGui::SetNextItemWidth(230); ImGui::SliderFloat("Snap radius", &snap_px, 4.0f, 40.0f, "%.0f px");
        ImGui::ColorEdit4("Selected colour", (float*)&sel_color, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);

        ImGui::Separator(); ImGui::Spacing();

        ImGui::SetNextItemWidth(230);
        ImGui::SliderFloat("Zoom", &scale, 0.1f, 4.0f, "%.2fx");
        if (ImGui::Button("Fit to 512px"))
            scale = img_ok ? std::min(MAX_SIDE / (float)img_w, MAX_SIDE / (float)img_h) : 1.0f;

        ImGui::Separator(); ImGui::Spacing();

        ImGui::Text("Selected points (%d):", (int)selected_points.size());
        ImGui::SameLine();
        ImGui::TextDisabled("(Ctrl+A = all)");
        ImGui::Spacing();

        static std::vector<int> list_highlighted;

        if (!selected_points.empty())
        {
            ImGui::BeginChild("##sel_list", ImVec2(0, 150), true);

            for (int i = 0; i < (int)selected_points.size(); ++i)
            {
                int    idx = selected_points[i];
                int    r = idx / cols;
                int    c = idx % cols;
                char   label[64];
                snprintf(label, sizeof(label), "idx %-4d  [col %2d, row %2d]", idx, c, r);

                bool is_hl = std::find(list_highlighted.begin(),
                    list_highlighted.end(), i)
                    != list_highlighted.end();

                if (ImGui::Selectable(label, is_hl,
                    ImGuiSelectableFlags_None))
                {
                    if (ImGui::GetIO().KeyCtrl)
                    {
                        auto it = std::find(list_highlighted.begin(),
                            list_highlighted.end(), i);
                        if (it == list_highlighted.end())
                            list_highlighted.push_back(i);
                        else
                            list_highlighted.erase(it);
                    }
                    else
                    {
                        list_highlighted.clear();
                        list_highlighted.push_back(i);
                    }
                }
            }

            if (ImGui::IsWindowFocused() &&
                ImGui::GetIO().KeyCtrl &&
                ImGui::IsKeyPressed(ImGuiKey_A))
            {
                list_highlighted.clear();
                for (int i = 0; i < (int)selected_points.size(); ++i)
                    list_highlighted.push_back(i);
            }

            ImGui::EndChild();
            ImGui::Spacing();

            auto build_copy_set = [&]() -> std::vector<int>
                {
                    if (!list_highlighted.empty())
                    {
                        std::vector<int> out;
                        for (int i : list_highlighted)
                            if (i < (int)selected_points.size())
                                out.push_back(selected_points[i]);
                        return out;
                    }
                    return selected_points;
                };

            if (ImGui::Button("Copy indices"))
            {
                auto set = build_copy_set();
                std::string s;
                for (int k = 0; k < (int)set.size(); ++k)
                {
                    if (k) s += ", ";
                    s += std::to_string(set[k]);
                }
                ImGui::SetClipboardText(s.c_str());
            }
            ImGui::SameLine();

            if (ImGui::Button("Copy col,row"))
            {
                auto set = build_copy_set();
                std::string s;
                for (int k = 0; k < (int)set.size(); ++k)
                {
                    if (k) s += ", ";
                    int idx = set[k];
                    s += '{';
                    s += std::to_string(idx % cols);
                    s += ',';
                    s += std::to_string(idx / cols);
                    s += '}';
                }
                ImGui::SetClipboardText(s.c_str());
            }
            ImGui::SameLine();

            if (ImGui::Button("Remove"))
            {
                if (!list_highlighted.empty())
                {
                    std::sort(list_highlighted.rbegin(), list_highlighted.rend());
                    for (int i : list_highlighted)
                        if (i < (int)selected_points.size())
                            selected_points.erase(selected_points.begin() + i);
                    list_highlighted.clear();
                }
                else
                {
                    selected_points.clear();
                }
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Highlight rows then Copy - or copy all if none highlighted.");
            ImGui::Spacing();

            if (ImGui::Button("Clear all"))
            {
                selected_points.clear();
                list_highlighted.clear();
            }
        }
        else
        {
            list_highlighted.clear();
            ImGui::TextDisabled("  (none — click a grid point)");
        }

        ImGui::End();
        ImGui::SetNextWindowPos({ 342, 10 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ dw + 20.0f, dh + 45.0f }, ImGuiCond_Always);
        ImGui::Begin("Image Viewer", nullptr,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_HorizontalScrollbar |
            ImGuiWindowFlags_NoResize);

        ImVec2 img_tl = ImGui::GetCursorScreenPos();

        if (img_ok) ImGui::Image((ImTextureID)(intptr_t)tex_id, ImVec2(dw, dh));
        else
        {
            ImGui::GetWindowDrawList()->AddRectFilled(img_tl, { img_tl.x + dw, img_tl.y + dh }, IM_COL32(70, 70, 70, 255));
            ImGui::Dummy(ImVec2(dw, dh));
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();

        hovered_point = -1;
        bool window_hovered = ImGui::IsWindowHovered();
        if (window_hovered && img_ok)
        {
            ImVec2 mouse = ImGui::GetMousePos();
            if (mouse.x >= img_tl.x && mouse.x <= img_tl.x + dw &&
                mouse.y >= img_tl.y && mouse.y <= img_tl.y + dh)
            {
                hovered_point = HitTestPoint(
                    mouse, cols, rows, img_tl, dw, dh, snap_px);
            }
        }

        if (hovered_point >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            auto it = std::find(selected_points.begin(), selected_points.end(), hovered_point);
            if (it == selected_points.end()) selected_points.push_back(hovered_point);
            else selected_points.erase(it);
        }

        if (show_grid && img_ok)
        {
            ImU32 gc = ImGui::ColorConvertFloat4ToU32(grid_color);
            float x0 = img_tl.x, y0 = img_tl.y;
            float x1 = x0 + dw, y1 = y0 + dh;

            for (int i = 1; i <= v_lines; ++i)
            {
                float x = x0 + (float)i / (float)(cols - 1) * dw;
                dl->AddLine({ x, y0 }, { x, y1 }, gc, grid_thick);
            }
            for (int i = 1; i <= h_lines; ++i)
            {
                float y = y0 + (float)i / (float)(rows - 1) * dh;
                dl->AddLine({ x0, y }, { x1, y }, gc, grid_thick);
            }
            dl->AddRect({ x0, y0 }, { x1, y1 }, gc, 0.0f, 0, grid_thick);
        }

        std::vector<int> hl_grid_indices;
        for (int li : list_highlighted)
            if (li < (int)selected_points.size())
                hl_grid_indices.push_back(selected_points[li]);

        if (img_ok && (show_grid || !selected_points.empty()))
        {
            ImU32 sel_c = ImGui::ColorConvertFloat4ToU32(sel_color);
            ImU32 hov_c = IM_COL32(255, 255, 100, 220);
            ImU32 nor_c = IM_COL32(255, 255, 255, 60);
            ImU32 hl_c = IM_COL32(255, 160, 30, 255);

            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    int    idx = r * cols + c;
                    ImVec2 pt = PointScreenPos(c, r, cols, rows, img_tl, dw, dh);

                    bool is_selected = std::find(selected_points.begin(),
                        selected_points.end(), idx)
                        != selected_points.end();
                    bool is_hovered = (idx == hovered_point);
                    bool is_hl = std::find(hl_grid_indices.begin(),
                        hl_grid_indices.end(), idx)
                        != hl_grid_indices.end();

                    if (is_selected)
                    {
                        dl->AddCircleFilled(pt, dot_radius, sel_c);
                        if (is_hl)
                        {
                            dl->AddCircle(pt, dot_radius + 3.0f, hl_c, 0, 2.5f);
                            dl->AddCircle(pt, dot_radius + 1.0f, IM_COL32(255, 255, 255, 220), 0, 1.5f);
                        }
                        else
                        {
                            dl->AddCircle(pt, dot_radius + 1.5f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
                        }
                    }
                    else if (is_hovered)
                    {
                        dl->AddCircle(pt, dot_radius, hov_c, 0, 2.0f);
                    }
                    else if (show_grid)
                    {
                        dl->AddCircleFilled(pt, 2.5f, nor_c);
                    }
                }
        }

        if (hovered_point >= 0)
        {
            int r = hovered_point / cols;
            int c = hovered_point % cols;
            bool already = std::find(selected_points.begin(), selected_points.end(), hovered_point) != selected_points.end();
            ImGui::BeginTooltip();
            ImGui::Text("Point index : %d", hovered_point);
            ImGui::Text("Col / Row   : %d / %d", c, r);
            ImGui::TextDisabled(already ? "Click to deselect" : "Click to select");
            ImGui::EndTooltip();
        }

        ImGui::End();

        ImGui::Render();
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (tex_id) glDeleteTextures(1, &tex_id);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}