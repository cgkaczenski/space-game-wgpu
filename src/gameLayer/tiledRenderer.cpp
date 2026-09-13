#include <tiledRenderer.h>

#include <cmath>

void TiledRenderer::render(wgpu2d::Renderer2D &renderer)
{
	auto viewRect = renderer.getViewRect();

	// A layer with parallax is drawn shifted by -view * strength, so it
	// appears to move slower than the world. In the layer's own unshifted
	// space, the view therefore spans `view * (1 + strength)` onward.
	glm::vec2 paralaxDistance = {viewRect.x, viewRect.y};
	paralaxDistance *= -paralaxStrength;

	const glm::vec2 layerMin = glm::vec2(viewRect.x, viewRect.y) - paralaxDistance;
	const glm::vec2 layerMax = layerMin + glm::vec2(viewRect.z, viewRect.w);

	// As many tiles as the view spans. This was a fixed 3x3 around the view's
	// corner, which only guaranteed coverage for a view one tile wide -- fine at
	// one zoom, a gap at the edge once the camera could zoom out. floor, not an
	// int cast: the cast rounds toward zero, which is one tile short for
	// negative coordinates, and the old -1..1 margin had been hiding that.
	const int firstX = (int)std::floor(layerMin.x / backgroundSize);
	const int firstY = (int)std::floor(layerMin.y / backgroundSize);
	const int lastX = (int)std::floor(layerMax.x / backgroundSize);
	const int lastY = (int)std::floor(layerMax.y / backgroundSize);

	for (int y = firstY; y <= lastY; y++)
	{
		for (int x = firstX; x <= lastX; x++)
		{
			renderer.renderRectangle(
				glm::vec4{x, y, 1, 1} * backgroundSize
				+glm::vec4(paralaxDistance,0,0)
				, texture);
		}
	}
}
