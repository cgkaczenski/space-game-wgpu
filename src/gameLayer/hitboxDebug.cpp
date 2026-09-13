#include <hitboxDebug.h>

#include "imgui.h"

namespace hitboxDebug
{

namespace
{
	bool showing = false;
	// On by default so that turning outlines on behaves as it always has.
	bool freezeDamage = true;

	wgpu2d::Color4f outlineColor(bool overlapping)
	{
		return overlapping ? Colors_Red : Colors_Green;
	}
}

bool isDamageFrozen() { return showing && freezeDamage; }

void draw(wgpu2d::Renderer2D &renderer,
	const collision::ICollisionSystem &collisions,
	const collision::Circle &playerHitbox,
	const std::vector<Enemy> &enemies,
	const std::vector<Bullet> &bullets)
{
	if (!showing) { return; }

	bool playerHit = false;
	for (auto &b : bullets)
	{
		if (b.isEnemy && collisions.overlaps(b.getHitbox(), playerHitbox))
		{
			playerHit = true;
			break;
		}
	}
	renderer.renderCircleOutline(playerHitbox.center, outlineColor(playerHit),
		playerHitbox.radius, 8.f, 32);

	for (auto &e : enemies)
	{
		const auto enemyHitbox = e.getHitbox();
		bool enemyHit = false;
		for (auto &b : bullets)
		{
			if (!b.isEnemy && collisions.overlaps(b.getHitbox(), enemyHitbox))
			{
				enemyHit = true;
				break;
			}
		}
		renderer.renderCircleOutline(enemyHitbox.center, outlineColor(enemyHit),
			enemyHitbox.radius, 8.f, 32);
	}

	for (auto &b : bullets)
	{
		const auto bulletHitbox = b.getHitbox();
		bool bulletHit = false;
		if (b.isEnemy)
		{
			bulletHit = collisions.overlaps(bulletHitbox, playerHitbox);
		}
		else
		{
			for (auto &e : enemies)
			{
				if (collisions.overlaps(bulletHitbox, e.getHitbox()))
				{
					bulletHit = true;
					break;
				}
			}
		}
		renderer.renderCircleOutline(bulletHitbox.center, outlineColor(bulletHit),
			bulletHitbox.radius, 6.f, 16);
	}
}

void debugUi()
{
	ImGui::Checkbox("Show", &showing);
	if (showing)
	{
		ImGui::SameLine();
		ImGui::Checkbox("Freeze damage", &freezeDamage);
	}
}

}
