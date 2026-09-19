#include <level.h>

#include <glm/glm.hpp>
#include <cmath>
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
		if (word == "marker")
		{
			std::string kind;
			Marker m;
			if (!(in >> kind >> m.position.x >> m.position.y)) { return false; }
			if (kind == "resource") { m.kind = Marker::Kind::Resource; }
			else if (kind == "gate") { m.kind = Marker::Kind::Gate; }
			else { return false; }
			out.markers.push_back(m);
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
	for (const Marker &m : level.markers)
	{
		file << "marker " << (m.kind == Marker::Kind::Gate ? "gate" : "resource")
			<< " " << m.position.x << " " << m.position.y << "\n";
	}
	file << "\n";
	for (const Scenery &s : level.scenery)
	{
		file << "scenery " << s.art << " " << s.position.x << " " << s.position.y << " "
			<< s.size << " " << s.depth << "\n";
	}
	return (bool)file;
}

}
