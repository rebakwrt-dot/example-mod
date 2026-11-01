#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

static constexpr float STEP = 1.f / 240.f;

// Игрок-копия, на котором идёт симуляция. Пока он не null, смерти игнорируются.
static PlayerObject* g_simPlayer = nullptr;
static bool g_simDied = false;

class $modify(BotPlayLayer, PlayLayer) {
    struct Fields {
        bool holding = false;
    };

    // Не даём симуляции убить настоящего игрока.
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (g_simPlayer && player == g_simPlayer) {
            g_simDied = true;
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }

    // Копируем состояние настоящего игрока в симуляционного.
    void copyState(PlayerObject* from, PlayerObject* to) {
        to->setPosition(from->getPosition());
        to->m_yVelocity = from->m_yVelocity;
        to->m_isOnGround = from->m_isOnGround;
        to->m_isUpsideDown = from->m_isUpsideDown;
        to->m_isShip = from->m_isShip;
        to->m_isBall = from->m_isBall;
        to->m_isBird = from->m_isBird;
        to->m_isDart = from->m_isDart;
        to->m_isRobot = from->m_isRobot;
        to->m_isSpider = from->m_isSpider;
        to->m_isSwing = from->m_isSwing;
        to->m_playerSpeed = from->m_playerSpeed;
        to->m_gravityMod = from->m_gravityMod;
    }

    // Возвращает, сколько кадров игрок прожил (horizon = выжил).
    int simulate(bool hold, int horizon) {
        auto real = m_player1;

        auto sim = PlayerObject::create(1, 1, this, m_objectLayer, false);
        sim->retain();
        copyState(real, sim);

        g_simPlayer = sim;
        g_simDied = false;

        int survived = horizon;
        for (int i = 0; i < horizon; i++) {
            if (hold) sim->pushButton(PlayerButton::Jump);
            else sim->releaseButton(PlayerButton::Jump);

            sim->update(STEP * 60.f);
            this->checkCollisions(sim, STEP * 60.f, false);

            if (g_simDied) {
                survived = i;
                break;
            }
        }

        g_simPlayer = nullptr;
        sim->release();
        return survived;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return;
        if (!m_player1 || m_player1->m_isDead || m_isPaused) return;

        int horizon = mod->getSettingValue<int64_t>("horizon");

        int withJump = this->simulate(true, horizon);
        int withoutJump = this->simulate(false, horizon);

        // Выбираем вариант, в котором живём дольше. При равенстве не жмём.
        bool wantHold = withJump > withoutJump;

        if (wantHold != m_fields->holding) {
            m_fields->holding = wantHold;
            this->handleButton(wantHold, 1, true);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->holding = false;
    }
};
