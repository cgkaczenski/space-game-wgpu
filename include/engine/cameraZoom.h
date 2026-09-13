#pragma once

// A camera zoom that eases toward where it was asked to go, within limits.
//
// Same boundary as cameraFollow: plain numbers in and out, no wgpu2d::Camera.
// The caller assigns `current` wherever it keeps its camera. Zoom here means
// what the renderer means by it: 1 is one world unit per pixel, 0.5 shows
// twice as much.
//
// Everything is multiplicative. A step multiplies the target and the easing
// runs in log space, because zoom is a ratio: going from 0.25 to 0.5 is the
// same change as 0.5 to 1, and an additive step would crawl at one end of the
// range and jump at the other.

namespace camera
{
	struct ZoomParams
	{
		float minZoom = 0.2f;
		float maxZoom = 1.f;

		// What one step multiplies the zoom by. Steps may be fractional -- a
		// trackpad sends small ones -- so 1.15 per step is 1.15^0.3 for 0.3.
		float stepFactor = 1.15f;

		// How fast `current` closes on `target`, per second. About 1/rate
		// seconds to cover most of the gap, at any frame rate.
		float easeRate = 12.f;
	};

	struct Zoom
	{
		float current = 1.f;
		float target = 1.f;
	};

	// Positive steps zoom in, negative out. Clamps the target.
	void zoomBy(Zoom &zoom, float steps, const ZoomParams &params);

	// Asks for a specific zoom; `current` still eases to it.
	void zoomTo(Zoom &zoom, float target, const ZoomParams &params);

	// Moves `current` toward `target`. The caller picks the clock; this
	// function has none. Also re-clamps both, so limits that change between
	// calls (a window resize) take effect.
	void easeZoom(Zoom &zoom, float deltaTime, const ZoomParams &params);
}
