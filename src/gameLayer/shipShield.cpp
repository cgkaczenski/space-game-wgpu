#include <shipShield.h>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace shield
{

namespace
{
	// Two textures, because the bubble is two different jobs. The rim is a
	// ring drawn additively (light adds). The tint is a disc drawn with alpha,
	// which is the only way to make the hull *darker* and bluer -- additive
	// can brighten and nothing else.
	wgpu2d::Texture rim;
	wgpu2d::Texture tint;

	bool active = false;
	float level = 0.f;   // eased `active`, 0..1
	float flare = 0.f;   // spikes on a hit, decays
	float phase = 0.f;

	// How it behaves.
	const float raisePerSecond = 9.f;
	const float dropPerSecond = 5.f;
	const float flareDecayPerSecond = 6.f;
	const float pulseHz = 0.9f;      // slow breathing, so it reads as powered
	const float pulseDepth = 0.12f;

	// How it looks. Two rings: a wide soft one and a tighter bright one. Under
	// additive they sum, which is what gives the rim a core instead of a flat
	// band -- a single quad cannot do it, since a quad has one colour per pixel.
	//
	// Sized to clear the hull. The shell's silhouette is at r = 1 of its quad
	// (the old ring peaked at 0.80 of it), so these are the previous numbers
	// scaled by that: the bubble is the same size on screen, the texture just
	// reaches its edge instead of its peak.
	const float haloScale = 1.38f;   // fractions of the ship's size
	const float shellScale = 1.26f;
	// These are why the rim looked thick. shieldColor's blue is 1.0, the pulse
	// carries it to ~1.1, and where the two rings overlap the weights summed to
	// ~1.3 -- so the blue channel clipped and a wide plateau read as solid
	// white-blue however narrow the gradient underneath was. Sized so the sum
	// just reaches 1 at the peak: only the very centre of the ring saturates,
	// and the falloff is visible instead of clipped flat.
	const float haloWeight = 0.30f;
	const float shellWeight = 0.62f;

	// The glass. Deliberately dark: alpha blending is C*a + dst*(1-a), so a
	// dark blue at partial alpha pulls the hull down and toward blue, which is
	// what "behind something" looks like.
	const glm::vec4 tintColor = {0.10f, 0.22f, 0.55f, 1.f};
	const float tintStrength = 0.15f;   // peak alpha, before level and flare

	// Under 1 per channel, for the reason recorded in outline 14: values above
	// 1 are invisible under alpha and load-bearing under additive.
	const glm::vec4 shieldColor = {0.30f, 0.58f, 1.0f, 1.f};
	const glm::vec4 flareColor = {0.55f, 0.80f, 1.0f, 1.f};

	// Fresnel: why a bubble reads as a sphere. The shell's surface turns away
	// from the viewer toward the rim, so the rim lights up. For a sphere
	// facing the screen that is a pure function of distance from the centre --
	// the normal tilts as Nz = sqrt(1 - r^2) and the rim term is (1 - Nz)^p --
	// which is exactly what a radial texture can store. So "looks round"
	// costs a texture formula, not a mesh, a depth buffer or a light.
	//
	// Physically the exponent wants to be 4 or 5. It is 2 here, plus a small
	// constant, and that is a size decision rather than a shading one: at p=4
	// the curve is still under 0.1 at r=0.92, so nearly all the brightness
	// lives in the outermost 5% of the radius. The bubble is about 110px
	// across on screen, which makes that rim roughly three pixels -- correct
	// and invisible. A softer exponent spreads it over something a player can
	// see, and the constant gives the interior enough glow to read as a volume
	// instead of an outline.
	const float fresnelPower = 2.0f;
	const float bodyGlow = 0.06f;

	// A baked specular lobe, up and to the left. It is baked because the quad
	// is never rotated: the highlight therefore sits in a fixed world
	// direction, which reads as a fixed light source. Rotating the quad would
	// rotate the highlight, if it should ever track something.
	const glm::vec2 specularAt = {-0.40f, -0.40f};
	const float specularSpread = 0.130f;
	const float specularGain = 0.85f;

	float fresnelAt(float r)
	{
		if (r > 1.f) { return 0.f; }
		const float nz = std::sqrt(std::max(0.f, 1.f - r * r));
		const float rim = std::pow(1.f - nz, fresnelPower) + bodyGlow;
		// Feather the silhouette. A sphere's edge is hard, but a hard edge in
		// a magnified texture aliases; this is about a texel wide at 256.
		return rim * std::clamp((1.f - r) / 0.045f, 0.f, 1.f);
	}

	// The lit shell: fresnel rim plus the highlight.
	bool buildShellTexture()
	{
		const int size = 256;
		std::vector<unsigned char> pixels((size_t)size * size * 4);

		for (int y = 0; y < size; y++)
		{
			for (int x = 0; x < size; x++)
			{
				const float dx = (x + 0.5f) / size * 2.f - 1.f;
				const float dy = (y + 0.5f) / size * 2.f - 1.f;
				const float r = std::sqrt(dx * dx + dy * dy);

				float a = fresnelAt(r);
				if (r <= 1.f)
				{
					// Dimmed by Nz so the highlight sits *on* the sphere and
					// fades as the surface turns away, instead of floating.
					const float nz = std::sqrt(std::max(0.f, 1.f - r * r));
					const float sx = dx - specularAt.x;
					const float sy = dy - specularAt.y;
					const float spec = std::exp(-(sx * sx + sy * sy)
						/ (2.f * specularSpread * specularSpread)) * nz;
					a = std::max(a, spec * specularGain);
				}

				unsigned char *p = pixels.data() + ((size_t)y * size + x) * 4;
				p[0] = 255; p[1] = 255; p[2] = 255;
				p[3] = (unsigned char)(std::clamp(a, 0.f, 1.f) * 255.f);
			}
		}

		rim.createFromBuffer((const char *)pixels.data(), size, size, false, true);
		return rim.id != 0;
	}

	// The glass, denser toward the rim. Looking at a shell near its edge means
	// looking along it, so there is more material in the way -- which is why a
	// glass sphere darkens at its edges rather than tinting evenly.
	bool buildTintTexture()
	{
		const int size = 256;
		std::vector<unsigned char> pixels((size_t)size * size * 4);

		for (int y = 0; y < size; y++)
		{
			for (int x = 0; x < size; x++)
			{
				const float dx = (x + 0.5f) / size * 2.f - 1.f;
				const float dy = (y + 0.5f) / size * 2.f - 1.f;
				const float r = std::sqrt(dx * dx + dy * dy);

				float a = 0.f;
				if (r <= 1.f)
				{
					const float nz = std::sqrt(std::max(0.f, 1.f - r * r));
					const float depth = 0.40f + 0.60f * std::pow(1.f - nz, 1.6f);
					a = depth * std::clamp((1.f - r) / 0.05f, 0.f, 1.f);
				}

				unsigned char *p = pixels.data() + ((size_t)y * size + x) * 4;
				p[0] = 255; p[1] = 255; p[2] = 255;
				p[3] = (unsigned char)(std::clamp(a, 0.f, 1.f) * 255.f);
			}
		}

		tint.createFromBuffer((const char *)pixels.data(), size, size, false, true);
		return tint.id != 0;
	}

	void drawDisc(wgpu2d::Renderer2D &renderer, wgpu2d::Texture texture, glm::vec2 centre,
		float shipSize, float scale, glm::vec4 color)
	{
		const float size = shipSize * scale;
		renderer.renderRectangle({centre - glm::vec2(size * 0.5f), size, size}, texture, color);
	}
}

bool init()
{
	if (!buildShellTexture() || !buildTintTexture())
	{
		std::cerr << "shield: could not create the bubble textures\n";
		return false;
	}
	return true;
}

void cleanup()
{
	rim.cleanup();
	tint.cleanup();
}

void setActive(bool a) { active = a; }
bool isActive() { return active; }

void hit(float strength)
{
	if (strength <= 0.f || !active) { return; }
	flare += strength;
	if (flare > 1.f) { flare = 1.f; }
}

void draw(wgpu2d::Renderer2D &renderer, glm::vec2 shipPos, float shipSize, float dt)
{
	if (rim.id == 0 || tint.id == 0) { return; }

	const float target = active ? 1.f : 0.f;
	const float rate = active ? raisePerSecond : dropPerSecond;
	level += (target - level) * std::min(1.f, rate * std::max(0.f, dt));
	if (flare > 0.f)
	{
		flare *= std::exp(-flareDecayPerSecond * std::max(0.f, dt));
		if (flare < 0.004f) { flare = 0.f; }
	}

	if (level < 0.004f)
	{
		level = 0.f; // settle exactly: a shield that is down records no quads
		return;
	}
	phase += dt;

	const float pulse = 1.f + pulseDepth * std::sin(6.2831853f * pulseHz * phase);
	const float intensity = level * pulse;

	// The flare rides on top of the steady rim rather than replacing it, so a
	// hit brightens and slightly widens the bubble instead of recolouring it.
	const glm::vec4 base = shieldColor * intensity;
	const glm::vec4 extra = flareColor * (flare * level);

	// 1. The glass, in alpha. This is the half additive cannot do: it pulls the
	//    hull down and toward blue so the ship reads as being *behind*
	//    something, rather than merely having light added in front of it.
	//
	//    It fades out as the flare rises -- an impact blows the shield bright,
	//    and a tint on top of that would only mute the flash -- and it scales
	//    with `level`, so a shield on its way down takes the glass with it.
	const float glass = tintStrength * level * (1.f - flare);
	if (glass > 0.002f)
	{
		// The glass is the same sphere as the shell, so it is the same size:
		// both textures put their silhouette at r = 1.
		const float glassScale = shellScale;
		glm::vec4 color = tintColor;
		color.a = glass;
		drawDisc(renderer, tint, shipPos, shipSize, glassScale, color);
	}

	// 2. The rim, additive, on top of the glass.
	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	{
		// A slightly larger, dimmer copy sits outside the shell as an outer
		// glow. Two silhouettes rather than two rings now, which is what gives
		// the edge a bloom instead of a hard stop.
		glm::vec4 halo = base * haloWeight + extra * 0.6f;
		halo.a = 1.f; // the shape is the texture's; this is intensity only
		drawDisc(renderer, rim, shipPos, shipSize, haloScale + 0.06f * flare, halo);

		glm::vec4 shell = base * shellWeight + extra;
		shell.a = 1.f;
		drawDisc(renderer, rim, shipPos, shipSize, shellScale, shell);
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
}

void debugUi()
{
	bool a = active;
	if (ImGui::Checkbox("Shield", &a)) { setActive(a); }
	ImGui::SameLine();
	if (ImGui::SmallButton("Flare")) { hit(); }
}

}
