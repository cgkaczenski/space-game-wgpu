#pragma once

// Hitbox outlines, green or red by whether each one overlaps something it can
// hit. Debug only; nothing the player sees.
//
// It used to be a flag and a sixty-line block inline in gameLogic, and the flag
// had a second job nobody could see from the checkbox: while outlines were on,
// the bullet collision loop was skipped, so nothing took damage. That is
// useful -- overlaps stay on screen to be looked at instead of vanishing the
// frame they happen -- but it is a choice, so it is now a checkbox of its own.

#include <render/wgpu2d.h>
#include <engine/collisionSystem.h>
#include <bullet.h>
#include <enemy.h>
#include <vector>

namespace hitboxDebug
{
	// True while outlines are showing and damage is set to freeze with them.
	// The collision loop asks this rather than asking whether outlines are on.
	bool isDamageFrozen();

	// Outlines for the player, every enemy and every bullet. Does nothing when
	// hidden. Call after the world is drawn, so the outlines sit on top.
	void draw(wgpu2d::Renderer2D &renderer,
		const collision::ICollisionSystem &collisions,
		const collision::Circle &playerHitbox,
		const std::vector<Enemy> &enemies,
		const std::vector<Bullet> &bullets);

	void debugUi();
}
