#include <zoomControl.h>
#include <controls.h>

#include <engine/cameraZoom.h>
#include "imgui.h"
#include "platformInput.h"

#include <algorithm>

namespace zoomControl
{

namespace
{
	constexpr float closestZoom = 1.f;
	constexpr float farthestZoom = 0.2f;

	// Held -/= in steps per second; a wheel notch is one step.
	constexpr float keyStepsPerSecond = 4.f;

	camera::Zoom zoom = {farthestZoom, farthestZoom};
	camera::ZoomParams params;

	// The default is as far out as this window allows. That floor moves with
	// the window -- the game opens at 500x500 and is then fullsized -- so the
	// zoom follows it until the player zooms in, and picks it up again if
	// they zoom back out to it.
	bool followingFloor = true;
	bool firstUpdate = true;

	// The furthest out the view can go before its edges reach the despawn
	// ring. The view is framebuffer / zoom world units across, so its longer
	// edge is half the longer side over zoom from the player. Edges, not
	// corners: a fullsized window at the default 0.5 already has its corners
	// just past the ring, and a corner rule would force it to zoom in.
	float floorFor(glm::vec2 framebufferSize, float despawnDistance)
	{
		// No ring (a level is loaded, and nothing is removed for distance):
		// only the fixed limit.
		if (despawnDistance <= 0.f) { return farthestZoom; }
		const float halfLongerSide = std::max(framebufferSize.x, framebufferSize.y) * 0.5f;
		return std::clamp(halfLongerSide / despawnDistance, farthestZoom, closestZoom);
	}
}

float update(float realDeltaTime, glm::vec2 framebufferSize, float despawnDistance)
{
	params.minZoom = floorFor(framebufferSize, despawnDistance);
	params.maxZoom = closestZoom;

	if (followingFloor)
	{
		zoom.target = params.minZoom;
		// Start there rather than easing to it from wherever the constant was.
		if (firstUpdate) { zoom.current = params.minZoom; }
	}
	firstUpdate = false;

	const ImGuiIO &io = ImGui::GetIO();

	// Ctrl + wheel: the wheel alone switches weapons (weapons.h), and Shift
	// is the brake (playerMove). Scrolling over the debug panel scrolls the
	// panel.
	// Ctrl + wheel, vertical or horizontal, is the Zoom action's binding
	// (controls.cpp).
	if (!io.WantCaptureMouse)
	{
		const float scroll = controls::steps(controls::Action::Zoom);
		if (scroll != 0.f)
		{
			camera::zoomBy(zoom, scroll, params);
			followingFloor = zoom.target <= params.minZoom;
		}
	}

	// And typing a '-' into a panel field types it.
	if (!io.WantCaptureKeyboard)
	{
		float keySteps = 0.f;
		if (controls::held(controls::Action::ZoomIn)) { keySteps += 1.f; }
		if (controls::held(controls::Action::ZoomOut)) { keySteps -= 1.f; }
		if (keySteps != 0.f)
		{
			camera::zoomBy(zoom, keySteps * keyStepsPerSecond * realDeltaTime, params);
			followingFloor = zoom.target <= params.minZoom;
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
		followingFloor = zoom.target <= params.minZoom;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Reset")) { followingFloor = true; }
	ImGui::TextDisabled("floor %.2f for this window  (shift+scroll, -/=)", params.minZoom);
}

}
