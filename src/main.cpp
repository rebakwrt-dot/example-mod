#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/coro.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <UIBuilder.hpp>
#include "pathfinder.hpp"
#include <future>
#include <set>
#include <sstream>

// Reads real object types/hitboxes from the game for every object ID used by the level,
// so 2.2 blocks/spikes the simulator has no hardcoded entry for are no longer ignored.
// NOTE: uses GameObject fields (m_objectType, m_objectRadius, getObjectRect); verify against your Geode bindings.
static ObjectInfoTable buildObjectInfo(std::string const& lvlString) {
    ObjectInfoTable table;
    std::set<int> ids;

    std::stringstream ss(lvlString);
    std::string objstr;
    bool first = true;
    while (std::getline(ss, objstr, ';')) {
        if (first) { first = false; continue; }
        std::stringstream os(objstr);
        std::string k, v;
        while (std::getline(os, k, ',') && std::getline(os, v, ',')) {
            if (k == "1") { ids.insert(atoi(v.c_str())); break; }
        }
    }

    for (int id : ids) {
        auto obj = GameObject::createWithKey(id);
        if (!obj) continue;

        ObjectInfo info;
        switch (obj->m_objectType) {
            case GameObjectType::Solid: info.kind = ObjectInfo::Kind::Solid; break;
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard: info.kind = ObjectInfo::Kind::Hazard; break;
            default: continue;
        }

        if (obj->m_objectRadius > 0) {
            info.kind = ObjectInfo::Kind::Circle;
            info.radius = obj->m_objectRadius;
        } else {
            auto r = obj->getObjectRect();
            info.width = r.size.width;
            info.height = r.size.height;
        }
        table[id] = info;
    }
    return table;
}

using namespace geode::prelude;
using namespace geode::utils::file;

