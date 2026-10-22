// Star Blade for VectorPie: Namco's 1991 game, its own 68000 code running on
// Musashi, its 3D and sprites drawn as vectors, its sound board running its own
// program (soundboard.cpp).
// Needs MAME's starblad.zip (the cabinet's ROM folders are searched).
//
//   starblade_emu [--roms PATH] [--dvg [PORT] | --window | --windowed | --maximized]
//   TAB, or START 1 and START 2 together: the CONTROLS page (the player's own
//   controls for the gun, missile and aim keys, kept in save/controls.ini;
//   the stick's dead zone and the aim speed, in save/options.ini)
//   P (ui_pause): pause / resume
//
// Development tools, with SB_DEV 1 (below):
//   --sprite-ids: sprites not traced yet drawn as a box with their ID
//   F5: save the game's state (save/state.bin); F7: load it, at any time,
//   e.g. right after starting: back to that moment
//   --record SECONDS: load save/state.bin, then for SECONDS write a capture
//               every RECORD_EVERY frames (save/record_<frame>.png at half
//               size, and .txt: the sprites), then quit
//   P: pausing also saves save/capture_<frame>.png (and .bin, .txt: the sprites, .pal: the palette, _sound.txt: the sound board)
//   --dump DIR: for comparing with MAME (tools/sb_dump.lua): the DSP RAM
//               every 300 frames (sbdump_dspram_<frame>.bin, 64 KB big-endian)
//               and trace.txt (each frame: page select, records drawn by type)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <vectorpie/vp.h>

#include "hle.h"
#include "machine.h"
#include "render3d.h"
#include "roms.h"
#include "soundboard.h"
#include "state.h"
#include "sprites.h"

