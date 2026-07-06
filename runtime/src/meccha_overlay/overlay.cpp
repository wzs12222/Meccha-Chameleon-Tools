#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <thread>
#include <atomic>
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "ole32")
#pragma comment(lib, "shlwapi")
#include <shlwapi.h>

// Memory engine
#pragma comment(lib, "runtime\\.build\\bin\\meccha-core.lib")
#include "../meccha_core/meccha_core.h"

// ---------------------- Constants --------------------------
constexpr int    TARGET_FPS      = 60;
constexpr int    TICK_MS         = 1000 / TARGET_FPS;
constexpr int    DATA_UPDATE_HZ  = 20;
constexpr int    DATA_TICK_MS    = 1000 / DATA_UPDATE_HZ;
constexpr int    RADAR_MIN       = 80;
constexpr int    RADAR_MAX       = 400;
constexpr float  PI              = 3.14159265f;
const wchar_t*   GAME_WINDOW     = L"Chameleon  ";
const wchar_t*   OVERLAY_CLASS   = L"MecchaOverlay";

// ---------------------- Direct2D Globals -------------------
static ID2D1Factory*          g_d2d    = nullptr;
static IDWriteFactory*        g_dwrite = nullptr;
static ID2D1HwndRenderTarget* g_rt     = nullptr;
static IDWriteTextFormat*     g_font   = nullptr;
static ID2D1SolidColorBrush*  g_brush  = nullptr;
static HWND  g_overlay = nullptr;
static HWND  g_game    = nullptr;
static RECT  g_rect    = {};
static std::atomic<bool> g_running{true};

// ---------------------- Config -----------------------------
struct ColorRGB { float r, g, b; };
struct EspConfig {
    bool  dot_esp        = true;
    bool  box_esp        = false;
    bool  corner_box     = false;
    bool  skeleton_esp   = false;
    bool  snap_lines     = true;
    bool  show_names     = true;
    bool  show_roles     = true;
    bool  show_distance  = true;
    bool  health_bar     = true;
    bool  shield_bar     = true;
    bool  enemy_only     = false;
    bool  radar_enabled  = false;
    bool  aimbot_enabled = false;
    bool  aimbot_show_fov = true;
    bool  invincible_detect = true;

    ColorRGB enemy_color     = {1,0,0};
    ColorRGB teammate_color  = {1,1,0};
    ColorRGB local_color     = {0,1,0};
    ColorRGB unknown_color   = {0,0.31f,0.71f};
    ColorRGB hunter_color    = {1,0.24f,0.24f};
    ColorRGB survivor_color  = {0.24f,0.71f,1};
    ColorRGB visible_color   = {0,1,0};
    ColorRGB not_visible_color = {0.5f,0,0.5f};
    ColorRGB invincible_color  = {1,0.84f,0};
    ColorRGB radar_color    = {1,1,1};

    int   dot_radius       = 8;
    float box_height_world = 100;
    int   box_y_offset     = 0;
    int   line_thickness   = 1;
    int   point_size       = 2;
    int   radar_size       = 180;
    float radar_range      = 5000;
    int   radar_opacity    = 160;
    int   aimbot_fov       = 150;
    float aimbot_smooth    = 0.3f;
    float aimbot_target_offset = 90;
    bool  distance_scaling = true;
    float scale_ref_dist   = 1500;

    bool  disable_buried   = true;
    bool  background_geo   = false;
    bool  show_cursor      = false;
    bool  draw_all         = false;
    float draw_all_max_dist = 3000;
};

static EspConfig g_cfg;

