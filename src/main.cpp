#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <gdr/gdr.hpp>
#include <algorithm>

using namespace geode::prelude;

// ---------- GDR2 replay types ----------
struct AMInput : gdr::Input<AMInput> {
    AMInput() = default;
    AMInput(int frame, PlayerButton button, bool player2, bool down)
        : Input(frame, button, player2, down) {}
};
struct AMReplay : gdr::Replay<AMReplay, AMInput> {
    AMReplay() : Replay("AutoMacro", "0.1.0") {}
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
    }

    // Apply input only on state change, record it
    void setHold(bool down, bool record) {
        if (down == g_holding) return;
        g_holding = down;
        if (down) this->pushButton(PlayerButton::Jump, true);
        else      this->releaseButton(PlayerButton::Jump, true);
        if (record)
            g_replay.inputs.emplace_back(m_fields->frame + 1, PlayerButton::Jump, false, down);
    }

    // Run one fixed 240 TPS step
    void stepOnce() { GJBaseGameLayer::update(1.f / 240.f); }

    Branch simulate(std::vector<bool> const& seed, bool startHolding) {
        Branch b; b.inputs = seed;
        g_sim = true; g_dead = false;
        bool hold = startHolding;
        for (bool h : seed) {
            if (h != hold) { hold = h; b.clicks++; }
            // direct button calls without recording
            if (h != g_holding) {
                g_holding = h;
                if (h) this->pushButton(PlayerButton::Jump, true);
                else   this->releaseButton(PlayerButton::Jump, true);
            }
            stepOnce();
            if (g_dead) { b.dead = true; break; }
        }
        b.x = m_player1->getPositionX();
        g_sim = false;
        return b;
    }

    void solveAndCommit() {
        int look = (int)opt("lookahead");
        int step = (int)opt("commit-step");
        int maxHold = (int)opt("max-hold");

        // snapshot
        auto cp = this->createCheckpoint();
        bool startHold = g_holding;

        std::vector<Branch> branches;
        // Candidates: [hold A frames][release B frames] then repeat last state
        for (int a = 0; a <= maxHold; a += 2) {
            for (int bLen = 0; bLen <= maxHold; bLen += 2) {
                std::vector<bool> seed;
                seed.reserve(look);
                bool s = startHold;
                // first segment keeps current state for a frames,
                // then flips for bLen, then flips back for the rest
                for (int i = 0; i < look; i++) {
                    if (i < a) seed.push_back(s);
                    else if (i < a + bLen) seed.push_back(!s);
                    else seed.push_back(s);
                }
                this->loadFromCheckpoint(cp);
                g_holding = startHold;
                branches.push_back(simulate(seed, startHold));
            }
        }

        // pick: alive first, then furthest x, then fewest clicks
        auto best = std::max_element(branches.begin(), branches.end(),
            [](Branch const& l, Branch const& r) {
                if (l.dead != r.dead) return l.dead;
                if (l.x != r.x) return l.x < r.x;
                return l.clicks > r.clicks;
            });

        // restore and commit the first `step` frames for real
        this->loadFromCheckpoint(cp);
        g_holding = startHold;
        for (int i = 0; i < step && i < (int)best->inputs.size(); i++) {
            setHold(best->inputs[i], true);
            m_fields->frame++;
            stepOnce();
        }
    }

    void update(float dt) {
        if (!g_active || m_hasCompletedLevel || g_sim) {
            PlayLayer::update(dt);
            return;
        }
        // Take over: solve in fixed chunks instead of normal update
        this->solveAndCommit();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (g_active && Mod::get()->getSettingValue<bool>("auto-export"))
            exportReplay(this);
        g_active = false;
    }

    static void exportReplay(PlayLayer* pl) {
        g_replay.author = Mod::get()->getSettingValue<std::string>("author");
        g_replay.description = "Generated by AutoMacro";
        g_replay.duration = (float)g_replay.inputs.size() > 0
            ? g_replay.inputs.back().frame / 240.f : 0.f;
        g_replay.framerate = 240.f;
        g_replay.gameVersion = GEODE_GD_VERSION;
        g_replay.levelInfo.id = pl->m_level->m_levelID;
        g_replay.levelInfo.name = pl->m_level->m_levelName;

        auto dir = Mod::get()->getSaveDir() / "macros";
        std::filesystem::create_directories(dir);
        auto path = dir / fmt::format("{}.gdr2", pl->m_level->m_levelName);
        auto data = g_replay.exportData(false); // binary
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<char const*>(data.data()), data.size());
        Notification::create("Macro saved: " + path.filename().string(),
                             NotificationIcon::Success)->show();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->frame = 0;
        g_holding = false;
        if (g_active) g_replay.inputs.clear();
    }
};

// ---------- Hotkey: F8 toggles auto-solver ----------
class $modify(CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat) {
        if (down && !repeat && key == KEY_F8 && PlayLayer::get()) {
            g_active = !g_active;
            if (g_active) {
                g_replay = AMReplay();
                PlayLayer::get()->resetLevel();
            }
            Notification::create(g_active ? "AutoMacro: ON" : "AutoMacro: OFF",
                                 NotificationIcon::Info)->show();
            return true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat);
    }
};
