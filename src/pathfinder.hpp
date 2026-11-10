#pragma once
#include <atomic>
#include <string>
#include <vector>
#include <functional>
#include <Object.hpp>

/**
 * extra:   hitbox info for objects the simulator doesn't hardcode (filled from the game).
 * message: optional out-string with a short status (unsupported mechanics, timeout, ...).
 */
std::vector<uint8_t> pathfind(
	std::string const& lvlString,
	std::atomic_bool& stop,
	std::function<void(double)> callback,
	ObjectInfoTable const& extra = {},
	std::string* message = nullptr
);
