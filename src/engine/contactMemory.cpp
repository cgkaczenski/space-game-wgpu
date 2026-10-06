#include <engine/contactMemory.h>

#include <algorithm>

namespace contacts
{

void clear(Memory &memory)
{
	memory.ghosts.clear();
	memory.tracks.clear();
}

void observe(Memory &memory, uint32_t id, bool seen, const State &state, float now)
{
	Memory::Track &track = memory.tracks[id];
	if (seen)
	{
		// Seen: whatever ghost it left is gone, and this is its last state.
		memory.ghosts.erase(std::remove_if(memory.ghosts.begin(), memory.ghosts.end(),
			[&](const Ghost &g) { return g.id == id; }), memory.ghosts.end());
		track.last = state;
		track.seen = true;
		return;
	}
	if (track.seen)
	{
		// Lost this frame: a ghost where it was last seen.
		Ghost ghost;
		ghost.id = id;
		ghost.last = track.last;
		ghost.lostAt = now;
		memory.ghosts.push_back(ghost);
	}
	track.seen = false;
}

void check(Memory &memory, const std::function<bool(glm::vec2)> &inSight, float now,
	bool clearWhenChecked, float forgetAfter)
{
	memory.ghosts.erase(std::remove_if(memory.ghosts.begin(), memory.ghosts.end(), [&](Ghost &g)
	{
		if (forgetAfter > 0.f && now - g.lostAt > forgetAfter) { return true; }
		const bool looked = inSight(g.last.position);
		if (!looked)
		{
			g.armed = true;
			return false;
		}
		return clearWhenChecked && g.armed;
	}), memory.ghosts.end());
}

}
