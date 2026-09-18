#pragma once

// What is left when things happen: an enemy's death (gameplay roadmap C4). A
// fireball that swells and fades, and the ship's own sprite broken into pieces
// that fly apart, spinning, then settle and stay -- a wreck field. Past a limit
// (400 pieces, a debug slider) the oldest fade out.
//
// The debris is the enemy's art, not generic shrapnel: each piece is drawn with
// one part of that enemy's cell of the ship sheet, so the wreck is visibly the
// ship that died. Nothing here is new to the renderer -- an additive glow and
// sub-rectangles of a texture already in use.
//
// Restart clears it: an explosion is something that happened, not a setting.

#include <render/wgpu2d.h>
#include <enemy.h>

namespace effects
{
	bool init();     // builds the fireball's glow texture
	void cleanup();
	void reset();

	// An enemy just died. `cell` is its texture coordinates in the ship sheet,
	// as the atlas gives them, so the debris can be cut from it.
	void enemyKilled(const Enemy &enemy, glm::vec4 cell);

	// Game time.
	void update(float gameDeltaTime);

	// The debris, under alpha, with the ship sheet the pieces are cut from.
	// Draw with the enemies, so a wreck sits where ships sit.
	void drawDebris(wgpu2d::Renderer2D &renderer, wgpu2d::Texture shipSheet);

	// The fireballs. **The caller must have set `BlendMode::Additive`**, as for
	// bullet glows: they go in that same pass.
	void drawGlow(wgpu2d::Renderer2D &renderer);

	// A missile's lock: a dashed red box round the target, its dashes marching
	// and pulsing, so it reads as live. `size` is the box's side; `time` drives
	// the march. Plain rectangles, under alpha.
	void drawTargetBox(wgpu2d::Renderer2D &renderer, glm::vec2 centre, float size, float time);

	void debugUi();
}
