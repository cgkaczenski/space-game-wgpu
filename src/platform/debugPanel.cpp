#include <debugPanel.h>

#include "imgui.h"
#include <framePacing.h>
#include <render/wgpu2d.h>

namespace debugPanel
{

void renderStats()
{
	// N2a: what the last frame cost. The counts are the renderer's and exact;
	// the CPU figure is the loop's own measured delta -- not the paced step the
	// game was given, which would read a flat 16.7 whatever happened --
	// smoothed so it can be read.
	// There is no true GPU timing here -- this adapter has no TimestampQuery
	// (roadmap N2) -- so the "gpu" line is submit-to-work-done, which is a
	// latency bound rather than a measure of GPU work. It is labelled that way
	// on purpose; a number called "gpu" that is not one is worse than none.
	static float smoothedMs = 0.f;
	const float frameMs = framePacing::lastMeasured() * 1000.f;
	smoothedMs += (frameMs - smoothedMs) * 0.1f;

	const wgpu2d::FrameStats stats = wgpu2d::frameStats();
	ImGui::Text("cpu %.2f ms (%.0f fps)", smoothedMs, smoothedMs > 0.f ? 1000.f / smoothedMs : 0.f);
	if (stats.gpuMillis >= 0.f)
	{
		ImGui::Text("submit->done %.2f ms", stats.gpuMillis);
	}
	else
	{
		ImGui::TextDisabled("submit->done  --");
	}
	ImGui::Text("%d quads  %d runs  %d flushes", stats.quads, stats.drawRuns, stats.flushes);
	ImGui::Text("%d cameras  %d pipelines", stats.cameras, stats.pipelineVariants);

	// The last few seconds, frame by frame: an average hides a zig-zag.
	framePacing::debugUi();

	// Render scale beside the numbers, because it is the answer when the frame
	// rate falls and the window is large. Rasterise fewer pixels; the framing,
	// the HUD and the mouse are unaffected.
	float scale = wgpu2d::renderScale();
	if (ImGui::SliderFloat("Render scale", &scale, 0.25f, 1.f, "%.2f"))
	{
		wgpu2d::setRenderScale(scale);
	}
}

void section(const char *name, void (*controls)())
{
	ImGui::PushID(name);
	ImGui::SeparatorText(name);
	controls();
	ImGui::PopID();
}

}
