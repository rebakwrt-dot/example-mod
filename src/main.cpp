#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <thread>
#include <chrono>

using namespace geode::prelude;

static bool s_freezing = false;

static void ultrakillEffect() {
    if (s_freezing) return;

    auto director = CCDirector::get();
    auto scene = director->getRunningScene();
    if (!scene) return;

    // 1) Вспышка: весь экран мгновенно белый
    if (Mod::get()->getSettingValue<bool>("flash-enabled")) {
        auto winSize = director->getWinSize();
        auto flash = CCLayerColor::create({255, 255, 255, 255}, winSize.width, winSize.height);
        flash->setZOrder(100000);
        scene->addChild(flash);
        // Действия идут через планировщик: пока игра заморожена, вспышка остаётся белой,
        // после разморозки плавно гаснет.
        flash->runAction(CCSequence::create(
            CCFadeOut::create(0.2f),
            CCRemoveSelf::create(),
            nullptr
        ));
    }

    // 2) Кратковременная остановка (hit-stop)
    float duration = static_cast<float>(Mod::get()->getSettingValue<double>("freeze-duration"));
    if (duration <= 0.f) return;

    auto scheduler = director->getScheduler();
    float prevScale = scheduler->getTimeScale();
    s_freezing = true;
    scheduler->setTimeScale(0.f);

    // Таймер в реальном времени (планировщик заморожен, поэтому отдельный поток)
    std::thread([duration, prevScale]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(duration * 1000.f)));
        Loader::get()->queueInMainThread([prevScale]() {
            CCDirector::get()->getScheduler()->setTimeScale(prevScale);
            s_freezing = false;
        });
    }).detach();
}

class $modify(UltrakillPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) override {
        bool wasDead = player->m_isDead;
        PlayLayer::destroyPlayer(player, object);

        // Эффект только при настоящей смерти (не ноклип, не античит-шип)
        if (!wasDead && player->m_isDead && object != m_anticheatSpike) {
            ultrakillEffect();
        }
    }
};
