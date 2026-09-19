#pragma once

// How a ship is drawn: one sprite from a sheet, turned to face where it is
// heading. The player and every enemy go through this.
//
// It takes values rather than a ship, so it knows nothing about Enemy or the
// player -- which is what lets those stay plain data with no renderer in their
// headers (roadmap R9). It used to live in tiledRenderer.h, for no reason
// beyond having been written next to it.

#include <cmath>
#include <render/wgpu2d.h>

inline void renderSpaceShip(
	wgpu2d::Renderer2D &renderer,
	glm::vec2 position, float size,
	wgpu2d::Texture texture,
	glm::vec4 uvs, glm::vec2 viewDirection,
	// Multiplies the sprite. Alpha below 1 fades the hull, which is how the
	// cloak makes it nearly transparent -- see gameLayer/cloak.h.
	wgpu2d::Color4f tint = Colors_White,
	// Stretches the hull along its heading, about its centre: the warp-out
	// (gameplay roadmap L1). The art's nose is along the sprite's height --
	// that is what the +90 below turns to face `viewDirection`.
	float lengthScale = 1.f)
{
	float spaceShipAngle = atan2(viewDirection.y, -viewDirection.x);

	const float length = size * lengthScale;
	renderer.renderRectangle({position - glm::vec2(size / 2, length / 2),
		size, length}, texture,
		tint, {}, glm::degrees(spaceShipAngle) + 90.f,
		uvs);
}