static void parse_json(const std::string& text, EspConfig& cfg) {
    auto s = [&](const std::string& key) -> std::string {
        auto p = text.find("\"" + key + "\"");
        if (p == std::string::npos) return "";
        p = text.find(':', p); if (p == std::string::npos) return "";
        p = text.find_first_of("tf0-9\"[", p+1); if (p == std::string::npos) return "";
        if (text[p] == '\"') {
            auto e = text.find('\"', p+1);
            return (e == std::string::npos) ? "" : text.substr(p+1, e-p-1);
        }
        if (text[p] == '[') {
            auto e = text.find(']', p);
            return (e == std::string::npos) ? "" : text.substr(p, e-p+1);
        }
        auto e = text.find_first_of(",}\n\r", p+1);
        return text.substr(p, e-p);
    };
    auto b = [&](const std::string& key) -> bool {
        auto v = s(key); return v == "true";
    };
    auto i = [&](const std::string& key) -> int {
        auto v = s(key); return v.empty() ? 0 : std::atoi(v.c_str());
    };
    auto f = [&](const std::string& key) -> float {
        auto v = s(key); return v.empty() ? 0 : (float)std::atof(v.c_str());
    };
    auto c = [&](const std::string& key) -> ColorRGB {
        auto v = s(key);
        if (v.empty() || v[0] != '[') return {1,1,1};
        std::string n = v.substr(1, v.size()-2);
        float r=1,g=1,b=1;
        auto c1 = n.find(','); if (c1==std::string::npos) return {1,1,1};
        auto c2 = n.find(',', c1+1); if (c2==std::string::npos) return {1,1,1};
        r = (float)std::atof(n.substr(0,c1).c_str())/255;
        g = (float)std::atof(n.substr(c1+1,c2-c1-1).c_str())/255;
        b = (float)std::atof(n.substr(c2+1).c_str())/255;
        return {r,g,b};
    };

    cfg.dot_esp          = b("dot_esp");
    cfg.box_esp          = b("box_esp");
    cfg.corner_box       = b("corner_box");
    cfg.skeleton_esp     = b("skeleton_esp");
    cfg.snap_lines       = b("snap_lines");
    cfg.show_names       = b("show_names");
    cfg.show_roles       = b("show_roles");
    cfg.show_distance    = b("show_distance");
    cfg.health_bar       = b("health_bar");
    cfg.shield_bar       = b("shield_bar");
    cfg.enemy_only       = b("enemy_only");
    cfg.radar_enabled    = b("radar_enabled");
    cfg.aimbot_enabled   = b("aimbot_enabled");
    cfg.aimbot_show_fov  = b("aimbot_show_fov");
    cfg.invincible_detect= b("invincible_detect");
    cfg.distance_scaling = b("distance_scaling");
    cfg.disable_buried   = b("disable_buried");
    cfg.background_geo   = b("show_background_geo");
    cfg.show_cursor      = b("show_cursor");
    cfg.draw_all         = b("draw_all");

    cfg.enemy_color      = c("enemy_color");
    cfg.teammate_color   = c("teammate_color");
    cfg.local_color      = c("local_color");
    cfg.unknown_color    = c("unknown_color");
    cfg.hunter_color     = c("hunter_visual_color");
    cfg.survivor_color   = c("survivor_visual_color");
    cfg.visible_color    = c("visible_color");
    cfg.not_visible_color= c("not_visible_color");
    cfg.invincible_color = c("invincible_color");
    cfg.radar_color      = c("radar_color");

    cfg.dot_radius       = i("dot_radius");
    cfg.box_height_world = f("box_height_world");
    cfg.box_y_offset     = i("box_y_offset");
    cfg.line_thickness   = i("line_thickness");
    cfg.point_size       = i("point_size");
    cfg.radar_size       = i("radar_size");
    cfg.radar_range      = f("radar_range");
    cfg.radar_opacity    = i("radar_opacity");
    cfg.aimbot_fov       = i("aimbot_fov");
    cfg.aimbot_smooth    = f("aimbot_smooth");
    cfg.aimbot_target_offset = f("aimbot_target_offset");
    cfg.scale_ref_dist   = f("scale_reference_dist");
    cfg.draw_all_max_dist= f("draw_all_max_distance");
}

