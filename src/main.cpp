#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

struct Obj {
    int id = 1;
    double x = 0, y = 0, rot = 0, scale = 1;
};

static const char* SYSTEM_PROMPT =
    "You are a Geometry Dash level builder. Convert the user's description into objects. "
    "Reply ONLY with a JSON array, no text, no markdown. Each item: "
    "{\"id\":int,\"x\":int,\"y\":int,\"rot\":number,\"scale\":number}. "
    "x,y are grid cells (1 cell = 1 block), x>=0 from the left, y=0 is the ground row. "
    "Allowed ids: 1=block, 8=spike, 39=small spike, 35=yellow pad, 36=yellow orb, "
    "84=blue orb, 140=pink pad, 141=pink orb, 10=normal gravity portal, 11=flipped gravity portal, "
    "12=cube portal, 13=ship portal. Spikes must sit on top of a block or the ground (rot 0). "
    "Maximum 400 objects. Make the layout playable.";

// ---------- offline generator (no AI, keyword based) ----------
static bool has(std::string const& p, std::initializer_list<const char*> words) {
    for (auto w : words) if (p.find(w) != std::string::npos) return true;
    return false;
}

static std::vector<Obj> offlineGenerate(std::string p) {
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return std::tolower(c); });
    int n = 5;
    for (size_t i = 0; i < p.size(); i++) {
        if (std::isdigit((unsigned char)p[i])) { n = std::clamp(std::atoi(p.c_str() + i), 1, 60); break; }
    }
    std::vector<Obj> out;
    int cur = 0;
    bool any = false;
    if (has(p, {"лестниц", "Лестниц", "stair"})) {
        any = true;
        for (int i = 0; i < n; i++) out.push_back({1, double(cur + i), double(i), 0, 1});
        cur += n + 2;
    }
    if (has(p, {"шип", "Шип", "spike"})) {
        any = true;
        for (int i = 0; i < n; i++) out.push_back({8, double(cur + i), 0, 0, 1});
        cur += n + 2;
    }
    if (has(p, {"платформ", "Платформ", "platform"})) {
        any = true;
        for (int i = 0; i < n; i++) out.push_back({1, double(cur + i), 3, 0, 1});
        cur += n + 2;
    }
    if (has(p, {"башн", "Башн", "tower"})) {
        any = true;
        for (int i = 0; i < n; i++) out.push_back({1, double(cur), double(i), 0, 1});
        cur += 3;
    }
    if (!any) for (int i = 0; i < n; i++) out.push_back({1, double(i), 0, 0, 1});
    return out;
}

// ---------- parse LLM answer ----------
static std::vector<Obj> parseObjects(std::string text) {
    std::vector<Obj> out;
    auto a = text.find('[');
    auto b = text.rfind(']');
    if (a == std::string::npos || b == std::string::npos || b < a) return out;
    auto parsed = matjson::parse(text.substr(a, b - a + 1));
    if (!parsed) return out;
    auto arr = parsed.unwrap();
    if (!arr.isArray()) return out;
    for (size_t i = 0; i < arr.size() && out.size() < 500; i++) {
        auto& it = arr[i];
        Obj o;
        o.id = (int)it["id"].asDouble().unwrapOr(0);
        o.x = it["x"].asDouble().unwrapOr(0);
        o.y = it["y"].asDouble().unwrapOr(0);
        o.rot = it["rot"].asDouble().unwrapOr(0);
        o.scale = it["scale"].asDouble().unwrapOr(1);
        if (o.id > 0 && o.id < 2000) out.push_back(o);
    }
    return out;
}

// ---------- settings popup ----------
class AISettingsPopup : public Popup {
protected:
    TextInput* m_url = nullptr;
    TextInput* m_key = nullptr;
    TextInput* m_model = nullptr;
    TextInput* m_startX = nullptr;

    TextInput* makeInput(char const* hint, float y, std::string const& value, bool pass) {
        auto in = TextInput::create(300.f, hint, "chatFont.fnt");
        in->setPosition({m_size.width / 2, y});
        in->setString(value);
        if (pass) in->setPasswordMode(true);
        m_mainLayer->addChild(in);
        return in;
    }

    bool init() {
        if (!Popup::init(340.f, 260.f)) return false;
        setTitle("AI Builder settings");
        auto mod = Mod::get();
        float h = m_size.height;
        m_url = makeInput("API URL (offline = no AI)", h - 65, mod->getSavedValue<std::string>("api-url", "offline"), false);
        m_key = makeInput("API key (optional)", h - 105, mod->getSavedValue<std::string>("api-key", ""), true);
        m_model = makeInput("Model", h - 145, mod->getSavedValue<std::string>("model", "llama-3.3-70b-versatile"), false);
        m_startX = makeInput("Start offset (cells)", h - 185, std::to_string(mod->getSavedValue<int64_t>("start-x", 10)), false);

        auto spr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png", .8f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AISettingsPopup::onSave));
        btn->setPosition({m_size.width / 2, 30.f});
        m_buttonMenu->addChild(btn);
        return true;
    }

