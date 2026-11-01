// Simple Bot v0.2.0
//
// Идея: на каждом кадре (PlayLayer::postUpdate) бот строит план нажатий на `horizon` кадров вперёд.
// Проверка плана делается НАСТОЯЩИМ движком игры: создаётся невидимый «призрак»-PlayerObject,
// ему загружается состояние реального игрока (PlayerCheckpoint), и он прогоняется
// через PlayerObject::update + GJBaseGameLayer::checkCollisions. Поэтому все режимы,
// орбы, пады, порталы гравитации/скорости/размера работают «сами» — физику считает игра.
//
// Поиск: сначала «ничего не менять»; если призрак умирает на кадре T — перебираем момент
// переключения d (от самого позднего к раннему), длину удержания/тапа и (рекурсивно) второе
// переключение. План кешируется и пересчитывается, только если закончился или реальный игрок
// отклонился от предсказанной траектории.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <vector>

using namespace geode::prelude;

namespace {
    PlayerObject* g_ghost = nullptr;  // призрак (для хука destroyPlayer)
    bool g_simDead = false;           // призрак «умер» в текущем шаге

    // Объекты, чья активация нельзя «просачивать» из симуляции в реальную игру
    // (двойной/одиночный портал, зеркальные порталы, монеты): симуляция останавливается перед ними.
    bool isBarrier(GameObject* o) {
        switch (o->m_objectID) {
            case 286: case 287:  // dual / solo portal
            case 45: case 46:    // mirror portals
            case 142: case 1329: // secret / user coins
                return true;
            default:
                return false;
        }
    }

    // Объекты с флагом активации, который симуляция может испортить (орбы, пады, порталы...).
    bool isStateful(GameObject* o) {
        switch (o->m_objectType) {
            case GameObjectType::Solid:
            case GameObjectType::Hazard:
            case GameObjectType::Decoration:
            case GameObjectType::Slope:
                return false;
            default:
                return true;
        }
    }

    struct Snap {
        PlayerCheckpoint* cp = nullptr;
        CCPoint pos;
        std::vector<uint8_t> flags;
        decltype(PlayerObject::m_holdingButtons) hb;
    };
}

class $modify(BotLayer, PlayLayer) {
    struct Fields {
        bool holding = false;
        std::vector<uint8_t> plan;
        std::vector<CCPoint> trace;
        size_t planPos = 0;

        Ref<PlayerObject> ghost;
        Ref<CCLayer> ghostParent;
        std::vector<Ref<PlayerCheckpoint>> pool;
        size_t used = 0;

        std::vector<GameObject*> cache;  // все «stateful» объекты уровня, отсортированы по X
        bool cacheBuilt = false;
        std::vector<GameObject*> win;    // объекты в окне перед игроком
        float barrierX = FLT_MAX;

        int budget = 0;
        int subs = 4;
        float subDt = 1.f / 240.f;

        ~Fields() {
            if (g_ghost && g_ghost == ghost.data()) g_ghost = nullptr;
        }
    };

    // ---------------------------------------------------------------- хуки

    void destroyPlayer(PlayerObject* p, GameObject* o) {
        if (g_ghost && p == g_ghost) {
            g_simDead = true;  // умер призрак — игру не трогаем
            return;
        }
        PlayLayer::destroyPlayer(p, o);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto f = m_fields.self();
        f->holding = false;
        f->plan.clear();
        f->trace.clear();
        f->planPos = 0;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        this->botTick(dt);
    }

    // ---------------------------------------------------------------- вспомогательное

    PlayerCheckpoint* botAcquire() {
        auto f = m_fields.self();
        if (f->used >= f->pool.size()) f->pool.push_back(PlayerCheckpoint::create());
        return f->pool[f->used++].data();
    }

    void botEnsureGhost() {
        auto f = m_fields.self();
        if (f->ghost) return;
        f->ghostParent = CCLayer::create();
        f->ghost = PlayerObject::create(1, 1, this, f->ghostParent, true);
        f->ghostParent->addChild(f->ghost);
        f->ghost->setVisible(false);
        g_ghost = f->ghost;
    }

