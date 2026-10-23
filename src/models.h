// models.h: the point ROM's 3D models, read from the player's starblad.zip
// as the game asks for them (docs/dsp.md), nothing extracted to files.
// A model is one mesh, its parts' shared corners merged; a face keeps its
// color word. A few models are drawn as lines instead (isLines).
#pragma once

#include <cstdint>
#include <vector>

#include <vectorpie/vp_3d.h>

struct SbModel {
    struct Face { std::vector<int> corners; uint16_t color; };   // 3 or 4 corners
    struct Line { vp_vec3 a, b; uint16_t color; };
    std::vector<vp_vec3> verts;         // the ROM's own coordinates
    std::vector<Face> faces;            // a model of faces ...
    std::vector<Line> lines;            // ... or of lines (isLines)
};

// Models drawn as lines, the game's (profile.h). Streaks (Star Blade's warp:
// 24 lengths of one diamond, up to 421 a frame): each as its long diagonal.
bool sb_isStreak(int code);
// Backdrops (Star Blade's horizon band): pushed out behind everything
// (render3d.cpp), its stripes as lines, one in two kept.
bool sb_isBackdrop(int code);

// The model the game's object list calls `code`. False if there is none.
bool sb_model(const std::vector<int32_t> &points, int code, SbModel &out);
