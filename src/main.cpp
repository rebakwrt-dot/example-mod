#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <vector>

using namespace geode::prelude;

// Один записанный кадр попытки.
struct GhostFrame {
    double t;      // время уровня (сек)
    float x, y;    // позиция в objectLayer
    float rot;     // поворот
    float sx, sy;  // масштаб (мини / зеркало / гравитация)
    IconType mode; // режим (куб, корабль, шар...)
};

static IconType modeOf(PlayerObject* p) {
    if (p->m_isShip) return IconType::Ship;
    if (p->m_isBall) return IconType::Ball;
    if (p->m_isBird) return IconType::Ufo;
    if (p->m_isDart) return IconType::Wave;
    if (p->m_isRobot) return IconType::Robot;
    if (p->m_isSpider) return IconType::Spider;
    if (p->m_isSwing) return IconType::Swing;
    return IconType::Cube;
}

static void setOpacityRecursive(CCNode* node, GLubyte o) {
    if (auto rgba = typeinfo_cast<CCRGBAProtocol*>(node)) rgba->setOpacity(o);
    if (auto kids = node->getChildren()) {
        for (auto kid : CCArrayExt<CCNode*>(kids)) setOpacityRecursive(kid, o);
    }
}

class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<GhostFrame> current;  // текущая попытка (запись)
        std::vector<GhostFrame> ghostRun; // прошлая неудачная попытка (воспроизведение)
        Ref<SimplePlayer> ghost;          // единственный призрак
        bool died = false;
        bool modeSet = false;
        IconType shownMode = IconType::Cube;
    };

    void ensureGhost() {
        auto f = m_fields.self();
        if (f->ghost || !m_objectLayer) return;
        auto gm = GameManager::get();
        f->ghost = SimplePlayer::create(gm->activeIconForType(IconType::Cube));
        f->ghost->setVisible(false);
        m_objectLayer->addChild(f->ghost, 1000);
    }

    void applyGhostLook(IconType mode) {
        auto f = m_fields.self();
        auto gm = GameManager::get();
        f->ghost->updatePlayerFrame(gm->activeIconForType(mode), mode);
        auto col = Mod::get()->getSettingValue<ccColor3B>("ghost_color");
        f->ghost->setColor(col);
        f->ghost->setSecondColor(col);
        f->shownMode = mode;
        f->modeSet = true;
    }

    void updateGhost(double t) {
        auto f = m_fields.self();
        auto& run = f->ghostRun;
        if (run.empty() || t < run.front().t || t > run.back().t) {
            if (f->ghost) f->ghost->setVisible(false);
            return;
        }
        ensureGhost();
        if (!f->ghost) return;

        auto it = std::upper_bound(run.begin(), run.end(), t,
            [](double v, GhostFrame const& fr) { return v < fr.t; });
        auto const& b = (it == run.end()) ? run.back() : *it;
        auto const& a = (it == run.begin()) ? *it : *(it - 1);

        float k = 0.f;
        if (b.t > a.t) k = static_cast<float>((t - a.t) / (b.t - a.t));

        if (!f->modeSet || f->shownMode != a.mode) applyGhostLook(a.mode);

        f->ghost->setPosition({a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k});
        f->ghost->setRotation(a.rot);
        f->ghost->setScaleX(a.sx);
        f->ghost->setScaleY(a.sy);
        f->ghost->setVisible(true);

        auto op = Mod::get()->getSettingValue<double>("opacity");
        setOpacityRecursive(f->ghost, static_cast<GLubyte>(std::clamp(op, 0.0, 1.0) * 255.0));
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto f = m_fields.self();
        if (!Mod::get()->getSettingValue<bool>("enabled") || m_isPracticeMode) {
            if (f->ghost) f->ghost->setVisible(false);
            return;
        }
        if (!m_player1) return;

        double t = m_gameState.m_levelTime;

        // Запись текущей попытки
        if (!m_player1->m_isDead && (f->current.empty() || t > f->current.back().t)) {
            auto mode = modeOf(m_player1);
            float sy = m_player1->getScaleY();
            if (m_player1->m_isUpsideDown &&
                (mode == IconType::Ship || mode == IconType::Ufo || mode == IconType::Swing)) {
                sy = -sy;
            }
            f->current.push_back({
                t,
                m_player1->getPositionX(), m_player1->getPositionY(),
                m_player1->getRotation(),
                m_player1->getScaleX(), sy,
                mode
            });
        }

        // Воспроизведение призрака
        updateGhost(t);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        if (player == m_player1 && m_player1->m_isDead && !m_isPracticeMode) {
            m_fields->died = true;
        }
    }

    void resetLevel() {
        auto f = m_fields.self();
        // Если прошлая попытка закончилась смертью — она становится призраком.
        // Старый призрак заменяется (всегда максимум один).
        if (f->died && f->current.size() > 2) {
            f->ghostRun = std::move(f->current);
        }
        f->current.clear();
        f->died = false;

        PlayLayer::resetLevel();

        if (f->ghost) f->ghost->setVisible(false);
    }
};