    void botBuildCache() {
        auto f = m_fields.self();
        f->cacheBuilt = true;
        f->cache.clear();
        for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
            if (obj && isStateful(obj)) f->cache.push_back(obj);
        }
        std::sort(f->cache.begin(), f->cache.end(), [](GameObject* a, GameObject* b) {
            return a->getPositionX() < b->getPositionX();
        });
    }

    void botBuildWindow(int horizon) {
        auto f = m_fields.self();
        f->win.clear();
        f->barrierX = FLT_MAX;
        float px = m_player1->getPositionX();
        float lo = px - 60.f;
        float hi = px + horizon * 12.f + 100.f;  // 12 юн/кадр — запас для 4x скорости
        auto it = std::lower_bound(f->cache.begin(), f->cache.end(), lo, [](GameObject* o, float v) {
            return o->getPositionX() < v;
        });
        for (; it != f->cache.end() && (*it)->getPositionX() <= hi; ++it) {
            GameObject* o = *it;
            f->win.push_back(o);
            if (isBarrier(o) && !o->m_isActivated && o->getPositionX() > px - 10.f) {
                f->barrierX = std::min(f->barrierX, o->getPositionX() - 25.f);
            }
        }
    }

    Snap botCapture(PlayerObject* p) {
        auto f = m_fields.self();
        Snap s;
        s.cp = this->botAcquire();
        p->saveToCheckpoint(s.cp);
        s.pos = p->getPosition();
        s.hb = p->m_holdingButtons;
        s.flags.resize(f->win.size());
        for (size_t i = 0; i < f->win.size(); ++i) {
            s.flags[i] = f->win[i]->m_isActivated;
        }
        return s;
    }

    void botRestore(const Snap& s) {
        auto f = m_fields.self();
        PlayerObject* g = f->ghost;
        g->loadFromCheckpoint(s.cp);
        g->m_holdingButtons = s.hb;
        for (size_t i = 0; i < f->win.size() && i < s.flags.size(); ++i) {
            f->win[i]->m_isActivated = s.flags[i] != 0;
        }
        // Пересобрать список касаемых орбов (checkpoint его не хранит).
        g_simDead = false;
        this->checkCollisions(g, f->subDt, false);
        g_simDead = false;
    }

    // Прогоняет призрака n кадров из снимка s. Возвращает, сколько кадров он прожил (n = выжил).
    // rec (если задан) получает снимки на начало каждого кадра: rec[i] = состояние перед кадром i.
    int botRun(const Snap& s, bool hold0, int n, const std::function<bool(int)>& input, std::vector<Snap>* rec) {
        auto f = m_fields.self();
        this->botRestore(s);
        PlayerObject* g = f->ghost;
        bool hold = hold0;
        if (rec) {
            rec->clear();
            rec->push_back(s);
        }
        for (int i = 0; i < n; ++i) {
            if (f->budget <= 0) return i;
            bool want = input(i);
            if (want != hold) {
                hold = want;
                if (want) g->pushButton(PlayerButton::Jump);
                else g->releaseButton(PlayerButton::Jump);
            }
            g_simDead = false;
            for (int k = 0; k < f->subs; ++k) {
                g->update(f->subDt);
                this->checkCollisions(g, f->subDt, false);
                if (g_simDead) return i;
            }
            f->budget -= f->subs;
            if (g->getPositionX() >= f->barrierX) return n;
            if (rec) rec->push_back(this->botCapture(g));
        }
        return n;
    }

    // Ищет план нажатий на n кадров. cur — текущее состояние кнопки, base — «по умолчанию».
    // out — план (n значений 0/1). Возвращает, сколько кадров переживает лучший найденный план.
    int botSearch(const Snap& s0, bool cur, bool base, int n, int depth, std::vector<uint8_t>& out) {
        auto f = m_fields.self();
        size_t mark = f->used;

        std::vector<Snap> rec;
        int T = this->botRun(s0, cur, n, [base](int) { return base; }, &rec);
        out.assign(n, base ? 1 : 0);
        if (T >= n || depth <= 0) {
            f->used = mark;
            return T;
        }

        auto makePlan = [&](int d, int len) {
            std::vector<uint8_t> p(n, base ? 1 : 0);
            for (int i = d; i < std::min(n, d + len); ++i) p[i] = base ? 0 : 1;
            return p;
        };

        static const int kLens[4] = {2, 5, 10, 0};  // 0 = держать до конца
        int best = T;
        int lo = std::max(0, T - 40);

        for (int d = T - 1; d >= lo && f->budget > 0; --d) {
            bool hAt = (d == 0) ? cur : base;
            for (int li = 0; li < 4 && f->budget > 0; ++li) {
                int len = kLens[li] ? kLens[li] : n - d;
                auto in = [base, len](int i) { return i < len ? !base : base; };
                size_t mark2 = f->used;

                std::vector<Snap> rec2;
                int T2 = this->botRun(rec[d], hAt, n - d, in, &rec2);
                int total = d + T2;

                if (total >= n) {
                    int dd = d;
                    if (d > 0) {  // запас прочности: пробуем нажать на кадр раньше
                        bool hAt2 = (d - 1 == 0) ? cur : base;
                        int T3 = this->botRun(rec[d - 1], hAt2, n - (d - 1), in, nullptr);
                        if (d - 1 + T3 >= n) dd = d - 1;
                    }
                    out = makePlan(dd, len);
                    f->used = mark;
                    return n;
                }
                if (total > best) {
                    best = total;
                    out = makePlan(d, len);
                }

                // Пережили сегмент и умерли позже — ищем второе переключение.
                if (depth > 1 && len > 0 && d + len < n && T2 > len) {
                    std::vector<uint8_t> sub;
                    int R = this->botSearch(rec2[len], !base, base, n - d - len, depth - 1, sub);
                    int tot = d + len + R;
                    if (tot > best || tot >= n) {
                        auto p = makePlan(d, len);
                        for (int i = 0; i < (int)sub.size() && d + len + i < n; ++i) p[d + len + i] = sub[i];
                        out = p;
                        if (tot >= n) {
                            f->used = mark;
                            return n;
                        }
                        best = tot;
                    }
                }
                f->used = mark2;
            }
        }
        f->used = mark;
        return best;
    }

    void botReplan(int horizon) {
        auto f = m_fields.self();
        this->botEnsureGhost();
        f->used = 0;
        f->budget = (int)Mod::get()->getSettingValue<int64_t>("max_steps");
        this->botBuildWindow(horizon);

        Snap root = this->botCapture(m_player1);
        std::vector<uint8_t> plan;
        this->botSearch(root, f->holding, f->holding, horizon, 2, plan);

        // Прогон выбранного плана для трассы ожидаемых позиций.
        f->budget = (int)plan.size() * f->subs + 64;
        std::vector<Snap> rec;
        this->botRun(root, f->holding, (int)plan.size(), [&plan](int i) { return plan[i] != 0; }, &rec);
        f->trace.clear();
        for (auto& s : rec) f->trace.push_back(s.pos);

        f->plan = std::move(plan);
        f->planPos = 0;
        f->used = 0;
    }

    void botTick(float dt) {
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (!m_player1 || m_player1->m_isDead) return;
        if (m_level && m_level->isPlatformer()) return;

        auto f = m_fields.self();
        if (!f->cacheBuilt) this->botBuildCache();

        int H = (int)Mod::get()->getSettingValue<int64_t>("horizon");
        dt = std::clamp(dt, 1.f / 240.f, 1.f / 30.f);
        f->subs = std::max(1, (int)std::lround(dt * 240.f));
        f->subDt = dt / (float)f->subs;

        bool need = f->plan.empty() || f->planPos >= f->plan.size() ||
                    (f->plan.size() - f->planPos) < (size_t)(H / 2);
        if (!need && f->planPos < f->trace.size()) {
            CCPoint p = m_player1->getPosition();
            CCPoint e = f->trace[f->planPos];
            if (std::fabs(p.x - e.x) > 1.5f || std::fabs(p.y - e.y) > 1.5f) need = true;
        }
        if (need) this->botReplan(H);

        if (f->planPos < f->plan.size()) {
            bool want = f->plan[f->planPos++] != 0;
            if (want != f->holding) {
                f->holding = want;
                this->handleButton(want, 1, true);
            }
        }
    }
};
