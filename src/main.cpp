#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>

using namespace geode::prelude;

static bool g_inject = false;

static bool on() { return Mod::get()->getSettingValue<bool>("enabled"); }

static std::optional<enumKeyCodes> parseKey(std::string const& s) {
	if (s.size() == 1 && s[0] >= 'A' && s[0] <= 'Z')
		return static_cast<enumKeyCodes>(KEY_A + (s[0] - 'A'));
	if (s == "Up") return KEY_Up;
	if (s == "Down") return KEY_Down;
	if (s == "Left") return KEY_Left;
	if (s == "Right") return KEY_Right;
	if (s == "Space") return KEY_Space;
	if (s == "Shift") return KEY_Shift;
	if (s == "Ctrl") return KEY_Control;
	if (s == "Enter") return KEY_Enter;
	if (s == "Tab") return KEY_Tab;
	return std::nullopt;
}

// Applies icons (texture packs are respected, frames come from the loaded pack) and colors.
static void applyLook(PlayerObject* p, bool first) {
	if (!p) return;
	auto m = Mod::get();
	std::string k = first ? "p1-" : "p2-";
	if (first && !m->getSettingValue<bool>("p1-override")) return;

	p->updatePlayerFrame(m->getSettingValue<int64_t>(k + "cube"));
	p->updatePlayerShipFrame(m->getSettingValue<int64_t>(k + "ship"));
	p->updatePlayerRollFrame(m->getSettingValue<int64_t>(k + "ball"));
	p->updatePlayerBirdFrame(m->getSettingValue<int64_t>(k + "ufo"));
	p->updatePlayerDartFrame(m->getSettingValue<int64_t>(k + "wave"));
	p->updatePlayerRobotFrame(m->getSettingValue<int64_t>(k + "robot"));
	p->updatePlayerSpiderFrame(m->getSettingValue<int64_t>(k + "spider"));
	p->updatePlayerSwingFrame(m->getSettingValue<int64_t>(k + "swing"));

	p->setColor(m->getSettingValue<ccColor3B>(k + "color1"));
	p->setSecondColor(m->getSettingValue<ccColor3B>(k + "color2"));

	if (m->getSettingValue<bool>(k + "glow-on")) {
		p->m_hasGlow = true;
		p->enableCustomGlowColor(m->getSettingValue<ccColor3B>(k + "glow"));
	} else {
		p->m_hasGlow = false;
		p->disableCustomGlowColor();
	}
	p->updatePlayerGlow();
}

static void forceDual(PlayLayer* pl) {
	if (!on()) return;
	pl->m_levelSettings->m_twoPlayerMode = true; // lets each player be controlled separately
	if (!pl->m_gameState.m_isDualMode) {
		pl->toggleDualMode(nullptr, true, pl->m_player1, true);
	}
	// Same plane: copy gravity and put Player 2 ahead of Player 1
	auto p1 = pl->m_player1;
	auto p2 = pl->m_player2;
	if (p1 && p2) {
		if (p2->m_isUpsideDown != p1->m_isUpsideDown) p2->flipGravity(p1->m_isUpsideDown, true);
		auto pos = p1->getPosition();
		pos.x += static_cast<float>(Mod::get()->getSettingValue<int64_t>("p2-offset"));
		p2->setPosition(pos);
		p2->m_lastPosition = pos;
	}
	applyLook(p1, true);
	applyLook(p2, false);
}

// Draws the dashed chain between the two players
static void updateLink(PlayLayer* pl) {
	auto p1 = pl->m_player1;
	auto p2 = pl->m_player2;
	if (!p1 || !p2) return;
	auto parent = p1->getParent();
	if (!parent) return;
	auto node = static_cast<CCDrawNode*>(parent->getChildByID("always-dual-link"_spr));
	if (!node) {
		node = CCDrawNode::create();
		node->setID("always-dual-link"_spr);
		parent->addChild(node, 50);
	}
	node->clear();
	if (!on() || !Mod::get()->getSettingValue<bool>("show-link") || !pl->m_gameState.m_isDualMode) return;

	auto c = Mod::get()->getSettingValue<ccColor3B>("link-color");
	ccColor4F col = {c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f};
	float th = static_cast<float>(Mod::get()->getSettingValue<int64_t>("link-thickness"));

	CCPoint a = p1->getPosition(), b = p2->getPosition();
	CCPoint d = b - a;
	float len = std::sqrt(d.x * d.x + d.y * d.y);
	if (len < 1.f) return;
	CCPoint dir = {d.x / len, d.y / len};
	const float dash = 8.f, gap = 6.f;
	for (float t = 0.f; t < len; t += dash + gap) {
		float e = std::min(t + dash, len);
		node->drawSegment(a + dir * t, a + dir * e, th * 0.5f, col);
	}
}

class $modify(AlwaysDualPlayLayer, PlayLayer) {
	void setupHasCompleted() {
		PlayLayer::setupHasCompleted();
		forceDual(this);
	}
	void resetLevel() {
		PlayLayer::resetLevel();
		forceDual(this);
	}
	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		updateLink(this);
	}
};

class $modify(AlwaysDualBaseLayer, GJBaseGameLayer) {
	// Portals can't turn dual mode off
	void toggleDualMode(GameObject* portal, bool dual, PlayerObject* player, bool noEffects) {
		if (on() && !dual && PlayLayer::get()) return;
		GJBaseGameLayer::toggleDualMode(portal, dual, player, noEffects);
	}
	// Optionally block normal input, only injected custom-key input passes
	void handleButton(bool down, int button, bool isPlayer1) {
		if (on() && button == 1 && !g_inject && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == static_cast<GJBaseGameLayer*>(this) &&
			Mod::get()->getSettingValue<bool>("only-custom-keys")) return;
		GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}
};

class $modify(AlwaysDualPlayer, PlayerObject) {
	// Re-apply look when the gamemode changes
	void switchedToMode(GameObjectType type) {
		PlayerObject::switchedToMode(type);
		auto pl = PlayLayer::get();
		if (!pl || !on()) return;
		if (this == pl->m_player1) applyLook(this, true);
		else if (this == pl->m_player2) applyLook(this, false);
	}
};

class $modify(AlwaysDualKeys, CCKeyboardDispatcher) {
	// NOTE: if your GD bindings use the 3-argument version, remove the `timestamp` parameter.
	bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
		auto pl = PlayLayer::get();
		if (on() && pl && !pl->m_isPaused && !repeat) {
			auto k1 = parseKey(Mod::get()->getSettingValue<std::string>("p1-key"));
			auto k2 = parseKey(Mod::get()->getSettingValue<std::string>("p2-key"));
			bool hit = false;
			g_inject = true;
			if (k1 && key == *k1) { pl->handleButton(down, 1, true); hit = true; }
			if (k2 && key == *k2) { pl->handleButton(down, 1, false); hit = true; }
			g_inject = false;
			if (hit) return true;
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
	}
};
