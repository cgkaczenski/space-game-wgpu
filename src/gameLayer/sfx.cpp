#include <sfx.h>
#include <tuning.h>

#include "imgui.h"
#include <raudio.h>

#include <iostream>

namespace sfx
{

namespace
{
	Sound shot = {};
	bool loaded = false;
	bool enabled = false;
}

bool init()
{
	shot = LoadSound(RESOURCES_PATH "shoot.flac");
	loaded = shot.stream.buffer != nullptr;
	if (!loaded)
	{
		std::cerr << "AUDIO: failed to load " << RESOURCES_PATH "shoot.flac\n";
		return true;
	}
	SetSoundVolume(shot, 1.0);
	return true;
}

void cleanup()
{
	if (loaded) { UnloadSound(shot); }
	shot = {};
	loaded = false;
}

void playerShot()
{
	if (enabled && loaded) { PlaySound(shot); }
}

void enemyShot()
{
	if (enabled && loaded && !IsSoundPlaying(shot)) { PlaySound(shot); }
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("sound", {
	{"enabled", enabled},
});

void debugUi()
{
	if (tune::Checkbox("Sound effects", &enabled) && !enabled && loaded)
	{
		StopSound(shot);
	}
}

}