namespace {

// The development tools: F5 / F7 save states, --record, --dump,
// --sprite-ids and the pause capture (P still pauses). 0 for the game as
// shipped; 1 to have them back. Kept compiled either way.
#define SB_DEV 0
constexpr bool DEV = SB_DEV != 0;

const long BOOT_FRAMES = 5000;          // the self test is skipped up to here at most
const int RECORD_EVERY = 6;             // --record: a capture every 6 frames, 10 a second

// The functions a player lays out (vp_controls.h); out of the box the
// cabinet's. The stick and the trackball aim as they are; the aim keys push
// the sight like a stick (hle.cpp).
struct Function { const char *name, *label, *standard; };
const Function FUNCTIONS[] = {
    { "gun",       "GUN",         "p1_button1" },
    { "missile",   "MISSILE",     "p1_button2" },
    { "aim_up",    "AIM UP",      "p1_up" },
    { "aim_down",  "AIM DOWN",    "p1_down" },
    { "aim_left",  "AIM LEFT",    "p1_left" },
    { "aim_right", "AIM RIGHT",   "p1_right" },
};
const int NFUNCTIONS = (int)(sizeof FUNCTIONS / sizeof FUNCTIONS[0]);
// (Star Blade has no view change: the I/O board's button 3 only steps
// through its service menus.)

// The CONTROLS page's two settings, after the functions: the stick's dead
// zone (a precise yoke wants little or none) and the aim speed (the stick's
// range, the aim keys' and the mouse's pace). Then EXIT.
const int DEADZONES[] = { 0, 2, 5, 10, 15, 20 };        // percent of the travel
const int DEADZONE_STANDARD = 4;                        // 15 %, the SDK's
struct AimSpeed { const char *name; float k; };
const AimSpeed SPEEDS[] = { { "SLOW", 0.6f }, { "NORMAL", 1.0f }, { "FAST", 1.5f }, { "FASTER", 2.0f } };
const int SPEED_STANDARD = 1;
const int ROW_DEADZONE = NFUNCTIONS, ROW_SPEED = NFUNCTIONS + 1, ROW_EXIT = NFUNCTIONS + 2, ROWS = NFUNCTIONS + 3;
const int KEY_TAB = 43, KEY_DELETE = 76, KEY_KP_PERIOD = 99, KEY_F5 = 62, KEY_F7 = 64;   // SDL scancodes

struct Game {
    std::string romPath;
    bool spriteIds = false;
    bool paused = false;
    std::string dumpDir;
    FILE *trace = nullptr;
    std::string error;
    Roms roms;
    Mcu mcu;
    Sound sound;
    Dsp dsp;
    std::unique_ptr<Machine> machine;
    std::unique_ptr<Render3d> render3d;
    std::unique_ptr<Sprites> sprites;
    // The CONTROLS page: open (the game stands still), the cursor's row
    // (ROW_DEADZONE, ROW_SPEED, ROW_EXIT after the functions), a function
    // being laid out, what the page says; the two settings.
    bool controls = false;
    int ctlRow = 0;
    int deadzone = DEADZONE_STANDARD;   // index in DEADZONES
    int speed = SPEED_STANDARD;         // index in SPEEDS
    vp_capture capture{};
    std::string said;
    bool holdStarts = false;            // after the page: START is not the game's until let go
    double recordSeconds = 0;           // --record: how long; 0: not recording
    long recordStart = -1;              // the frame it started at (after loading the state)
    std::string stateSaid;              // "STATE SAVED" ..., shown for a while
    double stateSaidUntil = 0;
};

// ---- save states (F5 / F7) -------------------------------------------------------

const char STATE_MAGIC[8] = { 'S', 'B', 'E', 'M', 'U', 'S', 'T', '1' };

std::string statePath()
{
    return std::string(vp_save_dir()) + "/state.bin";
}

void say(Game &s, vp_game *g, const char *what)
{
    s.stateSaid = what;
    s.stateSaidUntil = vp_game_time(g) + 2.0;
}

void saveState(Game &s, vp_game *g)
{
    State st(true);
    st.bytes(const_cast<char *>(STATE_MAGIC), sizeof STATE_MAGIC);
    s.machine->state(st);
    FILE *f = fopen(statePath().c_str(), "wb");
    bool ok = f && fwrite(st.data().data(), 1, st.data().size(), f) == st.data().size();
    if (f && fclose(f) != 0) ok = false;
    if (!ok) fprintf(stderr, "starblade: cannot write %s\n", statePath().c_str());
    say(s, g, ok ? "STATE SAVED" : "STATE NOT SAVED");
}

void loadState(Game &s, vp_game *g)
{
    std::vector<uint8_t> data;
    if (FILE *f = fopen(statePath().c_str(), "rb")) {
        uint8_t buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
        fclose(f);
    }
    if (data.size() < sizeof STATE_MAGIC || memcmp(data.data(), STATE_MAGIC, sizeof STATE_MAGIC)) {
        say(s, g, data.empty() ? "NO SAVED STATE" : "NOT A STATE OF THIS VERSION");
        return;
    }
    // The machine as it is now, to put back if the file does not fit.
    State now(true);
    s.machine->state(now);
    State st(std::move(data));
    char magic[sizeof STATE_MAGIC];
    st.bytes(magic, sizeof magic);
    s.machine->state(st);
    if (!st.ok() || !st.atEnd()) {
        State back(std::move(now.data()));
        s.machine->state(back);
        say(s, g, "NOT A STATE OF THIS VERSION");
        return;
    }
    say(s, g, "STATE LOADED");
}

void saveControls(vp_input *in)
{
    if (vp_input_save(in, nullptr) != VP_OK) fprintf(stderr, "starblade: controls not saved: %s\n", vp_last_error());
}

// The settings: applied, and kept in save/options.ini ("deadzone = 5",
// "aim_speed = FAST").
std::string optionsPath() { return std::string(vp_save_dir()) + "/options.ini"; }

void applyOptions(Game &s, vp_input *in)
{
    vp_input_set_deadzone(in, DEADZONES[s.deadzone] / 100.0f);
    s.mcu.setAimSpeed(SPEEDS[s.speed].k);
}

void saveOptions(Game &s)
{
    FILE *f = fopen(optionsPath().c_str(), "w");
    if (!f) { fprintf(stderr, "starblade: cannot write %s\n", optionsPath().c_str()); return; }
    fprintf(f, "# Starblade Emulator: the CONTROLS page's settings\ndeadzone = %d\naim_speed = %s\n",
            DEADZONES[s.deadzone], SPEEDS[s.speed].name);
    fclose(f);
}

void loadOptions(Game &s)
{
    FILE *f = fopen(optionsPath().c_str(), "r");
    if (!f) return;
    char line[128], value[64];
    int n;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, " deadzone = %d", &n) == 1) {
            for (int i = 0; i < (int)(sizeof DEADZONES / sizeof DEADZONES[0]); i++)
                if (DEADZONES[i] == n) s.deadzone = i;
        } else if (sscanf(line, " aim_speed = %63s", value) == 1) {
            for (int i = 0; i < (int)(sizeof SPEEDS / sizeof SPEEDS[0]); i++)
                if (!strcmp(SPEEDS[i].name, value)) s.speed = i;
        }
    }
    fclose(f);
}

