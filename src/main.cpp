#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

static bool modOn() {
    return Mod::get()->getSettingValue<bool>("enabled");
}

// Makes P2 run on the SAME lane as P1: same gravity (not mirrored),
// same height (not in the floor/ceiling), shifted forward by the offset.
static void syncP2(GJBaseGameLayer* layer, bool place) {
    auto p1 = layer->m_player1;
    auto p2 = layer->m_player2;
    if (!p1 || !p2) return;

    if (p1->m_isUpsideDown != p2->m_isUpsideDown) {
        p2->flipGravity(p1->m_isUpsideDown, true);
    }
    if (place) {
        auto pos = p1->getPosition();
        float off = static_cast<float>(Mod::get()->getSettingValue<int64_t>("p2-offset"));
        p2->setPosition({pos.x + off, pos.y});
        p2->m_yVelocity = 0;
    }
}

class $modify(ADBase, GJBaseGameLayer) {
    void toggleDualMode(GameObject* portal, bool enable, PlayerObject* player, bool noEffects) {
        GJBaseGameLayer::toggleDualMode(portal, enable, player, noEffects);
        if (enable && modOn()) syncP2(this, true);
    }
};

class $modify(ADLayer, PlayLayer) {
    void forceDual() {
        if (!modOn()) return;
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
        if (modOn() && m_gameState.m_isDualMode) syncP2(this, false);
        this->hideCeiling();
    }
};
