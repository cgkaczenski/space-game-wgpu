#include <sfx.h>

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

void debugUi()
{
	if (ImGui::Checkbox("Sound effects", &enabled) && !enabled && loaded)
	{
		StopSound(shot);
	}
}

}
