// render3d.h: what the DSP board draws, as vectors, through the SDK's 3D
// (vp_3d.h). Each frame's camera block and object list (docs/dsp.md) place
// the point ROM's models (read from the ROM: models.cpp) in a scene, which
// hides the lines behind solid faces. Colors: each face's color word looked
// up in the game's palette as it is that frame.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <vectorpie/vp_3d.h>

#include "hle.h"

class Machine;

class Render3d {
public:
    Render3d();
    ~Render3d();
    int open(const std::vector<int32_t> &points);     // the point ROM (Roms::points)
    // hides: areas of the screen the 3D is hidden behind (the HUD's panels,
    // Sprites::hides): convex polygons, board units, x y pairs.
    void draw(vp_dvg *d, const Dsp::Frame &frame, const Machine &machine,
              const std::vector<std::vector<float>> &hides);

private:
    struct Model {
        vp3d_mesh *mesh = nullptr;      // nullptr: no such model
        std::vector<uint16_t> entry;    // per face: its palette entry in the 3D block (0-511)
        std::vector<bool> ground;       // per face: flat at the model's foot (a drawing on the floor): dimmed
        struct Line { vp_vec3 a, b; uint16_t entry; };
        std::vector<Line> lines;        // a model of lines only (no faces): its lines, each in its color
        std::vector<vp_vec3> verts;     // its points (the floor: where it touches the ground)
        float minY = 0;                 // its lowest point
        bool flat = false;              // lies on the floor (Cyber Sled's drawings on the ground): dimmed
    };
    // The floor this frame (profile.h floorCell): false when there is none.
    bool findFloor(const std::vector<uint16_t> &list, float &y, float &offX, float &offZ);
    Model &model(int code);

    vp3d_scene *scene_ = nullptr;
    const std::vector<int32_t> *points_ = nullptr;
    std::unordered_map<int, Model> models_;         // by code, built on first use
    uint8_t rgb_[512][3];                           // the 3D palette this frame, brightened
    std::vector<int> streakDepth_;                  // the warp streaks this frame (scratch)
};
