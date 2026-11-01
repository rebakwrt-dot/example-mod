#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

static bool modOn() {
    return Mod::get()->getSettingValue<bool>("enabled");
}

static int g_frame = 0;
static int g_lines = 0;

class $modify(ADLayer, PlayLayer) {
    void forceDual() {
        if (!modOn()) return;
        if (m_level->isPlatformer()) return;
        if (!m_gameState.m_isDualMode) {
            this->toggleDualMode(nullptr, true, m_player1, true);
        }
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
        PlayLayer::setupHasCompleted();
        this->forceDual();
        this->splitControls();
        this->hideCeiling();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        g_frame = 0;
        g_lines = 0;
        this->forceDual();
        this->splitControls();
        this->hideCeiling();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (modOn() && m_gameState.m_isDualMode && m_player1 && m_player2) {
            // P2 always keeps the same gravity as P1 (same lane, not mirrored)
            if (m_player1->m_isUpsideDown != m_player2->m_isUpsideDown) {
                m_player2->flipGravity(m_player1->m_isUpsideDown, true);
            }
        }
        this->hideCeiling();

        // Diagnostic: where exactly are both players during the first seconds
        if (modOn() && m_gameState.m_isDualMode && m_player1 && m_player2) {
            g_frame++;
            if (g_frame % 15 == 0 && g_lines < 14) {
                g_lines++;
                log::info(
                    "[AlwaysDual] pos: P1=({:.1f},{:.1f}) up={} | P2=({:.1f},{:.1f}) up={} vis={}",
                    m_player1->getPositionX(), m_player1->getPositionY(), m_player1->m_isUpsideDown,
                    m_player2->getPositionX(), m_player2->getPositionY(), m_player2->m_isUpsideDown,
                    m_player2->isVisible()
                );
            }
        }
    }

    // Logs showed both players dying in the first frames after spawn
    // (x < 20) from a hidden spike (id 8) or from "no object".
    // No real level object can be there, so ignore deaths at the very start.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (modOn() && m_gameState.m_isDualMode && m_player1 &&
            m_player1->getPositionX() < 30.f) {
            return;
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