static void load_config() {
    wchar_t path[512];
    GetEnvironmentVariableW(L"APPDATA", path, 512);
    PathAppendW(path, L"MecchaCamouflage\\esp_config.json");
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::stringstream ss; ss << f.rdbuf();
    parse_json(ss.str(), g_cfg);
    // Radar range clamping
    if (g_cfg.radar_size < RADAR_MIN) g_cfg.radar_size = RADAR_MIN;
    if (g_cfg.radar_size > RADAR_MAX) g_cfg.radar_size = RADAR_MAX;
}

// ---------------------- Direct2D Init ----------------------
static bool init_d2d(HWND hwnd) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d)))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), (IUnknown**)&g_dwrite)))
        return false;
    RECT rc; GetClientRect(hwnd, &rc);
    if (FAILED(g_d2d->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right-rc.left, rc.bottom-rc.top)),
        &g_rt)))
        return false;
    g_rt->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1), &g_brush);
    g_dwrite->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &g_font);
    return true;
}

// ---------------------- Game Data --------------------------
struct PlayerData {
    uint64_t actor;
    double   pos[3];
    double   head[3];
    float    health, shield;
    bool     invincible, is_local, is_enemy;
    int      role; // 0=unknown 1=hunter 2=survivor
    char     name[64];
    float    dist;
};

struct CameraData {
    double loc[3], rot[3];
    float fov;
    bool  valid;
};

struct TerrainSeg {
    float x1,y1,x2,y2,z;
};

static std::vector<PlayerData> g_players;
static std::vector<PlayerData> g_actors; // draw_all items
static std::vector<TerrainSeg> g_terrain;
static CameraData g_cam = {};
static int g_game_status = 0; // 0=waiting, 1=attached, 2=in-game
static int g_data_tick = 0;

static HWND find_game() {
    return FindWindowW(GAME_WINDOW, nullptr);
}

static void read_game_data() {
    g_data_tick++;
    bool do_read = (g_data_tick % (TARGET_FPS / DATA_UPDATE_HZ) == 0);

    CameraData cam = {};
    cam.valid = mc_read_camera(cam.loc, cam.rot, &cam.fov);
    if (cam.valid) { g_cam = cam; g_game_status = 2; }
    else if (g_cam.valid) { g_game_status = 1; }
    else { g_game_status = 0; }

    if (!do_read) return;
    load_config();

    std::vector<PlayerData> players;
    uint64_t buf[64];
    int32_t n = mc_read_players(buf, 64);
    for (int32_t i = 0; i < n; i++) {
        PlayerData p = {};
        p.actor = buf[i];
        float pf[3];
        if (!mc_read_vec3_f(buf[i] + 0x128, pf)) continue;
        p.pos[0] = pf[0]; p.pos[1] = pf[1]; p.pos[2] = pf[2];
        p.head[0] = pf[0]; p.head[1] = pf[1]; p.head[2] = pf[2] + 80;

        p.health  = mc_player_get_health(buf[i], 0);
        p.shield  = mc_read_float(buf[i] + 0x140);
        p.invincible = mc_player_get_invincible(buf[i]) && g_cfg.invincible_detect;
        p.role    = mc_player_get_role(buf[i]);
        mc_uobject_get_name(buf[i], p.name, sizeof(p.name));
        p.is_local = (i == 0);
        p.is_enemy = (i > 0);
        if (p.health < 0.01f) continue;
        p.dist = (float)sqrt(
            (p.pos[0]-g_cam.loc[0])*(p.pos[0]-g_cam.loc[0]) +
            (p.pos[1]-g_cam.loc[1])*(p.pos[1]-g_cam.loc[1]) +
            (p.pos[2]-g_cam.loc[2])*(p.pos[2]-g_cam.loc[2]));
        players.push_back(p);
    }
    g_players = players;
}

