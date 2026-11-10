#pragma once
#include <util.hpp>
#include <unordered_map>
#include <cstring>
#include <optional>

struct ObjectContainer;
struct Player;

/**
 * Describes an object the simulator has no hardcoded entry for (typically 2.2 blocks/spikes).
 * The mod fills this table from the running game (real object type + hitbox), so new objects
 * are no longer silently ignored.
 */
struct ObjectInfo {
    enum class Kind { Solid, Hazard, Circle } kind = Kind::Solid;
    float width = 30, height = 30; // for Solid / Hazard
    float radius = 0;              // for Circle (sawblade-like)
};
using ObjectInfoTable = std::unordered_map<int, ObjectInfo>;

struct Object : public Entity {
    /// NOT object id. A unique ID associated with each Object for direct comparisons
    int id;

    /**
     * In GD, some objects have their collision checks later than others (blocks, hazards). 
     * A higher prio numbers means collisions are processed later.
     */
    int prio;

    Object() = default;
    Object(Vec2D size, std::unordered_map<int, std::string>&& fields);

    /// Determines if the object should be counted as colliding with the player.
    virtual bool touching(Player const&) const;

    /// Where all of the collision magic happens.
    virtual void collide(Player&) const;

    /// Create an object from a given level string mapping.
    static std::optional<ObjectContainer> create(std::unordered_map<int, std::string>&& ob, ObjectInfoTable const* extra = nullptr);

    /// True for object IDs that are known to need mechanics the simulator cannot model yet.
    static char const* unsupportedReason(int id);
};

/**
 * In the simulator, there are many classes that subclass Object. Some of these add member variables
 * and all of them have virtual overrides. Under normal circumstances, this would require every Object
 * class to be dynamically allocated, which is inefficient.
 * 
 * ObjectContainer provides a solution to this by storing every created Object subclass in a padded
 * container class. Doing this instead of dynamic allocation not only saves time from heap allocation,
 * it also allows for better CPU caching since all Object classes are stored contiguously in memory.
 */
struct ObjectContainer {
    char buffer[sizeof(Object) + 0x8] = {0};

    ObjectContainer(ObjectContainer& cont) { memcpy(buffer, cont.buffer, sizeof(buffer)); }
    ObjectContainer(ObjectContainer const& cont) { memcpy(buffer, cont.buffer, sizeof(buffer)); }

    template <class T>
    ObjectContainer(T&& obj) {
        static_assert(sizeof(T) <= sizeof(buffer));
        memcpy(buffer, (void*)&obj, sizeof(T));
    }
    Object const* operator->() const {
        return reinterpret_cast<Object const*>(buffer);
    }
    Object* operator->() {
        return reinterpret_cast<Object*>(buffer);
    }
};
