#include <framePacing.h>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace framePacing
{

namespace
{
	bool enabled = true;

	// Whether pacing applied to the last frame: enabled, and the frames look
	// display-paced.
	bool active = false;

	float carry = 0.f;        // seconds owed to (or ahead of) wall time
	float refresh = 1.f / 60.f;
	float smoothedMeasured = 1.f / 60.f;
	float measured = 0.f;

	// The last four seconds or so at 60 Hz, in milliseconds.
	constexpr int historySize = 240;
	float measuredMs[historySize] = {};
	float steppedMs[historySize] = {};
	int next = 0;
}

float step(float measuredSeconds, float refreshHz)
{
	measured = measuredSeconds;

	// Said once, and again if it changes. Every step depends on this number:
	// a 75 Hz display reported as 60 would round every 13.3 ms frame up to a
	// 16.7 ms step and run the game a quarter fast.
	const float hz = refreshHz > 0.f ? refreshHz : 60.f;
	static float reportedHz = 0.f;
	if (hz != reportedHz)
	{
		std::cerr << "Frame pacing: " << hz << " Hz"
			<< (refreshHz > 0.f ? "" : " (display did not say; assumed)") << "\n";
		reportedHz = hz;
	}
	refresh = 1.f / hz;

	// Averaged, so one fast frame in a zig-zag does not switch pacing off.
	smoothedMeasured += (measuredSeconds - smoothedMeasured) * 0.05f;
	active = enabled && smoothedMeasured > refresh * 0.75f;

	float result = measuredSeconds;
	if (active)
	{
		const float owed = measuredSeconds + carry;
		const int refreshes = std::max(1, (int)std::lround(owed / refresh));
		result = refreshes * refresh;
		// Bounded, so a long stretch the rounding cannot absorb -- a stall, a
		// change of refresh rate -- does not turn into a debt paid off later.
		carry = std::clamp(owed - result, -refresh, refresh);
	}
	else
	{
		carry = 0.f;
	}

	measuredMs[next] = measuredSeconds * 1000.f;
	steppedMs[next] = result * 1000.f;
	next = (next + 1) % historySize;

	return result;
}

float lastMeasured() { return measured; }

void debugUi()
{
	// Same scale on both, and room above a 60 Hz frame for a doubled one, so a
	// zig-zag and a hitch are both visible at a glance.
	const float top = 50.f;
	ImGui::PlotLines("##measured", measuredMs, historySize, next, "measured ms",
		0.f, top, ImVec2(-1.f, 50.f));
	ImGui::PlotLines("##stepped", steppedMs, historySize, next, "game step ms",
		0.f, top, ImVec2(-1.f, 50.f));

	ImGui::Checkbox("Pace to display", &enabled);
	ImGui::SameLine();
	if (active)
	{
		ImGui::TextDisabled("%.0f Hz, carry %+.1f ms", 1.f / refresh, carry * 1000.f);
	}
	else if (enabled)
	{
		ImGui::TextDisabled("off: frames faster than the display (not paced)");
	}
}

}