// ---------------------- World to Screen --------------------
static bool w2s(const double pos[3], const CameraData& cam,
                UINT sw, UINT sh, float& sx, float& sy) {
    if (!cam.valid || cam.fov <= 0) return false;
    float p = cam.rot[0]*PI/180, y = cam.rot[1]*PI/180;
    float sp=sinf(p), cp=cosf(p), sy_=sinf(y), cy_=cosf(y);
    double fwd[3]={cp*cy_, cp*sy_, sp}, rgt[3]={-sy_, cy_, 0}, up[3]={-sp*cy_, -sp*sy_, cp};
    double dx=pos[0]-cam.loc[0], dy=pos[1]-cam.loc[1], dz=pos[2]-cam.loc[2];
    double vx=dx*fwd[0]+dy*fwd[1]+dz*fwd[2];
    double vy=dx*rgt[0]+dy*rgt[1]+dz*rgt[2];
    double vz=dx*up[0]+dy*up[1]+dz*up[2];
    if (vx <= 0.1) return false;
    float thf = tanf(cam.fov*PI/360); if (thf <= 0.001f) return false;
    float ndc_x = float(vy/(vx*thf));
    float ndc_y = float(vz/(vx*thf/(float(sw)/float(sh))));
    if (fabsf(ndc_x) > 1.5f || fabsf(ndc_y) > 1.5f) return false;
    sx = (1+ndc_x)*sw/2; sy = (1-ndc_y)*sh/2;
    return true;
}

// ---------------------- Drawing Helpers --------------------
static D2D1_COLOR_F to_d2d(const ColorRGB& c, float a=1) {
    return {c.r, c.g, c.b, a};
}

static float scale_factor(float dist) {
    if (!g_cfg.distance_scaling) return 1;
    return std::max(0.3f, std::min(2.0f, 1500.0f / std::max(100.0f, dist)));
}

static void draw_text(const wchar_t* t, float x, float y, float w, float h,
                       D2D1_COLOR_F c) {
    g_brush->SetColor(c);
    g_rt->DrawText(t, (UINT32)wcslen(t), g_font, D2D1::RectF(x,y,x+w,y+h), g_brush);
}
static void draw_text_s(const wchar_t* t, float x, float y, D2D1_COLOR_F c) {
    draw_text(t,x,y,300,20,c);
}

static void draw_filled_circle(float cx, float cy, float r, D2D1_COLOR_F c) {
    g_brush->SetColor(c);
    g_rt->FillEllipse(D2D1::Ellipse({cx,cy},r,r), g_brush);
}
static void draw_circle(float cx, float cy, float r, D2D1_COLOR_F c, float w=1) {
    g_brush->SetColor(c);
    g_rt->DrawEllipse(D2D1::Ellipse({cx,cy},r,r), g_brush, w);
}
static void draw_outlined_circle(float cx, float cy, float r, D2D1_COLOR_F fill,
                                   D2D1_COLOR_F outline, float w=2) {
    draw_filled_circle(cx,cy,r-1,fill);
    draw_circle(cx,cy,r,outline,w);
}

static void draw_line(float x1, float y1, float x2, float y2,
                       D2D1_COLOR_F c, float w=1) {
    g_brush->SetColor(c);
    g_rt->DrawLine({x1,y1},{x2,y2}, g_brush, w);
}

static void draw_rect(float x, float y, float w, float h, D2D1_COLOR_F c, float lw=1) {
    g_brush->SetColor(c);
    g_rt->DrawRectangle(D2D1::RectF(x-w,y-h,x+w,y+h), g_brush, lw);
}

static void draw_bar(float x, float y, float w, float h, float p, D2D1_COLOR_F c) {
    if (p<0)p=0; if(p>1)p=1;
    g_brush->SetColor({0.1f,0.1f,0.1f,0.5f});
    g_rt->FillRectangle(D2D1::RectF(x,y,x+w,y+h), g_brush);
    if (p > 0.01f) {
        g_brush->SetColor(c);
        g_rt->FillRectangle(D2D1::RectF(x,y,x+w*p,y+h), g_brush);
    }
}

static D2D1_COLOR_F player_color(const PlayerData& p) {
    if (p.is_local) return to_d2d(g_cfg.local_color);
    if (p.is_enemy) return to_d2d(g_cfg.enemy_color);
    return to_d2d(g_cfg.teammate_color);
}
static D2D1_COLOR_F role_color(const PlayerData& p) {
    if (p.role == 1) return to_d2d(g_cfg.hunter_color);
    if (p.role == 2) return to_d2d(g_cfg.survivor_color);
    return to_d2d(g_cfg.unknown_color);
}

