#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// Icons are NOT overridden: the game itself gives both players
// your currently selected icons (P2 with swapped colors), in every mode.
class $modify(AlwaysDualLayer, PlayLayer) {
    void forceDual() {
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (m_level->isPlatformer()) return;
        if (!m_gameState.m_isDualMode) {
            this->toggleDualMode(nullptr, true, m_player1, true);
        }
    }

    void hideCeiling() {
        if (!Mod::get()->getSettingValue<bool>("hide-ceiling")) return;
        if (m_groundLayer2) m_groundLayer2->setVisible(false);
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        this->forceDual();
        this->hideCeiling();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        this->forceDual();
        this->hideCeiling();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        this->hideCeiling();
    }
};
