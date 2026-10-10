#include <crates.h>
#include <textLook.h>
#include <tuning.h>
#include <weapons.h>

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace crates
{

namespace
{
	std::vector<Crate> all;
	int nextCrateId = 1;
	int nextThingId = 1;

	// Tuning.
	float dropChance = 0.35f;        // a kill leaves a crate
	float countTwo = 0.3f;           // a rolled crate holds two
	float countThree = 0.1f;         // or three (else one)
	float kindWeight[weapons::slotCount] = {35.f, 25.f, 20.f, 20.f};  // burst, heavy, missile, beam
	float stunChance = 0.15f;
	float lockdownChance = 0.1f;
	float spreadChance = 0.15f;      // never on the beam

	float openReach = 2200.f;        // from the ship
	float openSeconds = 3.f;
	float size = 170.f;              // world units across
	float throwSpeed = 700.f;
	float drift = 2.5f;              // per second, to a stop

	const glm::vec4 bodyColour = {0.42f, 0.38f, 0.28f, 1.f};
	const glm::vec4 bandColour = {0.62f, 0.56f, 0.4f, 1.f};

	float random01() { return (float)std::rand() / (float)RAND_MAX; }

	inventory::Item rollItem(int kind = -1, AbilityChoice stun = AbilityChoice::Random,
		AbilityChoice lockdown = AbilityChoice::Random, AbilityChoice spread = AbilityChoice::Random)
	{
		inventory::Item it;
		if (kind < 0)
		{
			float total = 0.f;
			for (float w : kindWeight) { total += std::max(w, 0.f); }
			float pick = random01() * total;
			kind = weapons::slotCount - 1;
			for (int k = 0; k < weapons::slotCount; k++)
			{
				pick -= std::max(kindWeight[k], 0.f);
				if (pick <= 0.f) { kind = k; break; }
			}
		}
		it.kind = kind;
		auto decide = [](AbilityChoice c, float chance)
		{
			return c == AbilityChoice::Yes || (c == AbilityChoice::Random && random01() < chance);
		};
		it.stun = decide(stun, stunChance);
		it.lockdown = decide(lockdown, lockdownChance);
		it.spread = it.kind != 3 && decide(spread, spreadChance);
		return it;
	}

	Crate &make(glm::vec2 at, glm::vec2 velocity)
	{
		Crate c;
		c.id = nextCrateId++;
		c.position = at;
		c.velocity = velocity;
		c.angle = random01() * 360.f;
		c.spin = (random01() - 0.5f) * 240.f;
		c.grid = hold::make(width, height);
		all.push_back(c);
		return all.back();
	}

	void fill(Crate &c, int count)
	{
		for (int i = 0; i < count; i++)
		{
			Thing t;
			t.item = rollItem();
			putAnywhere(c, t);
		}
	}

	int rolledCount()
	{
		const float r = random01();
		return r < countThree ? 3 : r < countThree + countTwo ? 2 : 1;
	}

	hold::Shape square()
	{
		hold::Shape s;
		s.cells = {{0, 0}};
		return s;
	}
}

inventory::Item roll() { return rollItem(); }

void reset(const std::vector<level::CratePlacement> &placed)
{
	all.clear();
	for (const level::CratePlacement &p : placed)
	{
		Crate &c = make(p.position, {});
		c.spin = 0.f;
		if (p.items.empty()) { fill(c, rolledCount()); continue; }
		for (const GunChoice &g : p.items)
		{
			Thing t;
			t.item = rollItem(g.weapon, g.stun, g.lockdown, g.spread);
			putAnywhere(c, t);
		}
	}
}

void enemyKilled(glm::vec2 at, bool boss)
{
	if (!boss && random01() >= dropChance) { return; }
	const float a = random01() * 6.2831853f;
	Crate &c = make(at, glm::vec2(std::cos(a), std::sin(a)) * throwSpeed * 0.5f);
	fill(c, boss ? 3 : rolledCount());
}

void dropped(glm::vec2 at, glm::vec2 direction, const Thing &thing)
{
	Crate &c = make(at, direction * throwSpeed);
	putAnywhere(c, thing);
}

Crate *find(int id)
{
	for (Crate &c : all) { if (c.id == id) { return &c; } }
	return nullptr;
}

const Thing *thingAt(const Crate &c, int thingId)
{
	for (const auto &t : c.things) { if (t.first == thingId) { return &t.second; } }
	return nullptr;
}

bool take(Crate &c, int thingId, Thing &out)
{
	for (size_t i = 0; i < c.things.size(); i++)
	{
		if (c.things[i].first != thingId) { continue; }
		out = c.things[i].second;
		c.things.erase(c.things.begin() + i);
		hold::remove(c.grid, thingId);
		return true;
	}
	return false;
}

bool put(Crate &c, const Thing &thing, glm::ivec2 sq)
{
	const int id = nextThingId;
	if (!hold::place(c.grid, id, square(), 0, sq)) { return false; }
	nextThingId++;
	c.things.push_back({id, thing});
	return true;
}

bool putAnywhere(Crate &c, const Thing &thing)
{
	glm::ivec2 at;
	int turns = 0;
	if (!hold::findSpot(c.grid, square(), at, turns)) { return false; }
	return put(c, thing, at);
}

bool move(Crate &c, int thingId, glm::ivec2 sq)
{
	return hold::place(c.grid, thingId, square(), 0, sq);
}

float reach() { return openReach; }

int update(float dt, glm::vec2 ship, glm::vec2 pointer, bool canOpen, int keepOpen, const Shown &shown)
{
	// Empty crates go -- not one open in the menu, which the player may be
	// putting things back into.
	all.erase(std::remove_if(all.begin(), all.end(),
		[&](const Crate &c) { return c.things.empty() && c.id != keepOpen; }), all.end());

	int opened = -1;
	for (Crate &c : all)
	{
		c.position += c.velocity * dt;
		c.velocity *= std::exp(-drift * dt);
		c.angle += c.spin * dt;
		c.spin *= std::exp(-drift * dt);

		const bool on = canOpen && glm::distance(pointer, c.position) <= size * 0.8f
			&& glm::distance(ship, c.position) <= openReach && (!shown || shown(c.position));
		if (!on) { c.hover = 0.f; continue; }
		c.hover += dt;
		if (c.hover >= openSeconds && opened < 0)
		{
			c.hover = 0.f;
			opened = c.id;
		}
	}
	return opened;
}

void draw(wgpu2d::Renderer2D &r, const Shown &shown)
{
	for (const Crate &c : all)
	{
		if (shown && !shown(c.position)) { continue; }
		// A box: a body, a lighter band round it and across it.
		const glm::vec4 rect = {c.position.x - size * 0.5f, c.position.y - size * 0.5f, size, size};
		const glm::vec2 origin = {};
		r.renderRectangle(rect, bandColour, origin, c.angle);
		const float inset = size * 0.12f;
		r.renderRectangle({rect.x + inset, rect.y + inset, size - 2.f * inset, size - 2.f * inset}, bodyColour, origin, c.angle);
		r.renderRectangle({c.position.x - size * 0.5f, c.position.y - size * 0.06f, size, size * 0.12f}, bandColour, origin, c.angle);

		// The ring: filling as the pointer is held on it.
		if (c.hover > 0.f)
		{
			const float t = std::min(c.hover / std::max(openSeconds, 0.01f), 1.f);
			const float radius = size * 0.95f;
			const int steps = 48;
			const glm::vec4 colour = {textLook::hintColour, 0.95f};
			const float start = -1.5707963f;
			glm::vec2 last = c.position + radius * glm::vec2(std::cos(start), std::sin(start));
			for (int i = 1; i <= (int)std::ceil(steps * t); i++)
			{
				const float a = start + 6.2831853f * std::min((float)i / steps, t);
				const glm::vec2 next = c.position + radius * glm::vec2(std::cos(a), std::sin(a));
				r.renderLine(last, next, colour, size * 0.07f);
				last = next;
			}
			r.renderCircleOutline(c.position, {textLook::hintColour, 0.25f}, radius, size * 0.03f, 48);
		}
	}
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("crates", {
	{"dropChance", dropChance},
	{"countTwo", countTwo},
	{"countThree", countThree},
	{"weight.burst", kindWeight[0]},
	{"weight.heavy", kindWeight[1]},
	{"weight.missile", kindWeight[2]},
	{"weight.beam", kindWeight[3]},
	{"stunChance", stunChance},
	{"lockdownChance", lockdownChance},
	{"spreadChance", spreadChance},
	{"openReach", openReach},
	{"openSeconds", openSeconds},
	{"size", size},
});

void debugUi()
{
	ImGui::Text("%d crates in space", (int)all.size());
	tune::SliderFloat("Drop chance", &dropChance, 0.f, 1.f, "%.2f of kills");
	tune::SliderFloat("Two weapons", &countTwo, 0.f, 1.f, "%.2f");
	tune::SliderFloat("Three weapons", &countThree, 0.f, 1.f, "%.2f");
	ImGui::TextDisabled("Kinds, by weight");
	tune::SliderFloat("Burst", &kindWeight[0], 0.f, 100.f, "%.0f");
	tune::SliderFloat("Heavy", &kindWeight[1], 0.f, 100.f, "%.0f");
	tune::SliderFloat("Missile", &kindWeight[2], 0.f, 100.f, "%.0f");
	tune::SliderFloat("Beam", &kindWeight[3], 0.f, 100.f, "%.0f");
	tune::SliderFloat("Stun", &stunChance, 0.f, 1.f, "%.2f");
	tune::SliderFloat("Lockdown", &lockdownChance, 0.f, 1.f, "%.2f");
	tune::SliderFloat("Spread", &spreadChance, 0.f, 1.f, "%.2f (not on the beam)");
	tune::SliderFloat("Open within", &openReach, 200.f, 6000.f, "%.0f of the ship");
	tune::SliderFloat("Hold to open", &openSeconds, 0.2f, 6.f, "%.1f s");
	tune::SliderFloat("Size", &size, 60.f, 400.f, "%.0f");
}

}
