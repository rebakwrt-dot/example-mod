#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

static bool modOn() { return Mod::get()->getSettingValue<bool>("enabled"); }

// P2 is placed one frame AFTER dual is enabled, so the game has
// finished its own setup of P2 first.
static bool g_placeP2 = false;

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
        if (enable && modOn()) g_placeP2 = true;
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

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        this->forceDual();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        this->forceDual();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (modOn() && m_gameState.m_isDualMode) {
            syncP2(this, g_placeP2);
            g_placeP2 = false;
        }
    }

    // Diagnostic: logs who died and from what.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        log::info(
            "[AlwaysDual] death: {} | object: {} | P1=({:.0f},{:.0f}) P2=({:.0f},{:.0f}) | gravity P1={} P2={}",
            player == m_player2 ? "P2" : "P1",
            object ? fmt::format("id {} at ({:.0f},{:.0f})", object->m_objectID, object->getPositionX(), object->getPositionY()) : std::string("none (bounds/ground)"),
            m_player1->getPositionX(), m_player1->getPositionY(),
            m_player2->getPositionX(), m_player2->getPositionY(),
            m_player1->m_isUpsideDown, m_player2->m_isUpsideDown
        );
        PlayLayer::destroyPlayer(player, object);
    }
};