// ---------------------- ESP Rendering ----------------------
static void render_dot(float sx, float sy, float dist, const PlayerData& p) {
    float r = g_cfg.dot_radius * scale_factor(dist);
    if (p.invincible)
        draw_outlined_circle(sx, sy, r, to_d2d(g_cfg.invincible_color), player_color(p), 2);
    else
        draw_filled_circle(sx, sy, r, player_color(p));
}

static void render_box(float sx, float sy, float dist, const PlayerData& p) {
    float s = scale_factor(dist);
    float hw = g_cfg.box_height_world;
    float h = hw * 500.0f / std::max(dist, 100.0f) * s;
    float w = h * 0.5f;
    draw_rect(sx, sy + g_cfg.box_y_offset, w, h, player_color(p),
              (float)g_cfg.line_thickness);
}

static void render_corner_box(float sx, float sy, float dist, const PlayerData& p) {
    float s = scale_factor(dist);
    float h = 100 * 500.0f / std::max(dist, 100.0f) * s;
    float w = h * 0.5f;
    float l = 6;
    float y = sy > 5 ? sy : sy;
    auto col = player_color(p);
    // Top-left
    draw_line(sx-w, y, sx-w+l, y, col); draw_line(sx-w, y, sx-w, y+l, col);
    // Top-right
    draw_line(sx+w, y, sx+w-l, y, col); draw_line(sx+w, y, sx+w, y+l, col);
    // Bottom-left
    draw_line(sx-w, y+h, sx-w+l, y+h, col); draw_line(sx-w, y+h, sx-w, y+h-l, col);
    // Bottom-right
    draw_line(sx+w, y+h, sx+w-l, y+h, col); draw_line(sx+w, y+h, sx+w, y+h-l, col);
}

static void render_skeleton(float sx, float sy, float dist, const PlayerData& p) {
    // Simplified T-pose skeleton using facing direction
    float s = scale_factor(dist);
    float h = 70 * 500.0f / std::max(dist, 100.0f) * s;
    float aw = h * 0.4f;
    float lw = h * 0.35f;
    auto col = player_color(p);
    // Spine (head to pelvis)
    float pelvis_y = sy + h;
    draw_line(sx, sy, sx, pelvis_y, col);
    // Arms (shoulder width)
    float shoulder_y = sy + h * 0.25f;
    draw_line(sx - aw, shoulder_y, sx + aw, shoulder_y, col);
    draw_line(sx - aw, shoulder_y, sx - aw, shoulder_y + h*0.15f, col);
    draw_line(sx + aw, shoulder_y, sx + aw, shoulder_y + h*0.15f, col);
    // Legs
    draw_line(sx, pelvis_y, sx - lw, pelvis_y + h*0.5f, col);
    draw_line(sx, pelvis_y, sx + lw, pelvis_y + h*0.5f, col);
}

static void render_snap_line(float sx, float sy, UINT sh, const PlayerData& p) {
    auto col = p.is_enemy ? to_d2d(g_cfg.enemy_color) : to_d2d(g_cfg.teammate_color);
    draw_line(sx, sy, sx, (float)sh, col, (float)g_cfg.line_thickness);
}

static void render_health_bar(float sx, float y, float dist, const PlayerData& p) {
    float s = scale_factor(dist);
    float w = 30 * s, h = 4 * s;
    float x = sx - w/2;
    draw_bar(x, y, w, h, p.health/100.0f, to_d2d(g_cfg.visible_color));
    if (g_cfg.shield_bar && p.shield > 0) {
        draw_bar(x, y - h - 1, w, h, p.shield/100.0f, {0.2f,0.4f,0.9f,1});
    }
}

