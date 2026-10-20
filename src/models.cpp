// models.cpp: the point ROM's models (docs/dsp.md), read in memory.
//
// The model table is the ROM's start: a code's word points to its list of
// parts, ended by a word that is no address. A part: its length, then
// (as signed 16-bit words) a vertex count, its depth bias for the board's
// polygon sort, the vertices (x y z), a face count and the faces (4 corners
// and a color word). Here the parts are one mesh: corners at the same place
// merged, faces of fewer than 3 distinct corners dropped.
#include "models.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace {

int16_t s16(int32_t v) { return (int16_t)(v & 0xffff); }

// Models drawn as lines (too many lines for a vector monitor as outlines). A
// thin face (its area under a twentieth of its length, squared) becomes its
// longest diagonal; another face, its edges.
const int STRIPE_WIDTH = 8000;          // the horizon band: a stripe across the screen

struct Body {
    std::vector<vp_vec3> verts;
    std::vector<SbModel::Face> faces;
};

double d2(const vp_vec3 &a, const vp_vec3 &b)
{
    double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}

void asLines(const Body &body, bool halfStripes, bool allThin, SbModel &out)
{
    const std::vector<vp_vec3> &v = body.verts;
    struct L { int a, b; uint16_t color; };
    std::vector<L> lines, thin;
    std::set<std::pair<int, int>> seen;
    for (const SbModel::Face &f : body.faces) {
        const std::vector<int> &c = f.corners;
        const size_t n = c.size();
        double span = 0;
        int la = c[0], lb = c[1];                       // the longest diagonal: the first longest pair
        for (size_t i = 0; i < n; i++)
            for (size_t j = i + 1; j < n; j++) {
                double d = d2(v[c[i]], v[c[j]]);
                if (d > span) { span = d; la = c[i]; lb = c[j]; }
            }
        double nx = 0, ny = 0, nz = 0;                  // twice its area (Newell)
        for (size_t i = 0; i < n; i++) {
            const vp_vec3 &p = v[c[i]], &q = v[c[(i + 1) % n]];
            nx += (double)(p.y - q.y) * (p.z + q.z);
            ny += (double)(p.z - q.z) * (p.x + q.x);
            nz += (double)(p.x - q.x) * (p.y + q.y);
        }
        const double area = std::sqrt(nx * nx + ny * ny + nz * nz) / 2;
        if (allThin || area * 20 < span) {
            thin.push_back({ la, lb, f.color });
        } else {
            for (size_t i = 0; i < n; i++) {
                int a = c[i], b = c[(i + 1) % n];
                if (seen.insert({ std::min(a, b), std::max(a, b) }).second) lines.push_back({ a, b, f.color });
            }
        }
    }
    if (halfStripes) {
        // The full-width stripes, bottom to top: every other one goes.
        std::vector<size_t> stripes;
        for (size_t i = 0; i < thin.size(); i++)
            if (std::fabs(v[thin[i].a].x - v[thin[i].b].x) >= STRIPE_WIDTH) stripes.push_back(i);
        std::stable_sort(stripes.begin(), stripes.end(), [&](size_t x, size_t y) {
            return v[thin[x].a].y + v[thin[x].b].y < v[thin[y].a].y + v[thin[y].b].y;
        });
        std::vector<bool> drop(thin.size(), false);
        for (size_t k = 1; k < stripes.size(); k += 2) drop[stripes[k]] = true;
        std::vector<L> kept;
        for (size_t i = 0; i < thin.size(); i++)
            if (!drop[i]) kept.push_back(thin[i]);
        thin.swap(kept);
    }
    lines.insert(lines.end(), thin.begin(), thin.end());
    for (const L &l : lines) out.lines.push_back({ v[l.a], v[l.b], l.color });
}

} // namespace

bool sb_isStreak(int code) { return (code >= 1297 && code <= 1320) || (code >= 4743 && code <= 4766); }
bool sb_isBackdrop(int code) { return code == 2 || code == 3448; }

bool sb_model(const std::vector<int32_t> &rom, int code, SbModel &out)
{
    out = SbModel();
    const int32_t size = (int32_t)rom.size();
    auto get = [&](int32_t i) { return i >= 0 && i < size ? rom[(size_t)i] : -1; };

    // The table ends where the first model's data begins.
    static int32_t end = -1;
    static const std::vector<int32_t> *endOf = nullptr;
    if (endOf != &rom) {
        end = size;
        for (int32_t i = 0; i < 0x4000 && i < size; i++)
            if (rom[(size_t)i] >= 0 && rom[(size_t)i] < end) end = rom[(size_t)i];
        endOf = &rom;
    }
    if (code < 0 || code >= end) return false;

    const int32_t master = get(code);
    if (master < 0 || master >= size) return false;
    Body body;
    std::map<std::tuple<int, int, int>, int> index;     // a place -> its merged vertex
    for (int k = 0; k < 1024; k++) {                    // parts until the marker
        const int32_t sub = get(master + k);
        if (sub < 0 || sub >= size) break;
        const int32_t n = get(sub) & 0xffff;
        if (n <= 2 || sub + n >= size) continue;
        auto d = [&](int32_t i) { return i < n ? (int)s16(get(sub + 1 + i)) : 0; };
        const int nv = d(1);
        int p = 3 + 3 * nv;
        if (nv <= 0 || p >= n) continue;
        const int nq = d(p);
        p += 1;
        if (nq <= 0 || p + 5 * nq > n) continue;
        for (int q = 0; q < nq; q++, p += 5) {
            int c[4];
            bool ok = true;
            for (int i = 0; i < 4; i++) {
                c[i] = d(p + i);
                if (c[i] < 0 || c[i] >= nv) ok = false;
            }
            if (!ok || std::set<int>(c, c + 4).size() < 3) continue;
            SbModel::Face f;
            f.color = (uint16_t)(d(p + 4) & 0xffff);
            for (int i = 0; i < 4; i++) {
                const int x = d(3 + 3 * c[i]), y = d(4 + 3 * c[i]), z = d(5 + 3 * c[i]);
                auto it = index.find({ x, y, z });
                int vi;
                if (it != index.end()) {
                    vi = it->second;
                } else {
                    vi = (int)body.verts.size();
                    index[{ x, y, z }] = vi;
                    body.verts.push_back(vp_v3((float)x, (float)y, (float)z));
                }
                if (f.corners.empty() || f.corners.back() != vi) f.corners.push_back(vi);
            }
            while (f.corners.size() > 1 && f.corners.back() == f.corners.front()) f.corners.pop_back();
            if (f.corners.size() >= 3) body.faces.push_back(f);
        }
    }
    if (body.faces.empty()) return false;

    if (sb_isStreak(code) || sb_isBackdrop(code)) {
        asLines(body, sb_isBackdrop(code), sb_isStreak(code), out);
        return !out.lines.empty();
    }
    out.verts.swap(body.verts);
    out.faces.swap(body.faces);
    return true;
}
