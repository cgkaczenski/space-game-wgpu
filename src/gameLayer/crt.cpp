#include <crt.h>

#include "imgui.h"
#include <platformTools.h>

#include <fstream>
#include <iostream>
#include <sstream>

namespace crt
{

namespace
{
	wgpu2d::Effect effect;

	bool enabled = false;

	// The master knob, and the parts it scales. Defaults chosen to read as a
	// monitor rather than as a filter: the curvature is the first thing to look
	// broken, so it is the smallest of them.
	float strength = 1.0f;
	float curvature = 0.06f;
	float scanlines = 0.14f;
	float mask = 0.08f;
	float fringing = 0.5f;
	float vignette = 0.35f;

	// The scanline period, in whole output pixels. Not a count of lines across
	// the picture: a count gives a fractional period, the dark band drifts
	// against the sprite grid, and the moire that produces reads as the ship
	// being see-through rather than as a monitor.
	//
	// Wide and light rather than narrow and heavy. A narrow period at strength
	// reads as texture laid on the artwork; a wide one puts the dark band far
	// enough apart that the eye reads it as a line. The slider goes wider still,
	// because at this window size a very wide band is a legitimate look rather
	// than a broken one -- and the brightness compensation in the shader does
	// not care what the period is, since the wave averages 0.5 over any of them.
	float period = 8.f;

	bool readFile(const char *path, std::string &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) { return false; }
		std::stringstream ss;
		ss << file.rdbuf();
		out = ss.str();
		return true;
	}
}

bool init()
{
	std::string source;
	const char *path = RESOURCES_PATH "shaders/crt.wgsl";
	if (!readFile(path, source))
	{
		std::cerr << "crt: cannot read " << path << "\n";
		return false;
	}

	effect = wgpu2d::createEffect(source.c_str(), "crt");
	if (effect.id == 0)
	{
		std::cerr << "crt: effect did not compile\n";
		return false;
	}
	return true;
}

void setEnabled(bool e) { enabled = e; }
bool isEnabled() { return enabled; }

void apply()
{
	// Off costs nothing: no target, no extra pass, the frame goes straight to
	// the surface as it always did.
	if (!enabled || effect.id == 0 || strength <= 0.f)
	{
		wgpu2d::clearFinalEffect();
		return;
	}

	wgpu2d::EffectParams params;
	params.a = {strength, curvature, scanlines, mask};
	params.b = {period, vignette, fringing, 0.f};
	wgpu2d::setFinalEffect(effect, params);
}

void debugUi()
{
	bool e = enabled;
	if (ImGui::Checkbox("CRT", &e)) { setEnabled(e); }
	if (!enabled) { return; }

	// The master first, because it is the one a player would be given. The
	// rest are separated because they go wrong at different rates: curvature
	// reads as broken well before the scanlines do.
	ImGui::SliderFloat("CRT strength", &strength, 0.f, 2.f);
	ImGui::SliderFloat("Curvature", &curvature, 0.f, 0.3f);
	ImGui::SliderFloat("Scanlines", &scanlines, 0.f, 1.f);
	ImGui::SliderFloat("Scanline period px", &period, 2.f, 32.f, "%.0f");
	ImGui::SliderFloat("Aperture mask", &mask, 0.f, 0.5f);
	ImGui::SliderFloat("Fringing", &fringing, 0.f, 2.f);
	ImGui::SliderFloat("Vignette", &vignette, 0.f, 1.f);
}

}
