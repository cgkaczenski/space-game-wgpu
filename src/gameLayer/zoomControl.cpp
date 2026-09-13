#include <zoomControl.h>

#include <engine/cameraZoom.h>
#include "imgui.h"
#include "platformInput.h"

#include <algorithm>

namespace zoomControl
{

namespace
{
	// The zoom the game has always drawn at, so nothing looks different until
	// someone scrolls.
	constexpr float defaultZoom = 0.5f;

	constexpr float closestZoom = 1.f;
	constexpr float farthestZoom = 0.2f;

	// Held -/= in steps per second; a wheel notch is one step.
	constexpr float keyStepsPerSecond = 4.f;

	camera::Zoom zoom = {defaultZoom, defaultZoom};
	camera::ZoomParams params;

	// The furthest out the view can go before its edges reach the despawn
	// ring. The view is framebuffer / zoom world units across, so its longer
	// edge is half the longer side over zoom from the player. Edges, not
	// corners: a fullsized window at the default 0.5 already has its corners
	// just past the ring, and a corner rule would force it to zoom in.
	float floorFor(glm::vec2 framebufferSize, float despawnDistance)
	{
		const float halfLongerSide = std::max(framebufferSize.x, framebufferSize.y) * 0.5f;
		return std::clamp(halfLongerSide / despawnDistance, farthestZoom, closestZoom);
	}
}

float update(float realDeltaTime, glm::vec2 framebufferSize, float despawnDistance)
{
	params.minZoom = floorFor(framebufferSize, despawnDistance);
	params.maxZoom = closestZoom;

	const ImGuiIO &io = ImGui::GetIO();

	// Scrolling over the debug panel scrolls the panel.
	if (!io.WantCaptureMouse)
	{
		const float scroll = platform::getScrollY();
		if (scroll != 0.f) { camera::zoomBy(zoom, scroll, params); }
	}

	// And typing a '-' into a panel field types it.
	if (!io.WantCaptureKeyboard)
	{
		float keySteps = 0.f;
		if (platform::isButtonHeld(platform::Button::Equal)) { keySteps += 1.f; }
		if (platform::isButtonHeld(platform::Button::Minus)) { keySteps -= 1.f; }
		if (keySteps != 0.f)
		{
			camera::zoomBy(zoom, keySteps * keyStepsPerSecond * realDeltaTime, params);
		}
	}

	camera::easeZoom(zoom, realDeltaTime, params);
	return zoom.current;
}

void debugUi()
{
	float target = zoom.target;
	if (ImGui::SliderFloat("Zoom", &target, params.minZoom, params.maxZoom, "%.2f",
		ImGuiSliderFlags_Logarithmic))
	{
		camera::zoomTo(zoom, target, params);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Reset")) { camera::zoomTo(zoom, defaultZoom, params); }
	ImGui::TextDisabled("floor %.2f for this window  (scroll, -/=)", params.minZoom);
}

}