    void onSave(CCObject*) {
        auto mod = Mod::get();
        mod->setSavedValue<std::string>("api-url", m_url->getString());
        mod->setSavedValue<std::string>("api-key", m_key->getString());
        mod->setSavedValue<std::string>("model", m_model->getString());
        int64_t x = std::clamp(std::atoi(m_startX->getString().c_str()), 0, 200);
        mod->setSavedValue<int64_t>("start-x", x);
        Notification::create("Saved", NotificationIcon::Success)->show();
        this->onClose(nullptr);
    }

public:
    static AISettingsPopup* create() {
        auto ret = new AISettingsPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------- popup ----------
class AIPopup : public Popup {
protected:
    LevelEditorLayer* m_lel = nullptr;
    TextInput* m_input = nullptr;
    CCMenuItemSpriteExtra* m_genBtn = nullptr;
    async::TaskHolder<web::WebResponse> m_holder;

    bool init(LevelEditorLayer* lel) {
        if (!Popup::init(340.f, 180.f)) return false;
        m_lel = lel;
        setTitle("AI Builder");
        auto size = m_size;

        m_input = TextInput::create(300.f, "Describe what to build...", "chatFont.fnt");
        m_input->setMaxCharCount(300);
        m_input->setPosition({size.width / 2, size.height / 2 + 15});
        m_mainLayer->addChild(m_input);

        auto spr = ButtonSprite::create("Create", "goldFont.fnt", "GJ_button_01.png", .8f);
        m_genBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AIPopup::onGenerate));
        m_genBtn->setPosition({size.width / 2 + 70, 35});
        m_buttonMenu->addChild(m_genBtn);

        auto setSpr = ButtonSprite::create("Settings", "goldFont.fnt", "GJ_button_04.png", .8f);
        auto setBtn = CCMenuItemSpriteExtra::create(setSpr, this, menu_selector(AIPopup::onSettings));
        setBtn->setPosition({size.width / 2 - 70, 35});
        m_buttonMenu->addChild(setBtn);
        return true;
    }

    void place(std::vector<Obj> const& objs) {
        if (objs.empty()) {
            FLAlertLayer::create("AI Builder", "No objects were produced. Try describing it differently.", "OK")->show();
            return;
        }
        double startX = (double)Mod::get()->getSavedValue<int64_t>("start-x", 10);
        int count = 0;
        for (auto const& o : objs) {
            // клетка (gx, gy) -> координаты центра объекта
            CCPoint pos{float((startX + o.x) * 30.0 + 15.0), float(o.y * 30.0 + 15.0)};
            auto obj = m_lel->createObject(o.id, pos, true);
            if (!obj) continue;
            if (o.rot != 0) obj->setRotation((float)o.rot);
            if (o.scale != 1 && o.scale > 0) obj->setScale((float)o.scale);
            count++;
        }
        Notification::create("Objects created: " + std::to_string(count), NotificationIcon::Success)->show();
        this->onClose(nullptr);
    }

    void onSettings(CCObject*) {
        if (auto p = AISettingsPopup::create()) p->show();
    }

    void onGenerate(CCObject*) {
        std::string prompt = m_input->getString();
        if (prompt.empty()) return;

        auto url = Mod::get()->getSavedValue<std::string>("api-url", "offline");
        if (url.empty() || url == "offline") {
            place(offlineGenerate(prompt));
            return;
        }
        auto key = Mod::get()->getSavedValue<std::string>("api-key", "");
        auto model = Mod::get()->getSavedValue<std::string>("model", "llama-3.3-70b-versatile");

        matjson::Value body = matjson::makeObject({
            {"model", model},
            {"temperature", 0.3},
            {"messages", std::vector<matjson::Value>{
                matjson::makeObject({{"role", "system"}, {"content", std::string(SYSTEM_PROMPT)}}),
                matjson::makeObject({{"role", "user"}, {"content", prompt}})
            }}
        });

        web::WebRequest req;
        if (!key.empty()) req.header("Authorization", "Bearer " + key);
        req.timeout(std::chrono::seconds(120));
        req.bodyJSON(body);

        m_genBtn->setEnabled(false);
        Notification::create("AI is thinking...", NotificationIcon::Loading)->show();

        m_holder.spawn(req.post(url), [this](web::WebResponse res) {
            m_genBtn->setEnabled(true);
            if (!res.ok()) {
                std::string msg = "Request failed: " + std::to_string(res.code()) +
                                  ". Check internet, URL, key and model name.";
                FLAlertLayer::create("AI Builder", msg, "OK")->show();
                return;
            }
            auto json = res.json();
            if (!json) {
                FLAlertLayer::create("AI Builder", "Invalid server response", "OK")->show();
                return;
            }
            auto j = json.unwrap();
            auto text = j["choices"][0]["message"]["content"].asString().unwrapOr("");
            place(parseObjects(text));
        });
    }

public:
    static AIPopup* create(LevelEditorLayer* lel) {
        auto ret = new AIPopup();
        if (ret->init(lel)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------- editor button ----------
class $modify(AIEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;

        auto winSize = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({40.f, winSize.height - 110.f});

        auto spr = ButtonSprite::create("AI", "goldFont.fnt", "GJ_button_04.png", .8f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AIEditorUI::onAI));
        menu->addChild(btn);
        this->addChild(menu, 100);
        return true;
    }

    void onAI(CCObject*) {
        if (auto p = AIPopup::create(m_editorLayer)) p->show();
    }
};