void openControls(Game &s, vp_game *g)
{
    s.controls = true;
    s.ctlRow = 0;
    s.capture = vp_capture{};
    s.said.clear();
    vp_audio_set_master(vp_game_audio(g), 0.0f);
}

void closeControls(Game &s, vp_game *g)
{
    s.controls = false;
    s.holdStarts = true;
    vp_audio_set_master(vp_game_audio(g), 1.0f);
}

// The CONTROLS page: Up/Down a row, START 1 / Enter lays out its function's
// controls (vp_capture), Left/Right none or standard, Delete standard; on a
// setting Left/Right change it, Delete its standard; EXIT or TAB back to the
// game.
void controlsPage(Game &s, vp_game *g)
{
    vp_input *in = vp_game_input(g);
    if (s.capture.capturing) {
        if (vp_capture_step(&s.capture, in) == VP_CAPTURE_CHANGED) saveControls(in);
    } else if (vp_input_key_pressed(in, KEY_TAB)) {
        closeControls(s, g);
        return;
    } else {
        if (vp_input_menu_up(in) || vp_input_menu_down(in)) {
            s.ctlRow = (s.ctlRow + (vp_input_menu_up(in) ? ROWS - 1 : 1)) % ROWS;
            vp_capture_moved(&s.capture);
        }
        int left = vp_input_menu_left_pressed(in), right = vp_input_menu_right_pressed(in);
        vp_capture_filter(&s.capture, &left, &right);
        if (s.ctlRow < NFUNCTIONS) {
            const char *name = FUNCTIONS[s.ctlRow].name;
            if (vp_input_menu_confirm(in)) {
                vp_capture_begin(&s.capture, in, name);
            } else if (left || right) {
                vp_capture_toggle(&s.capture, in, name);
                saveControls(in);
            } else if (vp_input_key_pressed(in, KEY_DELETE) || vp_input_key_pressed(in, KEY_KP_PERIOD)) {
                vp_capture_standard(&s.capture, in, name);
                saveControls(in);
            }
        } else if (s.ctlRow == ROW_DEADZONE || s.ctlRow == ROW_SPEED) {
            const bool dz = s.ctlRow == ROW_DEADZONE;
            int &v = dz ? s.deadzone : s.speed;
            const int count = dz ? (int)(sizeof DEADZONES / sizeof DEADZONES[0]) : (int)(sizeof SPEEDS / sizeof SPEEDS[0]);
            const int was = v;
            if (left && v > 0) v--;
            if (right && v < count - 1) v++;
            if (vp_input_key_pressed(in, KEY_DELETE) || vp_input_key_pressed(in, KEY_KP_PERIOD))
                v = dz ? DEADZONE_STANDARD : SPEED_STANDARD;
            if (v != was) { applyOptions(s, in); saveOptions(s); }
        } else if (vp_input_menu_confirm(in)) {
            closeControls(s, g);
            return;
        }
    }
    const char *said = vp_capture_said(&s.capture);
    s.said = said ? said : "";
}

