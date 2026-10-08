#include <crt.h>
#include <tuning.h>

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

	bool enabled = true;

	// The master knob, and the parts it scales. Defaults chosen to read as a
	// monitor rather than as a filter: the curvature is the first thing to look
	// broken, so it is the smallest of them.
	float strength = 1.0f;
	float playerAmount = 1.f; // the player's setting, on top of the tuning
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

	// The phosphor glow. Off by default: it is the one part of the filter that
	// costs real work, and the rest of the CRT reads correctly without it.
	bool glowOn = true;
	wgpu2d::FinalGlow glow;

	// How warm the bright end of the picture runs, 0 to 1. One knob for two
	// things: the shader pulls highlights toward `warmColour`, and the glow is
	// tinted toward it. The planet art is blue-white, and a blue-white bloom
	// over it at full brightness was hard to look at; a warm phosphor is also
	// closer to how a real tube's highlights sat.
	float warmth = 0.5f;

	// Kept in step with `warmTint` in resources/shaders/crt.wgsl: the effect
	// has no free parameter left to carry a colour, only the amount.
	const glm::vec3 warmColour = {1.0f, 0.80f, 0.58f};

	// The round's transitions (gameplay roadmap L1), 0 .. 1 each. Not
	// settings: the game sets them every frame.
	float switchOff = 0.f;
	float whiteOut = 0.f;
	float warp = 0.f;
	float warpStreak = 0.35f; // how far toward the centre the longest streak reaches

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
void setPlayerStrength(float s) { playerAmount = glm::clamp(s, 0.f, 1.f); }
float playerStrength() { return playerAmount; }

void setTransition(float off, float white, float w)
{
	switchOff = off;
	whiteOut = white;
	warp = w;
}

void cleanup()
{
	// What this feature holds is the routing, not the pipeline: wgpu2d has no
	// call to release an effect, and render::wgpuShutdown releases what it
	// built. Leaving the final effect set would route the next frame, if there
	// were one, through a feature that has shut down.
	wgpu2d::clearFinalEffect();
	wgpu2d::clearFinalGlow();
	effect = {};
}

void apply()
{

	const bool transition = switchOff > 0.f || whiteOut > 0.f || warp > 0.f;
	const float master = strength * playerAmount;

	// Off costs nothing: no target, no extra pass, the frame goes straight to
	// the surface as it always did.
	if (effect.id == 0 || (!transition && (!enabled || master <= 0.f)))
	{
		wgpu2d::clearFinalEffect();
		wgpu2d::clearFinalGlow();
		return;
	}

	// The filter switched off but a transition running: the shader runs for
	// the transition alone, every part of the CRT look at zero. Warmth too --
	// it is the one part the master does not scale.
	if (!enabled || master <= 0.f)
	{
		wgpu2d::clearFinalGlow();
		wgpu2d::EffectParams params;
		params.a = {0.f, 0.f, 0.f, 0.f};
		params.b = {period, 0.f, 0.f, 0.f};
		params.c = {switchOff, whiteOut, warp * warpStreak, 0.f};
		wgpu2d::setFinalEffect(effect, params);
		return;
	}

	// The glow rides on the frame's target, which only exists while a final
	// effect is set -- so it follows the filter rather than switching on alone.
	if (glowOn && glow.intensity > 0.f)
	{
		// Scaled to unit luminance, so the tint turns the halo amber without
		// making it dimmer: red goes a little above 1, which is fine for a
		// colour that is added rather than blended.
		const float warmLuma = glm::dot(warmColour, glm::vec3(0.2126f, 0.7152f, 0.0722f));
		glow.tint = glm::mix(glm::vec3(1.f), warmColour / warmLuma, warmth);
		wgpu2d::FinalGlow scaled = glow;
		scaled.intensity *= playerAmount;
		wgpu2d::setFinalGlow(scaled);
	}
	else { wgpu2d::clearFinalGlow(); }

	wgpu2d::EffectParams params;
	params.a = {master, curvature, scanlines, mask};
	params.b = {period, vignette, fringing, warmth};
	params.c = {switchOff, whiteOut, warp * warpStreak, 0.f};
	wgpu2d::setFinalEffect(effect, params);
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("crt", {
	{"enabled", enabled},
	{"strength", strength},
	{"warmth", warmth},
	{"curvature", curvature},
	{"scanlines", scanlines},
	{"period", period},
	{"mask", mask},
	{"warpStreak", warpStreak},
	{"fringing", fringing},
	{"vignette", vignette},
	{"glowOn", glowOn},
	{"glow.threshold", glow.threshold},
	{"glow.knee", glow.knee},
	{"glow.width", glow.sigma},
	{"glow.intensity", glow.intensity},
});

void debugUi()
{
	bool e = enabled;
	{
		tune::Highlight h(&enabled); // a copy is edited, so it highlights for the real one
		if (ImGui::Checkbox("Enabled", &e)) { setEnabled(e); }
	}
	if (!enabled) { return; }

	// The master first, because it is the one a player would be given. The
	// rest are separated because they go wrong at different rates: curvature
	// reads as broken well before the scanlines do.
	tune::SliderFloat("Strength", &strength, 0.f, 2.f);
	tune::SliderFloat("Warmth", &warmth, 0.f, 1.f);
	tune::SliderFloat("Curvature", &curvature, 0.f, 0.3f);
	tune::SliderFloat("Scanlines", &scanlines, 0.f, 1.f);
	tune::SliderFloat("Scanline period px", &period, 2.f, 32.f, "%.0f");
	tune::SliderFloat("Aperture mask", &mask, 0.f, 0.5f);
	tune::SliderFloat("Warp streak", &warpStreak, 0.f, 0.9f);
	tune::SliderFloat("Fringing", &fringing, 0.f, 2.f);
	tune::SliderFloat("Vignette", &vignette, 0.f, 1.f);

	tune::Checkbox("Phosphor glow", &glowOn);
	if (glowOn)
	{
		tune::SliderFloat("Glow threshold", &glow.threshold, 0.f, 1.f);
		tune::SliderFloat("Glow knee", &glow.knee, 0.01f, 0.5f);
		tune::SliderFloat("Glow width", &glow.sigma, 0.5f, 8.f);
		tune::SliderFloat("Glow intensity", &glow.intensity, 0.f, 2.f);
	}
}

}
