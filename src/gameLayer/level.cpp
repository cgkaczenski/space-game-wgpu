#include <level.h>

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
			else { return false; }
			out.enemies.push_back(e);
			return true;
		}
		if (word == "resource")
		{
			Resource r;
			if (!(in >> r.position.x >> r.position.y)) { return false; }
			in >> r.amount; // optional: the default stands
			out.resources.push_back(r);
			return true;
		}
		if (word == "marker")
		{
			std::string kind;
			glm::vec2 at;
			if (!(in >> kind >> at.x >> at.y)) { return false; }
			// Before L3 a resource was a marker. Read it as a deposit.
			if (kind == "resource") { out.resources.push_back({at, Resource().amount}); return true; }
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
			// The field above it, its core dragged away from the middle.
			glm::vec2 at;
			if (out.fields.empty() || !(in >> at.x >> at.y)) { return false; }
			out.fields.back().coreMoved = true;
			out.fields.back().core = at;
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
		file << "enemy " << (e.behaviour == Enemy::Behaviour::KeepDistance ? "sniper" : "rusher")
			<< " " << e.position.x << " " << e.position.y << " " << e.facingDegrees << "\n";
	}
	file << "\n";
	for (const Resource &r : level.resources)
	{
		file << "resource " << r.position.x << " " << r.position.y << " " << r.amount << "\n";
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
		if (f.coreMoved) { file << "core " << f.core.x << " " << f.core.y << "\n"; }
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
	return (bool)file;
}

}