// A row per function, its controls beside it (gray while they are the
// standard), the row being laid out blinking; EXIT; the help.
void drawControls(const Game &s, vp_game *g, vp_dvg *d)
{
    vp_input *in = vp_game_input(g);
    char text[VP_CONTROLS_LENGTH * 2];
    vp_text_font(VP_FONT_ASTEROIDS);
    vp_color(d, 0, 200, 255);
    vp_text(d, "CONTROLS", 2048, 3450, 170, VP_TEXT_CENTER);
    for (int i = 0; i < ROWS; i++) {
        const bool sel = i == s.ctlRow;
        const float y = 3050 - i * 240.0f - (i >= ROW_DEADZONE ? 60.0f : 0.0f) - (i == ROW_EXIT ? 60.0f : 0.0f);
        vp_text_font(VP_FONT_ASTEROIDS);
        if (sel) vp_color(d, 255, 60, 60); else vp_color(d, 255, 255, 255);
        if (i == ROW_EXIT) {
            vp_text(d, "EXIT", 2048, y, 100, VP_TEXT_CENTER);
            break;
        }
        if (i == ROW_DEADZONE || i == ROW_SPEED) {
            const bool dz = i == ROW_DEADZONE;
            char value[32];
            if (dz) snprintf(value, sizeof value, DEADZONES[s.deadzone] ? "%d%%" : "NONE", DEADZONES[s.deadzone]);
            else snprintf(value, sizeof value, "%s", SPEEDS[s.speed].name);
            vp_text(d, dz ? "STICK DEAD ZONE" : "AIM SPEED", 450, y, 90, VP_TEXT_LEFT);
            const bool standard = dz ? s.deadzone == DEADZONE_STANDARD : s.speed == SPEED_STANDARD;
            if (standard) vp_color(d, 150, 150, 150);
            else if (!sel) vp_color(d, 120, 255, 120);
            vp_text(d, value, 1900, y, 90, VP_TEXT_LEFT);
            continue;
        }
        vp_text(d, FUNCTIONS[i].label, 450, y, 90, VP_TEXT_LEFT);
        if (sel && s.capture.capturing) {
            if ((int)(vp_game_time(g) * 3) % 2 == 0) {
                vp_color(d, 255, 220, 60);
                vp_text(d, "...", 1900, y, 90, VP_TEXT_LEFT);
            }
            continue;
        }
        const char *has = vp_input_controls(in, FUNCTIONS[i].name);
        vp_input_spelled(in, FUNCTIONS[i].name, has, text, sizeof text);
        vp_text_font(VP_FONT_HERSHEY);
        if (has && !strcmp(has, "standard")) vp_color(d, 150, 150, 150);
        else if (!sel) vp_color(d, 120, 255, 120);
        vp_text(d, text, 1900, y, 75, VP_TEXT_LEFT);
    }
    vp_text_font(VP_FONT_HERSHEY);
    if (!s.said.empty()) {
        vp_color(d, 255, 220, 60);
        vp_text(d, s.said.c_str(), 2048, 900, 75, VP_TEXT_CENTER);
    }
    vp_color(d, 160, 160, 160);
    if (s.ctlRow == ROW_DEADZONE || s.ctlRow == ROW_SPEED) {
        vp_text(d, "LEFT, RIGHT - CHANGE   DEL - STANDARD", 2048, 620, 75, VP_TEXT_CENTER);
    } else if (s.ctlRow < NFUNCTIONS || s.capture.capturing) {
        const char *one, *two;
        vp_capture_help(&s.capture, &one, &two);
        vp_text(d, one, 2048, 680, 75, VP_TEXT_CENTER);
        vp_text(d, two, 2048, 550, 75, VP_TEXT_CENTER);
    } else {
        vp_text(d, "START, ENTER OR TAB - BACK TO THE GAME", 2048, 620, 75, VP_TEXT_CENTER);
    }
}

std::string nvramPath()
{
    return std::string(vp_save_dir()) + "/starblad.nv";
}

