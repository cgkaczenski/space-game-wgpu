#pragma once

// Last known positions: a memory of things that can be lost from sight (sight
// roadmap S4). Enemies the player has seen are this game's use, and the same
// memory pointed the other way could hold where enemies last saw the player;
// nothing here knows what is being remembered.
//
// **A ghost is born at the edge.** Each frame the caller says, for each thing,
// whether it is seen and -- while it is -- its state. The memory keeps the last
// seen state of each, and on the frame a thing goes from seen to unseen it
// leaves a ghost holding that state: where it was last seen, not where it is.
// Seen again, its ghost is gone; the thing itself is there to look at.
//
// **A ghost clears when its spot is looked at again, empty -- once armed.** A
// ghost's spot is where its thing was last seen, so it is in sight at the
// moment it is made: cleared on "the spot is in sight", it would go the frame
// after. So a ghost is first armed, by its spot leaving sight; after that,
// the spot back in sight with the thing not there clears it. Watch something
// slip behind cover and its ghost stays at the cover's edge; look away and
// back and it is gone.
//
// Plain numbers, like regionMask and visibility: no drawing. Time is the
// caller's clock, in seconds.

#include <glm/vec2.hpp>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace contacts
{
	struct State
	{
		glm::vec2 position = {};
		glm::vec2 facing = {1.f, 0.f};
		glm::vec2 velocity = {};
	};

	struct Ghost
	{
		uint32_t id = 0;
		State last;          // as it was last seen
		float lostAt = 0.f;  // when it went out of sight
		bool armed = false;  // its spot has been out of sight since
	};

	struct Memory
	{
		std::vector<Ghost> ghosts;

		// Per thing: its last seen state, and whether it was seen last frame.
		struct Track
		{
			State last;
			bool seen = false;
		};
		std::unordered_map<uint32_t, Track> tracks;
	};

	void clear(Memory &memory);

	// One thing, this frame: seen or not, and its state while it is (ignored
	// while it is not). A thing never seen leaves no ghost.
	void observe(Memory &memory, uint32_t id, bool seen, const State &state, float now);

	// Ghosts whose spot is out of sight are armed; armed ones whose spot is
	// back in sight are cleared, when `clearWhenChecked`. Any older than
	// `forgetAfter` seconds go, unless it is 0.
	void check(Memory &memory, const std::function<bool(glm::vec2)> &inSight, float now,
		bool clearWhenChecked, float forgetAfter);
}
