#include <playerSettings.h>
#include <crt.h>
#include <gameLayer.h>

#include <raudio.h>
#include <glm/glm.hpp>
#include <fstream>
#include <sstream>
#include <string>

namespace playerSettings
{

namespace
{
	const char *file = "settings.cfg";
	Settings current;

	void apply()
	{
		SetMasterVolume(current.volume);
		crt::setPlayerStrength(current.crt);
		platform::setFullScreen(current.fullscreen);
	}

	void save()
	{
		std::ofstream out(file);
		out << "volume " << current.volume << "\n"
			<< "crt " << current.crt << "\n"
			<< "fullscreen " << (current.fullscreen ? 1 : 0) << "\n";
	}
}

bool init()
{
	std::ifstream in(file);
	std::string line;
	while (std::getline(in, line))
	{
		std::istringstream words(line);
		std::string key;
		float value = 0.f;
		if (!(words >> key >> value)) { continue; }
		if (key == "volume") { current.volume = glm::clamp(value, 0.f, 1.f); }
		else if (key == "crt") { current.crt = glm::clamp(value, 0.f, 1.f); }
		else if (key == "fullscreen") { current.fullscreen = value != 0.f; }
	}
	apply();
	return true;
}

const Settings &get() { return current; }

void set(const Settings &s)
{
	current = s;
	apply();
	save();
}

}