void loadNvram(std::vector<uint8_t> &nv)
{
    FILE *f = fopen(nvramPath().c_str(), "rb");
    if (!f) return;
    std::vector<uint8_t> data(nv.size());
    if (fread(data.data(), 1, data.size(), f) == data.size()) nv = data;
    fclose(f);
}

void saveNvram(const std::vector<uint8_t> &nv)
{
    FILE *f = fopen(nvramPath().c_str(), "wb");
    if (!f) return;
    fwrite(nv.data(), 1, nv.size(), f);
    fclose(f);
}

// --dump: what our DSP stand-in sees, to compare with MAME's captures.
void dump(Game &s)
{
    const long f = s.machine->frame();
    const uint16_t *ram = s.machine->dspram();
    if (!s.trace) s.trace = fopen((s.dumpDir + "/trace.txt").c_str(), "w");
    if (s.trace) {
        const Dsp::Frame &fr = s.dsp.finished();
        int counts[4] = {};             // types 0, 1/2, 100h/101h, other
        for (size_t i = 0; i < fr.list.size() && fr.list[i] != 0xffff;) {
            uint16_t t = fr.list[i];
            size_t len = (t == 1 || t == 2) ? 13 : t == 0 ? 6 : t == 0x100 ? 11 : t == 0x101 ? 14 : 0;
            counts[t == 0 ? 0 : (t == 1 || t == 2) ? 1 : len ? 2 : 3]++;
            if (!len) break;
            i += len;
        }
        fprintf(s.trace, "%ld sel=%d valid=%d type0=%d type1/2=%d type100=%d bad=%d\n", f, s.machine->dspram()[0x103],
                fr.valid ? 1 : 0, counts[0], counts[1], counts[2], counts[3]);
        if (f % 300 == 0) fflush(s.trace);
    }
    if (f % 300 == 0) {
        char name[64];
        snprintf(name, sizeof name, "/sbdump_dspram_%06ld.bin", f);
        FILE *out = fopen((s.dumpDir + name).c_str(), "wb");
        if (!out) return;
        for (int i = 0; i < 0x8000; i++) {
            uint8_t b[2] = { (uint8_t)(ram[i] >> 8), (uint8_t)ram[i] };
            fwrite(b, 1, 2, out);
        }
        fclose(out);
    }
}

// --record's: the picture at half size and the sprite list.
void recordFrame(Game &s, vp_dvg *d)
{
    char name[64];
    snprintf(name, sizeof name, "/record_%06ld", s.machine->frame());
    std::string base = std::string(vp_save_dir()) + name;
    if (vp_dvg_save_png(d, (base + ".png").c_str(), 512, 384) != VP_OK)
        fprintf(stderr, "starblade: %s\n", vp_last_error());
    if (FILE *out = fopen((base + ".txt").c_str(), "w")) {
        s.sprites->list(out, *s.machine);
        fclose(out);
    }
}

