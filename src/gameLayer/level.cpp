#include <level.h>

#include <weapons.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace level
{

glm::vec2 direction(float degrees)
{
	const float r = glm::radians(degrees);
	return {std::cos(r), std::sin(r)};
}

namespace
{
	// An old deposit as a rock: a radius that grows with what it held, and a
	// seed from where it sat, so it loads the same rock every time.
	Asteroid depositAsRock(glm::vec2 at, float amount)
	{
		Asteroid a;
		a.position = at;
		a.radius = 250.f + 30.f * std::max(amount, 0.f);
		// Unsigned arithmetic: it wraps by definition, where signed overflow
		// would be undefined.
		a.seed = ((uint32_t)(int)at.x * 73856093u) ^ ((uint32_t)(int)at.y * 19349663u);
		return a;
	}

	// One line, already stripped of its comment. False if it does not parse.
	bool parseLine(std::istringstream &in, const std::string &word, Level &out)
	{
		if (word == "arena")
		{
			return (bool)(in >> out.arenaRadius);
		}
		if (word == "start")
		{
			return (bool)(in >> out.start.x >> out.start.y >> out.startFacingDegrees);
		}
		if (word == "enemy")
		{
			std::string kind;
			EnemyPlacement e;
			if (!(in >> kind >> e.position.x >> e.position.y >> e.facingDegrees)) { return false; }
			if (kind == "rusher") { e.behaviour = Enemy::Behaviour::CloseIn; }
			else if (kind == "sniper") { e.behaviour = Enemy::Behaviour::KeepDistance; }
			else if (kind == "boss") { e.behaviour = Enemy::Behaviour::Boss; }
			else { return false; }
			// Then, in any order: a weapon (B1; without one it rolls), and an
			// ability:choice for each of shield, cloak and ram -- yes or random
			// (without one, no).
			std::string word;
			while (in >> word)
			{
				// A weapon slot (B2): gun:kind[:stun=..][:lockdown=..][:spread=..]
				if (word.rfind("gun:", 0) == 0)
				{
					GunChoice g;
					std::istringstream parts(word.substr(4));
					std::string part;
					bool first = true;
					while (std::getline(parts, part, ':'))
					{
						if (first)
						{
							first = false;
							if (part == "random") { g.weapon = -1; continue; }
							g.weapon = weapons::shipWeaponSlot(part.c_str());
							if (g.weapon < 0) { return false; }
							continue;
						}
						const size_t eq = part.find('=');
						if (eq == std::string::npos) { return false; }
						const std::string name = part.substr(0, eq), choice = part.substr(eq + 1);
						AbilityChoice *slot = name == "stun" ? &g.stun : name == "lockdown" ? &g.lockdown
							: name == "spread" ? &g.spread : nullptr;
						if (!slot) { return false; }
						if (choice == "yes") { *slot = AbilityChoice::Yes; }
						else if (choice == "random") { *slot = AbilityChoice::Random; }
						else if (choice == "no") { *slot = AbilityChoice::No; }
						else { return false; }
					}
					e.guns.push_back(g);
					continue;
				}
				const size_t colon = word.find(':');
				if (colon != std::string::npos)
				{
					const std::string ability = word.substr(0, colon), choice = word.substr(colon + 1);
					AbilityChoice *slot = ability == "shield" ? &e.shield : ability == "cloak" ? &e.cloak
						: ability == "ram" ? &e.ram : nullptr;
					if (!slot) { return false; }
					if (choice == "yes") { *slot = AbilityChoice::Yes; }
					else if (choice == "random") { *slot = AbilityChoice::Random; }
					else if (choice == "no") { *slot = AbilityChoice::No; }
					else { return false; }
					continue;
				}
				// A bare kind, from before B2: one gun, its modifiers rolled.
				GunChoice g;
				g.weapon = weapons::shipWeaponSlot(word.c_str());
				if (g.weapon < 0) { return false; }
				g.stun = g.lockdown = g.spread = AbilityChoice::Random;
				e.guns.push_back(g);
			}
			out.enemies.push_back(e);
			return true;
		}
		if (word == "resource")
		{
			// A deposit, from before asteroids replaced them (A4): a rock
			// where it was, sized by how much it held.
			glm::vec2 at;
			float amount = 6.f;
			if (!(in >> at.x >> at.y)) { return false; }
			in >> amount; // optional, as it was
			out.asteroids.push_back(depositAsRock(at, amount));
			return true;
		}
		if (word == "marker")
		{
			std::string kind;
			glm::vec2 at;
			if (!(in >> kind >> at.x >> at.y)) { return false; }
			// Before L3 a resource was a marker; since A4 a rock.
			if (kind == "resource") { out.asteroids.push_back(depositAsRock(at, 6.f)); return true; }
			if (kind != "gate") { return false; }
			out.markers.push_back({Marker::Kind::Gate, at});
			return true;
		}
		if (word == "ring")
		{
			Ring r;
			if (!(in >> r.position.x >> r.position.y >> r.radius) || r.radius < 0.f) { return false; }
			out.rings.push_back(r);
			return true;
		}
		if (word == "asteroid")
		{
			Asteroid a;
			if (!(in >> a.position.x >> a.position.y >> a.radius >> a.seed) || a.radius <= 0.f) { return false; }
			out.asteroids.push_back(a);
			return true;
		}
		if (word == "field")
		{
			AsteroidField f;
			if (!(in >> f.seed >> f.maxSize >> f.maxGap) || f.maxSize <= 0.f || f.maxGap < 0.f) { return false; }
			out.fields.push_back(f);
			return true;
		}
		if (word == "paint" || word == "erase")
		{
			// A stamp belongs to the field above it.
			FieldStamp s;
			s.erase = word == "erase";
			if (out.fields.empty() || !(in >> s.position.x >> s.position.y >> s.radius) || s.radius <= 0.f)
			{
				return false;
			}
			out.fields.back().stamps.push_back(s);
			return true;
		}
		if (word == "core")
		{
			// One of the field above it's cores, placed by hand.
			glm::vec2 at;
			if (out.fields.empty() || !(in >> at.x >> at.y)) { return false; }
			out.fields.back().cores.push_back(at);
			return true;
		}
		if (word == "lane")
		{
			Lane l;
			if (!(in >> l.width >> l.speed) || l.width <= 0.f) { return false; }
			out.lanes.push_back(l);
			return true;
		}
		if (word == "point")
		{
			// A point of the lane above it.
			glm::vec2 at;
			if (out.lanes.empty() || !(in >> at.x >> at.y)) { return false; }
			out.lanes.back().points.push_back(at);
			return true;
		}
		if (word == "scenery")
		{
			Scenery s;
			if (!(in >> s.art >> s.position.x >> s.position.y >> s.size >> s.depth)) { return false; }
			out.scenery.push_back(s);
			return true;
		}
		return false;
	}
}

bool load(const char *path, Level &out)
{
	std::ifstream file(path);
	if (!file.is_open()) { return false; }

	out = {};
	std::string line;
	int number = 0;
	while (std::getline(file, line))
	{
		number++;
		const size_t hash = line.find('#');
		if (hash != std::string::npos) { line.erase(hash); }

		std::istringstream in(line);
		std::string word;
		if (!(in >> word)) { continue; } // blank

		if (!parseLine(in, word, out))
		{
			std::cerr << "level: " << path << ":" << number << ": cannot read \"" << line << "\"\n";
		}
	}
	return true;
}

std::vector<std::string> list(const std::string &directory)
{
	std::vector<std::string> names;
	std::error_code error; // a missing folder is an empty list, not an exception
	for (const auto &entry : std::filesystem::directory_iterator(directory, error))
	{
		if (entry.is_regular_file(error) && entry.path().extension() == ".txt")
		{
			names.push_back(entry.path().filename().string());
		}
	}
	std::sort(names.begin(), names.end());
	return names;
}

bool save(const char *path, const Level &level)
{
	std::ofstream file(path);
	if (!file.is_open()) { return false; }

	file << "arena " << level.arenaRadius << "\n";
	file << "start " << level.start.x << " " << level.start.y << " "
		<< level.startFacingDegrees << "\n\n";

	for (const EnemyPlacement &e : level.enemies)
	{
		file << "enemy " << (e.behaviour == Enemy::Behaviour::KeepDistance ? "sniper"
			: e.behaviour == Enemy::Behaviour::Boss ? "boss" : "rusher")
			<< " " << e.position.x << " " << e.position.y << " " << e.facingDegrees;
		for (const GunChoice &g : e.guns)
		{
			file << " gun:" << (g.weapon >= 0 ? weapons::shipWeaponKey(g.weapon) : "random");
			auto modifier = [&](const char *name, AbilityChoice c)
			{
				if (c == AbilityChoice::Yes) { file << ":" << name << "=yes"; }
				if (c == AbilityChoice::Random) { file << ":" << name << "=random"; }
			};
			modifier("stun", g.stun);
			modifier("lockdown", g.lockdown);
			modifier("spread", g.spread);
		}
		auto ability = [&](const char *name, AbilityChoice c)
		{
			if (c == AbilityChoice::Yes) { file << " " << name << ":yes"; }
			if (c == AbilityChoice::Random) { file << " " << name << ":random"; }
		};
		ability("shield", e.shield);
		ability("cloak", e.cloak);
		ability("ram", e.ram);
		file << "\n";
	}
	file << "\n";
	for (const Marker &m : level.markers)
	{
		file << "marker gate " << m.position.x << " " << m.position.y << "\n";
	}
	file << "\n";
	for (const Ring &r : level.rings)
	{
		file << "ring " << r.position.x << " " << r.position.y << " " << r.radius << "\n";
	}
	file << "\n";
	for (const Asteroid &a : level.asteroids)
	{
		file << "asteroid " << a.position.x << " " << a.position.y << " " << a.radius << " " << a.seed << "\n";
	}
	file << "\n";
	for (const AsteroidField &f : level.fields)
	{
		file << "field " << f.seed << " " << f.maxSize << " " << f.maxGap << "\n";
		for (const glm::vec2 &c : f.cores) { file << "core " << c.x << " " << c.y << "\n"; }
		for (const FieldStamp &s : f.stamps)
		{
			file << (s.erase ? "erase " : "paint ") << s.position.x << " " << s.position.y << " " << s.radius << "\n";
		}
		file << "\n";
	}
	for (const Scenery &s : level.scenery)
	{
		file << "scenery " << s.art << " " << s.position.x << " " << s.position.y << " "
			<< s.size << " " << s.depth << "\n";
	}
	for (const Lane &l : level.lanes)
	{
		file << "\nlane " << l.width << " " << l.speed << "\n";
		for (const glm::vec2 &p : l.points) { file << "point " << p.x << " " << p.y << "\n"; }
	}
	return (bool)file;
}

std::vector<AsteroidField> fieldsAsPlayed(const Level &level, float gateClearing)
{
	// A lane as a chain of erasers along it, close enough that their circles
	// overlap well past the lane's half-width: the cut's edge wavers by under
	// a twentieth of the width.
	std::vector<FieldStamp> cuts;
	for (const Lane &l : level.lanes)
	{
		const float r = l.width * 0.5f;
		const float step = std::max(r * 0.6f, 1.f);
		for (size_t i = 0; i + 1 < l.points.size(); i++)
		{
			const glm::vec2 a = l.points[i], b = l.points[i + 1];
			const float length = glm::length(b - a);
			const int n = std::max(1, (int)std::ceil(length / step));
			for (int k = 0; k < n; k++) { cuts.push_back({a + (b - a) * ((float)k / (float)n), r, true}); }
		}
		if (!l.points.empty()) { cuts.push_back({l.points.back(), r, true}); }
	}
	if (gateClearing > 0.f)
	{
		for (const Marker &m : level.markers)
		{
			if (m.kind == Marker::Kind::Gate) { cuts.push_back({m.position, gateClearing, true}); }
		}
	}
	std::vector<AsteroidField> fields = level.fields;
	for (AsteroidField &f : fields) { f.stamps.insert(f.stamps.end(), cuts.begin(), cuts.end()); }
	return fields;
}

}