class PathfinderNode : public CCLayerColor {
    std::atomic_bool m_stop = false;
    std::atomic<double> m_progress = 0;
    std::future<std::vector<uint8_t>> m_result;
    std::string m_levelName;
    std::string m_message;
public:
    static PathfinderNode* create(std::string const& levelName, std::string const& lvlString) {
        auto node = new PathfinderNode();
        if (node && node->init(levelName, lvlString)) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    ~PathfinderNode() {
        m_stop = true;
        if (m_result.valid())
            m_result.get();
    }

    void finalize(std::vector<uint8_t> macro) {
        getChildByIDRecursive("stop")->setVisible(false);

        if (!m_message.empty())
            Notification::create(m_message, NotificationIcon::Warning)->show();
 
        auto callback = [this, macro](this auto self) -> arc::Future<void> {
            auto saveDir = Mod::get()->getSaveDir();
            if (Loader::get()->isModLoaded("eclipse.eclipse-menu")) {
                saveDir = Loader::get()->getLoadedMod("eclipse.eclipse-menu")->getSaveDir() / "replays";
            }

            if (!exists(saveDir)) {
                create_directories(saveDir);
            }

            FilePickOptions opts(
                saveDir / fmt::format("{}.gdr2", m_levelName), {{
                std::string("Macro File"),
                std::unordered_set {std::string("gdr2")}
            }});

            if (auto path = co_await pick(PickMode::SaveFile, opts); path.isOk() && path.unwrap().has_value()) {
                (void)writeBinary(*path.unwrap(), macro);
                queueInMainThread([this] {
                    removeFromParentAndCleanup(true);
                });
            }
        };

        Build<ButtonSprite>::create("Export", "bigFont.fnt", "GJ_button_01.png")
            .intoMenuItem(async::wrapSpawn(callback))
            .scale(0.8)
            .move(0, -40)
            .parent(getChildByID("menu"));
    }

    void keyBackClicked() override  {
        m_stop = true;
        CCLayer::keyBackClicked();
        removeFromParentAndCleanup(true);
    }

    bool init(std::string const& levelName, std::string const& lvlString) {
        CCLayerColor::initWithColor({0, 0, 0, 100});
        setCascadeOpacityEnabled(true);

        m_levelName = levelName;

        // Must be built on the main thread (creates GameObjects)
        auto extra = buildObjectInfo(lvlString);

        m_result = std::async(std::launch::async, [lvlString, extra, this]() {
            try {
            return pathfind(lvlString, m_stop, [this](double progress) {
                if (m_progress < progress)
                    m_progress = progress;
            }, extra, &m_message);
            } catch (std::exception& e) {
                log::error("{}", e.what());
                return std::vector<uint8_t>();
            }
        });

        setKeypadEnabled(true);

        Build(this).initTouch().schedule([this](float) {
                Build(this).intoChildRecurseID<CCLabelBMFont>("percent")
                    .string(fmt::format("{:.2f}%", m_progress).c_str());

                if (m_result.valid() && m_result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    finalize(m_result.get());
                }
        });

        auto handle = [this](CCMenuItemSpriteExtra* it) {
            m_stop = true;

            if (it->getID() == "stop")
                finalize(m_result.get());
            else
                removeFromParentAndCleanup(true);
        };

        auto menu = Build<CCMenu>::create().parent(this).id("menu").children(
            Build<CCScale9Sprite>::create("GJ_square02.png")
                .contentSize(250, 140),
            Build<CCLabelBMFont>::create("Pathfinding...", "bigFont.fnt")
                .move(0, 50)
                .scale(0.8),
            Build<CCLabelBMFont>::create("0.00", "chatFont.fnt")
                .id("percent")
                .move(0, 10),
            Build<ButtonSprite>::create("Stop", "bigFont.fnt", "GJ_button_04.png")
                .scale(0.8)
                .intoMenuItem(handle)
                .id("stop")
                .move(0, -40),
            Build<CCSprite>::createSpriteName("GJ_closeBtn_001.png")
                .intoMenuItem(handle)
                .id("close")
                .move(-125, 70)
                .scale(0.8)
        );

        /*    .intoNewChild(CCMenu::create())
                .id("menu")
                .intoNewChild(CCScale9Sprite::create("GJ_square04.png"))
                    .contentSize(250, 140)
                .intoNewSibling(CCLabelBMFont::create("Pathfinding...", "bigFont.fnt"))
                    .move(0, 50)
                    .scale(0.8)
                .intoNewSibling(CCLabelBMFont::create("0.00", "chatFont.fnt"))
                    .id("percent")
                    .move(0, 10)
                .intoNewSibling(ButtonSprite::create("Stop", "bigFont.fnt", "GJ_button_04.png"))
                    .intoMenuItem([this]() {
                        m_stop = true;
                        finalize(m_result.get());
                    })
                    .scale(0.8)
                    .id("cancel")
                    .move(0, -40)
                .intoNewSibling(CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"))
                    .intoMenuItem([this]() {
                        m_stop = true;
                        this->removeFromParentAndCleanup(true);
                    })
                    .move(-125, 70)
                    .scale(0.8);*/
        ;

        return true;
    }

};

class $modify(EditLevelLayer) {
    bool init(GJGameLevel* p0) {
        EditLevelLayer::init(p0);

        auto btn = Build<BasedButtonSprite>::create(
            CCSprite::create("pathfinder.png"_spr),
            BaseType::Circle,
            4,
            3
        ).scale(0.8);

        btn->setTopRelativeScale(1.4);

        btn.intoMenuItem([this]() {
                auto lvlString = ZipUtils::decompressString(m_level->m_levelString, true, 0);
                Build<PathfinderNode>::create(m_level->m_levelName, lvlString).parent(this).zOrder(100);
        }).id("pathfinder-button")
          .intoNewParent(CCMenu::create())
          .parent(this)
          .id("pathfinder-menu")
          .matchPos(getChildByIDRecursive("delete-button"))
          .move(-45, 0);


        return true;
    }
};

class $modify(LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        LevelInfoLayer::init(level, challenge);

        auto btn = Build<BasedButtonSprite>::create(
            CCSprite::create("pathfinder.png"_spr),
            BaseType::Circle,
            4,
            3
        ).scale(0.8);

        btn->setTopRelativeScale(1.4);

        btn.intoMenuItem([this]() {
                auto lvlString = ZipUtils::decompressString(m_level->m_levelString, true, 0);
                Build<PathfinderNode>::create(m_level->m_levelName, lvlString).parent(this).zOrder(100);
        }).id("pathfinder-button")
          .parent(getChildByID("other-menu"))
          .matchPos(getChildByIDRecursive("list-button"))
          .move(0, 45);

        return true;
    }
};