void capture(Game &s, vp_dvg *d)
{
    char name[64];
    snprintf(name, sizeof name, "/capture_%06ld", s.machine->frame());
    std::string base = std::string(vp_save_dir()) + name;
    if (vp_dvg_save_png(d, (base + ".png").c_str(), 1024, 768) != VP_OK)
        fprintf(stderr, "starblade: %s\n", vp_last_error());
    FILE *out = fopen((base + ".bin").c_str(), "wb");
    if (!out) return;
    const uint16_t *ram = s.machine->dspram();
    for (int i = 0; i < 0x8000; i++) {
        uint8_t b[2] = { (uint8_t)(ram[i] >> 8), (uint8_t)ram[i] };
        fwrite(b, 1, 2, out);
    }
    fclose(out);
    if ((out = fopen((base + ".txt").c_str(), "w"))) {
        s.sprites->list(out, *s.machine);
        fclose(out);
    }
    if ((out = fopen((base + "_sound.txt").c_str(), "w"))) {
        s.sound.list(out);
        fclose(out);
    }
    // The palette, as the game has it now: 32768 colors, r g b bytes.
    if ((out = fopen((base + ".pal").c_str(), "wb"))) {
        for (int i = 0; i < 0x8000; i++) {
            uint32_t rgb = s.machine->paletteRgb(i);
            uint8_t b[3] = { (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb };
            fwrite(b, 1, 3, out);
        }
        fclose(out);
    }
}

int init(vp_game *g)
{
    Game &s = *(Game *)vp_game_user(g);
    if (!roms_load(s.romPath, s.roms, s.error)) {
        fprintf(stderr, "starblade: %s\n", s.error.c_str());
        return VP_OK;                   // shown on screen, then back to the menu
    }
    s.machine = std::make_unique<Machine>(s.roms, s.mcu, s.sound, s.dsp);
    loadNvram(s.machine->nvram());
    s.render3d = std::make_unique<Render3d>();
    if (s.render3d->open(s.roms.points) != VP_OK) {
        fprintf(stderr, "starblade: %s\n", vp_last_error());
        return VP_ERR_NOMEM;
    }
    s.sprites = std::make_unique<Sprites>(s.roms.sprites);
    char vec[1024];
    s.sprites->load(vp_asset_path("sprites.vec", vec, sizeof vec));
    s.sprites->setShowIds(DEV && s.spriteIds);
    vp_input *in = vp_game_input(g);
    for (const Function &f : FUNCTIONS) vp_input_define(in, f.name, f.standard);
    vp_input_reserve_key(in, KEY_TAB);          // opens and closes the CONTROLS page
    if (DEV) {
        vp_input_reserve_key(in, KEY_F5);       // save state
        vp_input_reserve_key(in, KEY_F7);       // load state
    }
    vp_input_load(in, nullptr);                 // the player's own, if they set any
    loadOptions(s);                             // the dead zone, the aim speed
    applyOptions(s, in);
    s.mcu.setInput(in);
    // The mouse aims (and its left button fires): captured, so it keeps
    // counting at the window's edge.
    vp_input_grab_mouse(vp_game_input(g), 1);
    s.sound.open(vp_game_audio(g), s.roms, s.machine->dpram());
    s.machine->reset();
    return VP_OK;
}

void update(vp_game *g, float)
{
    Game &s = *(Game *)vp_game_user(g);
    if (!s.machine) {
        if (vp_game_time(g) > 8.0) vp_game_quit(g);
        return;
    }
    // The CONTROLS page: the game stands still while it is open. TAB, or
    // START 1 and START 2 together, opens it.
    vp_input *in = vp_game_input(g);
    if (s.controls) {
        controlsPage(s, g);
        return;
    }
    if (!s.paused && (vp_input_key_pressed(in, KEY_TAB) ||
                      (vp_input_held(in, "start1") && vp_input_held(in, "start2") &&
                       (vp_input_pressed(in, "start1") || vp_input_pressed(in, "start2"))))) {
        openControls(s, g);
        return;
    }
    // --record: the saved state, then a capture every RECORD_EVERY frames.
    if (DEV && s.recordSeconds > 0) {
        if (s.recordStart < 0) {
            loadState(s, g);
            if (s.stateSaid != "STATE LOADED") {
                fprintf(stderr, "starblade: --record: %s (%s)\n", s.stateSaid.c_str(), statePath().c_str());
                vp_game_quit(g);
                return;
            }
            s.recordStart = s.machine->frame();
        } else {
            const long done = s.machine->frame() - s.recordStart;
            if (done % RECORD_EVERY == 0) recordFrame(s, vp_game_output(g));
            if (done >= (long)(s.recordSeconds * Machine::kFrameRate)) { vp_game_quit(g); return; }
        }
    }
    if (DEV && vp_input_key_pressed(in, KEY_F5)) saveState(s, g);
    if (DEV && vp_input_key_pressed(in, KEY_F7)) loadState(s, g);
    // Leaving the page with START: the game does not get that press.
    if (s.holdStarts) {
        if (vp_input_held(in, "start1") || vp_input_held(in, "start2") || vp_input_menu_confirm_held(in)) return;
        s.holdStarts = false;
    }
    // P (ui_pause): freeze the game, to compare a moment with the original.
    // Pausing also saves the frame (the last one sent) and the DSP RAM, into
    // save/: capture_<frame>.png, .bin (as tools/sb_dump.lua), .txt (the sprite list)
    // and .pal (the palette).
    if (vp_input_pressed(vp_game_input(g), "ui_pause")) {
        s.paused = !s.paused;
        vp_audio_set_master(vp_game_audio(g), s.paused ? 0.0f : 1.0f);
        if (DEV && s.paused) capture(s, vp_game_output(g));
        vp_input_grab_mouse(vp_game_input(g), !s.paused);   // the desktop's while paused
    }
    if (s.paused) return;
    // The boot self test (RAM, ROMs, waiting on the DSPs): run through it
    // at full speed, not shown, up to the game starting its DSPs.
    if (!s.dsp.running() && s.machine->frame() < BOOT_FRAMES) {
        for (int k = 0; k < 100 && !s.dsp.running(); k++) s.machine->runFrame();
        return;
    }
    s.machine->runFrame();
    if (DEV && !s.dumpDir.empty()) dump(s);
}

void draw(vp_game *g, vp_dvg *d)
{
    Game &s = *(Game *)vp_game_user(g);
    if (s.machine && !s.dsp.running() && s.machine->frame() < BOOT_FRAMES) return;   // the self test: not shown
    if (!s.machine) {
        vp_color(d, 255, 80, 80);
        vp_text(d, "STAR BLADE", 2048, 2500, 160, VP_TEXT_CENTER);
        vp_color(d, 255, 255, 255);
        vp_text(d, "STARBLAD.ZIP NOT FOUND", 2048, 2000, 110, VP_TEXT_CENTER);
        vp_text(d, "PUT MAME'S STAR BLADE ROMS IN THE ROM FOLDER", 2048, 1700, 80, VP_TEXT_CENTER);
        return;
    }
    if (s.controls) {
        drawControls(s, g, d);
        return;
    }
    if (s.machine->videoEnabled()) {
        s.render3d->draw(d, s.dsp.finished(), *s.machine);
        s.sprites->draw(d, *s.machine);
    }
    if (!s.stateSaid.empty() && vp_game_time(g) < s.stateSaidUntil) {
        vp_text_font(VP_FONT_ASTEROIDS);
        vp_color(d, 255, 220, 60);
        vp_text(d, s.stateSaid.c_str(), 2048, 3800, 100, VP_TEXT_CENTER);
    }
}

void shutdown(vp_game *g)
{
    Game &s = *(Game *)vp_game_user(g);
    if (s.machine) saveNvram(s.machine->nvram());
    if (s.trace) fclose(s.trace);
    s.sound.close();
}

} // namespace

int main(int argc, char **argv)
{
    static Game game;

    // Our own options; the rest go to vp_run. (The development ones are
    // read even with SB_DEV 0, and then ignored.)
    std::vector<char *> rest;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && !strcmp(argv[i], "--roms") && i + 1 < argc) { game.romPath = argv[++i]; continue; }
        if (i > 0 && !strcmp(argv[i], "--sprite-ids")) { game.spriteIds = true; continue; }
        if (i > 0 && !strcmp(argv[i], "--dump") && i + 1 < argc) { game.dumpDir = argv[++i]; continue; }
        if (i > 0 && !strcmp(argv[i], "--record") && i + 1 < argc) { game.recordSeconds = atof(argv[++i]); continue; }
        rest.push_back(argv[i]);
    }

    vp_game_desc desc = {};
    desc.title = "Star Blade";
    desc.user = &game;
    desc.init = init;
    desc.update = update;
    desc.draw = draw;
    desc.shutdown = shutdown;
    desc.update_hz = 60;
    desc.frame_hz = 60;
    desc.frame_hz_board = 30;
    return vp_run(&desc, (int)rest.size(), rest.data());
}