static void render_info(float sx, float y, const PlayerData& p) {
    wchar_t buf[256];
    if (g_cfg.show_names) {
        wchar_t wname[64]; MultiByteToWideChar(CP_UTF8,0,p.name,-1,wname,64);
        if (g_cfg.show_distance)
            swprintf_s(buf, L"%s [%.0fm]", wname, p.dist/100);
        else
            wcscpy_s(buf, wname);
        draw_text_s(buf, sx - 50, y, role_color(p));
    }
    if (g_cfg.show_roles) {
        const wchar_t* r = p.role==1 ? L"\U0001F3A9 Hunter" :
                           p.role==2 ? L"\U0001F46B Survivor" : L"?";
        swprintf_s(buf, L"%s%s", r, p.invincible ? L" \U00002B50 INV" : L"");
        draw_text_s(buf, sx - 50, y + 14, role_color(p));
    }
}

// ---------------------- Radar ------------------------------
static void render_radar(UINT sw, UINT sh) {
    if (!g_cfg.radar_enabled) return;
    int r_size = g_cfg.radar_size;
    int r_x = 10, r_y = sh - r_size - 10;
    float cx = r_x + r_size/2.0f, cy = r_y + r_size/2.0f;
    float range = std::max(g_cfg.radar_range, 1000.0f);
    float scale = (r_size/2.0f) / range;

    // Background
    g_brush->SetColor({0.05f,0.05f,0.08f, g_cfg.radar_opacity/255.0f});
    g_rt->FillEllipse({cx,cy,(float)r_size/2,(float)r_size/2}, g_brush);
    draw_circle(cx, cy, r_size/2, {0.3f,0.3f,0.4f,1}, 1);

    // Terrain (simplified dots if background_geo enabled)
    if (g_cfg.background_geo && !g_terrain.empty()) {
        for (auto& seg : g_terrain) {
            float dx = (seg.x1 + seg.x2)/2 - (float)g_cam.loc[0];
            float dy = (seg.y1 + seg.y2)/2 - (float)g_cam.loc[1];
            float rx = dy * scale, ry = -dx * scale;
            if (fabsf(rx) < r_size/2 && fabsf(ry) < r_size/2)
                draw_filled_circle(cx+rx, cy+ry, 0.5f, {0.3f,0.3f,0.4f,0.5f});
        }
    }

    // Player dots
    for (auto& p : g_players) {
        float dx = (float)(p.pos[1] - g_cam.loc[1]);
        float dy = (float)(p.pos[0] - g_cam.loc[0]);
        float rx = dx * scale, ry = -dy * scale;
        if (fabsf(rx) > r_size/2 || fabsf(ry) > r_size/2) continue;
        float dot_r = p.is_local ? 3 : 2;
        D2D1_COLOR_F col = p.is_local ? to_d2d(g_cfg.local_color) :
                            p.is_enemy ? to_d2d(g_cfg.enemy_color) :
                                         to_d2d(g_cfg.teammate_color);
        draw_filled_circle(cx+rx, cy+ry, dot_r, col);
    }

    // Border
    draw_circle(cx, cy, r_size/2, to_d2d(g_cfg.radar_color), 1);
}

