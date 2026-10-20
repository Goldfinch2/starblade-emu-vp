// render3d.cpp: the master and slave DSPs' work (docs/dsp.md), done in float:
// object matrix, camera matrix, projection; the models drawn as outlines in
// an SDK scene, hidden lines removed, instead of filled polygons.
#include "render3d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "machine.h"
#include "models.h"

namespace {

struct Mat { float m[9]; };

Mat mul(const Mat &a, const Mat &b)
{
    Mat r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    return r;
}

Mat rotX(float s, float c) { return {{ 1, 0, 0,  0, c, -s,  0, s, c }}; }
Mat rotY(float s, float c) { return {{ c, 0, s,  0, 1, 0,  -s, 0, c }}; }
Mat rotZ(float s, float c) { return {{ c, -s, 0,  s, c, 0,  0, 0, 1 }}; }

int16_t s16(uint16_t v) { return (int16_t)v; }

// The six rotation orders of the master DSP (routines 892Bh..8A66h):
// angle 1 turns about X, angle 2 about Y, angle 3 about Z.
// `w` points at s1, c1, s2, c2, s3, c3 (Q15).
Mat rotation(const uint16_t *w, int order)
{
    const float k = 1.0f / 32768.0f;
    Mat x = rotX(s16(w[0]) * k, s16(w[1]) * k);
    Mat y = rotY(s16(w[2]) * k, s16(w[3]) * k);
    Mat z = rotZ(s16(w[4]) * k, s16(w[5]) * k);
    switch (order) {
    default:
    case 0: return mul(mul(z, y), x);
    case 1: return mul(mul(y, z), x);
    case 2: return mul(mul(z, x), y);
    case 3: return mul(mul(x, z), y);
    case 4: return mul(mul(y, x), z);
    case 5: return mul(mul(x, y), z);
    }
}

vp_vec3 apply(const Mat &a, vp_vec3 p)
{
    return vp_v3(a.m[0] * p.x + a.m[1] * p.y + a.m[2] * p.z,
                 a.m[3] * p.x + a.m[4] * p.y + a.m[5] * p.z,
                 a.m[6] * p.x + a.m[7] * p.y + a.m[8] * p.z);
}

// A color made as bright as it can be (the hue kept): on a vector monitor a
// dark raster shade is a faint line. Black: a light blue.
void brightColor(uint32_t rgb, uint8_t *out)
{
    int r = rgb >> 16 & 0xff, g = rgb >> 8 & 0xff, b = rgb & 0xff;
    int top = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (top < 8) { out[0] = 120; out[1] = 160; out[2] = 255; return; }
    out[0] = (uint8_t)(r * 255 / top);
    out[1] = (uint8_t)(g * 255 / top);
    out[2] = (uint8_t)(b * 255 / top);
}

// The horizon band: model 2 (also 3448), a strip the game keeps 16 to 3300 units in
// front of the camera, while the ships and planets it must stay behind are
// farther (the board sorted it last with a depth bias of 15000). A backdrop:
// pushed out along the lines of sight by BACKDROP_SCALE, it shows on the
// screen where it did, behind everything.
const float BACKDROP_SCALE = 4096.0f;

// The warp: up to 421 streaks per frame (sb_isStreak): more than a vector
// monitor redraws without flicker. Only the nearest MAX_STREAKS are drawn:
// the long, fast ones; the far ones are specks at the center.
const int MAX_STREAKS = 15;

// An object record's length in words, or 0 (the end, or a type not known).
size_t recordLength(uint16_t type)
{
    return (type == 1 || type == 2) ? 13 : type == 0 ? 6 : type == 0x100 ? 11 : type == 0x101 ? 14 : 0;
}

} // namespace

Render3d::Render3d() = default;

Render3d::~Render3d()
{
    for (auto &m : models_) vp3d_mesh_free(m.second.mesh);
    vp3d_scene_free(scene_);
}

int Render3d::open(const std::vector<int32_t> &points)
{
    points_ = &points;
    return vp3d_scene_create(&scene_);
}

// A model, built on first use from the point ROM (models.cpp). Every
// polygon edge drawn (crease 0), and every face hides from both sides: many
// of the models are open shapes (rings, plates, the tunnel). A face's color
// word picks palette entry 3E00h + its low byte, in the upper 256 of that
// block unless bit 200h is set.
Render3d::Model &Render3d::model(int code)
{
    auto it = models_.find(code);
    if (it != models_.end()) return it->second;
    Model &m = models_[code];
    SbModel rom;
    if (!points_ || !sb_model(*points_, code, rom)) return m;
    auto entryOf = [](int word) { return (uint16_t)((word & 0xff) | (word & 0x200 ? 0 : 0x100)); };
    // A model of lines only (the warp's streaks, the horizon band): drawn
    // line by line, each in its own color (the SDK's lines have none).
    if (!rom.lines.empty()) {
        for (const SbModel::Line &l : rom.lines) m.lines.push_back({ l.a, l.b, entryOf(l.color) });
        return m;
    }
    if (vp3d_mesh_create(&m.mesh) != VP_OK) {
        m.mesh = nullptr;
        return m;
    }
    for (const vp_vec3 &v : rom.verts) vp3d_mesh_vertex(m.mesh, v.x, v.y, v.z);
    for (const SbModel::Face &f : rom.faces) {
        vp3d_mesh_face(m.mesh, f.corners.data(), (int)f.corners.size());
        m.entry.push_back(entryOf(f.color));
    }
    if (vp3d_mesh_finish(m.mesh) != VP_OK) {
        vp3d_mesh_free(m.mesh);
        m.mesh = nullptr;
        m.entry.clear();
        return m;
    }
    vp3d_mesh_set_crease(m.mesh, 0.0f);
    vp3d_mesh_set_two_sided(m.mesh, 1);
    return m;
}

