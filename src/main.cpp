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

static void toggleAutoMacro() {
    auto pl = PlayLayer::get();
    if (!pl) return;
    g_active = !g_active;
    if (g_active) {
        g_replay = AMReplay();
        pl->resetLevel();
    }
    Notification::create(g_active ? "AutoMacro: ON" : "AutoMacro: OFF",
                         NotificationIcon::Info)->show();
}

class $modify(AMPlayLayer, PlayLayer) {
    struct Fields {
        int frame = 0;
        CheckpointObject* cp = nullptr;
    };

    // On-screen button (works on phones where there is no F8 key)
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        auto spr = ButtonSprite::create("Auto");
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(AMPlayLayer::onToggleBtn));
        auto menu = CCMenu::create();
        menu->addChild(btn);
        auto win = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width - 45.f, win.height - 45.f});
        menu->setZOrder(100);
        m_uiLayer->addChild(menu);
    }

    void onToggleBtn(CCObject*) {
        toggleAutoMacro();
    }

    // Virtual death while simulating
    void destroyPlayer(PlayerObject* p, GameObject* o) {
        if (g_sim) { g_dead = true; return; }
        PlayLayer::destroyPlayer(p, o);
    }

    // Apply input only on state change, record it
    void setHold(bool down, bool record) {
        if (down == g_holding) return;
        g_holding = down;
        if (down) this->pushButton(static_cast<int>(PlayerButton::Jump), true);
        else      this->releaseButton(static_cast<int>(PlayerButton::Jump), true);
        if (record) {
            g_replay.inputs.emplace_back(
                static_cast<uint64_t>(m_fields->frame + 1),
                static_cast<uint8_t>(PlayerButton::Jump),
                false,
                down
            );
        }
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
                if (h) this->pushButton(static_cast<int>(PlayerButton::Jump), true);
                else   this->releaseButton(static_cast<int>(PlayerButton::Jump), true);
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
        std::string levelName = pl->m_level->m_levelName;

        g_replay.author = Mod::get()->getSettingValue<std::string>("author");
        g_replay.description = "Generated by AutoMacro";
        g_replay.duration = g_replay.inputs.empty()
            ? 0.f : static_cast<float>(g_replay.inputs.back().frame) / 240.f;
        g_replay.framerate = 240.0;
        g_replay.gameVersion = GEODE_COMP_GD_VERSION;
        g_replay.levelInfo.id = static_cast<uint32_t>(pl->m_level->m_levelID.value());
        g_replay.levelInfo.name = levelName;

        auto dir = Mod::get()->getSaveDir() / "macros";
        std::filesystem::create_directories(dir);
        auto path = dir / fmt::format("{}.gdr2", levelName);

        // exportData() takes no arguments and returns a Result
        auto res = g_replay.exportData();
        if (!res.isOk()) {
            Notification::create("AutoMacro: export failed", NotificationIcon::Error)->show();
            return;
        }
        auto const& data = res.unwrap();

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

// ---------- Hotkey: F8 toggles auto-solver (desktop only) ----------
#ifndef GEODE_IS_ANDROID
class $modify(CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (down && !repeat && key == KEY_F8 && PlayLayer::get()) {
            toggleAutoMacro();
            return true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};
#endif