// ---------------------- Main Render ------------------------
static void render_frame() {
    if (!g_rt || g_overlay != GetForegroundWindow()) return;
    RECT rc; GetClientRect(g_overlay, &rc);
    UINT sw = rc.right - rc.left, sh = rc.bottom - rc.top;
    if (sw == 0 || sh == 0) return;

    g_rt->BeginDraw();
    g_rt->Clear({0,0,0,0});

    if (g_game_status == 0) {
        draw_text_s(L"Waiting for game...", (float)sw/2-80, (float)sh/2, {0.5f,0.5f,0.5f,1});
    } else {
        for (auto& p : g_players) {
            float sx, sy;
            if (!w2s(p.pos, g_cam, sw, sh, sx, sy)) continue;
            if (g_cfg.enemy_only && !p.is_enemy) continue;
            if (p.is_local && !g_cfg.show_distance) continue;
            if (g_cfg.disable_buried && p.dist < 50) continue;

            float s = scale_factor(p.dist);

            if (!p.is_local) {
                if (g_cfg.snap_lines) render_snap_line(sx, sy, sh, p);
                if (g_cfg.dot_esp)    render_dot(sx, sy, p.dist, p);
            } else {
                if (g_cfg.dot_esp)    render_dot(sx, sy, p.dist, p);
            }
            if (g_cfg.box_esp)        render_box(sx, sy, p.dist, p);
            if (g_cfg.corner_box)     render_corner_box(sx, sy, p.dist, p);
            if (g_cfg.skeleton_esp)   render_skeleton(sx, sy, p.dist, p);
            if (g_cfg.health_bar)     render_health_bar(sx, sy+5, p.dist, p);
            if (g_cfg.show_names || g_cfg.show_roles)
                render_info(sx, sy+10, p);
        }

        // Aimbot FOV circle
        if (g_cfg.aimbot_enabled && g_cfg.aimbot_show_fov) {
            float fov_r = g_cfg.aimbot_fov * (float)sh / 90.0f;
            draw_circle((float)sw/2, (float)sh/2, std::min(fov_r, (float)std::max(sw,sh)),
                        {0,1,0,0.3f}, 1);
        }

        render_radar(sw, sh);

        // Status
        wchar_t st[128];
        swprintf_s(st, L"Players: %zu | FOV: %.0f", g_players.size(), g_cam.fov);
        draw_text_s(st, 10, 10, {0.7f,0.7f,0.7f,1});
    }

    g_rt->EndDraw();
}

// ---------------------- Window Proc ------------------------
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY: g_running = false; PostQuitMessage(0); return 0;
    case WM_KEYDOWN:
        if (wp == VK_END) { g_running = false; DestroyWindow(hwnd); }
        if (wp == VK_F1 || wp == VK_INSERT) {
            // Signal Python menu to toggle via a named event
            HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"MecchaMenuToggle");
            if (ev) { SetEvent(ev); CloseHandle(ev); }
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------- Main -------------------------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Init memory engine with retry
    for (int i = 0; i < 30; i++) {
        if (mc_init()) break;
        Sleep(2000);
    }
    load_config();

    WNDCLASSEXW wc = {sizeof(wc), CS_HREDRAW|CS_VREDRAW, wnd_proc};
    wc.hInstance = hInst; wc.hCursor = LoadCursor(0,IDC_ARROW);
    wc.lpszClassName = OVERLAY_CLASS;
    RegisterClassExW(&wc);

    for (int i = 0; i < 30; i++) { g_game = find_game(); if (g_game) break; Sleep(1000); }
    if (!g_game) return 1;
    GetWindowRect(g_game, &g_rect);

    int w_px = g_rect.right - g_rect.left, h_px = g_rect.bottom - g_rect.top;
    g_overlay = CreateWindowExW(
        WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_NOACTIVATE,
        OVERLAY_CLASS, L"Meccha Overlay", WS_POPUP,
        g_rect.left, g_rect.top, w_px, h_px, 0, 0, hInst, 0);
    if (!g_overlay) return 1;
    SetLayeredWindowAttributes(g_overlay, 0, 255, LWA_ALPHA);
    SetWindowPos(g_overlay, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);

    if (!init_d2d(g_overlay)) return 1;
    ShowWindow(g_overlay, SW_SHOW);

    // Ensure cursor is visible if configured
    if (g_cfg.show_cursor) ShowCursor(TRUE);

    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, 0,0,0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (GetWindowRect(g_game, &g_rect)) {
            SetWindowPos(g_overlay, HWND_TOPMOST,
                g_rect.left, g_rect.top,
                g_rect.right-g_rect.left, g_rect.bottom-g_rect.top,
                SWP_SHOWWINDOW);
        }
        read_game_data();
        render_frame();
        Sleep(TICK_MS);
    }

    if (g_font)  g_font->Release();
    if (g_brush) g_brush->Release();
    if (g_rt)    g_rt->Release();
    if (g_dwrite)g_dwrite->Release();
    if (g_d2d)   g_d2d->Release();
    mc_cleanup();
    return 0;
}