void Render3d::draw(vp_dvg *d, const Dsp::Frame &frame, const Machine &machine)
{
    if (!frame.valid || !scene_) return;
    for (int e = 0; e < 512; e++) brightColor(machine.paletteRgb(0x3e00 | e), rgb_[e]);
    const uint16_t *cam = frame.camera;

    // The lens: screen x = 248 + 582 vx/vz, y = 240 - 797 vy/vz on the
    // 496 x 480 picture, which fills the tube; the focal lengths scale with
    // the zoom word (8192 when fitted). As the SDK's view: the horizontal
    // field of view, and the vertical focal length over the horizontal one.
    float zoom = cam[2] ? cam[2] / 8192.0f : 1.0f;
    float fx = 4096.0f * 582.0f / 496.0f * zoom;
    float fy = 4096.0f * 797.0f / 480.0f * zoom;
    vp3d_view view;
    vp3d_view_init(&view, 2.0f * std::atan(2048.0f / fx) * 180.0f / 3.14159265f);
    view.aspect = fy / fx;
    view.near_z = 16.0f;
    view.far_z = 1.0e8f;                // the backdrop, pushed out (BACKDROP_SCALE)

    int camOrder = s16(cam[0x26]);
    if (camOrder < 0 || camOrder > 5) camOrder = 0;
    const Mat M = mul(rotation(cam + 3, 4), rotation(cam + 0x20, camOrder));

    const std::vector<uint16_t> &list = frame.list;
    auto isStreak = [](int code) { return sb_isStreak(code); };
    // The warp streaks' depths (all type 2: in camera space), to keep the nearest.
    streakDepth_.clear();
    for (size_t i = 0; i < list.size() && list[i] != 0xffff;) {
        size_t len = recordLength(list[i]);
        if (!len || i + len > list.size()) break;
        if (list[i] == 2 && isStreak((list[i + 1] + 1) & 0xffff)) streakDepth_.push_back(s16(list[i + 5]));
        i += len;
    }
    int farthest = 0x7fffffff, atFarthest = 0;  // draw streaks nearer than farthest, and atFarthest at it
    if ((int)streakDepth_.size() > MAX_STREAKS) {
        std::nth_element(streakDepth_.begin(), streakDepth_.begin() + (MAX_STREAKS - 1), streakDepth_.end());
        farthest = streakDepth_[MAX_STREAKS - 1];
        atFarthest = MAX_STREAKS;
        for (int z : streakDepth_)
            if (z < farthest) atFarthest--;
    }

    vp3d_scene_begin(scene_, d, &view);
    size_t i = 0;
    while (i < list.size() && list[i] != 0xffff) {
        uint16_t type = list[i];
        size_t len = recordLength(type);
        if (!len || i + len > list.size()) break;
        const uint16_t *r = &list[i];
        i += len;
        if (type == 0x100 || type == 0x101) continue;       // not decoded yet

        int code = (r[1] + 1) & 0xffff;
        if (type == 2 && farthest != 0x7fffffff && isStreak(code)) {
            int z = s16(r[5]);
            if (z > farthest || (z == farthest && atFarthest-- <= 0)) continue;
        }
        Model &mdl = model(code);
        if (!mdl.mesh && mdl.lines.empty()) continue;
        // This frame's colors (the scene keeps them as it is added).
        for (size_t f = 0; f < mdl.entry.size(); f++) {
            const uint8_t *c = rgb_[mdl.entry[f]];
            vp3d_mesh_face_color(mdl.mesh, (int)f, c[0], c[1], c[2]);
        }
        vp_vec3 pos = vp_v3((float)s16(r[3]), (float)s16(r[4]), (float)s16(r[5]));
        int order = len == 13 ? s16(r[12]) : 0;
        if (order < 0 || order > 5) order = 0;
        const Mat R = len == 13 ? rotation(r + 6, order) : Mat{{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }};
        // Type 2: in the camera's own space (the player's shots, aimed at the
        // reticle): the camera does not turn them.
        const Mat C = type == 2 ? Mat{{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }} : M;
        const Mat MR = mul(C, R);
        const vp_vec3 t = apply(C, pos);
        vp_xf place;
        const float scale = sb_isBackdrop(code) ? BACKDROP_SCALE : 1.0f;
        for (int k = 0; k < 9; k++) place.m[k] = MR.m[k] * scale;
        place.t[0] = t.x * scale; place.t[1] = t.y * scale; place.t[2] = t.z * scale;
        if (!mdl.lines.empty()) {
            // Lines only: hidden by the solids in front, hiding nothing.
            for (const Model::Line &l : mdl.lines) {
                const uint8_t *c = rgb_[l.entry];
                vp_color(d, c[0], c[1], c[2]);
                vp3d_scene_line(scene_, vp_xf_point(&place, l.a), vp_xf_point(&place, l.b));
            }
            continue;
        }
        vp3d_scene_mesh(scene_, mdl.mesh, &place, VP3D_SOLID);
    }
    vp3d_scene_end(scene_);
}
