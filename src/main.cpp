#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <gdr/gdr.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

// ---------- GDR2 replay types ----------
// Default input type from the library, bot name + integer version (see README)
struct AMReplay : gdr::Replay<AMReplay, gdr::Input<>> {
    AMReplay() : Replay("AutoMacro", 1) {}
};

// ---------- Global state ----------
static bool g_active = false;   // auto-solver running
static bool g_sim = false;      // inside simulation (deaths are virtual)
static bool g_dead = false;     // died during simulation
static bool g_holding = false;  // current real button state
static AMReplay g_replay;

static int64_t opt(char const* k) { return Mod::get()->getSettingValue<int64_t>(k); }

struct Branch {
    std::vector<bool> inputs; // one entry per frame: true = hold
    float x = 0.f;
    bool dead = false;
    int clicks = 0;
};

class $modify(AMPlayLayer, PlayLayer) {
    struct Fields {
        int frame = 0;
        CheckpointObject* cp = nullptr;
    };

    // Virtual death while simulating
    void destroyPlayer(PlayerObject* p, GameObject* o) {
        if (g_sim) { g_dead = true; return; }
        PlayLayer::destroyPlayer(p, o);
