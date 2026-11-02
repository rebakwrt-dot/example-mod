#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

static bool modOn() {
    return Mod::get()->getSettingValue<bool>("enabled");
}

class $modify(ADLayer, PlayLayer) {
    // Use the game's own "Start Dual" level flag. The game then sets up
    // P2 AND the ground/ceiling correctly during its normal level reset
    // (calling toggleDualMode by hand broke the ground).
    void forceDual() {
        if (!modOn()) return;
        if (m_level->isPlatformer()) return;
        if (m_levelSettings) m_levelSettings->m_startDual = true;
    }

    // Built-in 2-player mode: tap left half = P1, right half = P2.
    void splitControls() {
        if (!modOn()) return;
        if (!Mod::get()->getSettingValue<bool>("split-controls")) return;
        if (m_levelSettings) m_levelSettings->m_twoPlayerMode = true;
    }

    void hideCeiling() {
        if (!Mod::get()->getSettingValue<bool>("hide-ceiling")) return;
        if (m_groundLayer2) m_groundLayer2->setVisible(false);
    }

    void setupHasCompleted() {
        this->forceDual();
        PlayLayer::setupHasCompleted();
        this->splitControls();
        this->hideCeiling();
    }

    void resetLevel() {
        this->forceDual();
        PlayLayer::resetLevel();
        this->splitControls();
        this->hideCeiling();
    }

    // Ball, spider and swing copter flip gravity on their own, so P1 and P2
    // may differ there. In every other mode P2 follows P1's gravity.
    static bool ownGravity(PlayerObject* p) {
        return p->m_isBall || p->m_isSpider || p->m_isSwing;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (modOn() && m_gameState.m_isDualMode && m_player1 && m_player2 &&
            !ownGravity(m_player1) && !ownGravity(m_player2)) {
            if (m_player1->m_isUpsideDown != m_player2->m_isUpsideDown) {
                m_player2->flipGravity(m_player1->m_isUpsideDown, true);
            }
        }
        this->hideCeiling();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (modOn() && m_gameState.m_isDualMode && m_player1) {
            // Deaths right after spawn (hidden start spike / no object)
            if (m_player1->getPositionX() < 30.f) return;
            // The invisible dual ceiling: a "no object" death high above the floor
            if (!object && player && player->getPositionY() > 150.f) return;
        }
        log::info(
            "[AlwaysDual] death: {} | object: {} | P1=({:.0f},{:.0f}) P2=({:.0f},{:.0f})",
            player == m_player2 ? "P2" : "P1",
            object ? fmt::format("id {} at ({:.0f},{:.0f})", object->m_objectID, object->getPositionX(), object->getPositionY()) : std::string("none (bounds/ground)"),
            m_player1->getPositionX(), m_player1->getPositionY(),
            m_player2->getPositionX(), m_player2->getPositionY()
        );
        PlayLayer::destroyPlayer(player, object);
    }
};
