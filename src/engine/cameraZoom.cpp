#include <engine/cameraZoom.h>

#include <algorithm>
#include <cmath>

namespace camera
{

namespace
{
	float clampZoom(float zoom, const ZoomParams &params)
	{
		return std::clamp(zoom, params.minZoom, params.maxZoom);
	}
}

void zoomBy(Zoom &zoom, float steps, const ZoomParams &params)
{
	zoom.target = clampZoom(zoom.target * std::pow(params.stepFactor, steps), params);
}

void zoomTo(Zoom &zoom, float target, const ZoomParams &params)
{
	zoom.target = clampZoom(target, params);
}

void easeZoom(Zoom &zoom, float deltaTime, const ZoomParams &params)
{
	zoom.target = clampZoom(zoom.target, params);
	zoom.current = clampZoom(zoom.current, params);

	// Exponential approach, so the feel does not depend on frame rate: the
	// fraction of the gap left after t seconds is exp(-rate * t) however that
	// time was cut into frames. Interpolating the logs is what makes it
	// multiplicative.
	const float t = 1.f - std::exp(-params.easeRate * deltaTime);
	const float logCurrent = std::log(zoom.current);
	const float logTarget = std::log(zoom.target);
	// Clamped again on the way out: exp(log(x)) is not exactly x, and landing
	// one rounding step under the floor would draw a frame past the limit.
	zoom.current = clampZoom(std::exp(logCurrent + (logTarget - logCurrent) * t), params);
}

}
