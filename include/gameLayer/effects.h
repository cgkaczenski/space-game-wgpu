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
	void enemyKilled(const Enemy &enemy, wgpu2d::Texture shipSheet, glm::vec4 cell);

	// Anything that breaks up: the same blast, and the same debris cut from
	// `cell` of `texture`. Enemies go through enemyKilled, the player's death
	// comes here directly (L1), and a spent deposit too (L3) -- a piece
	// carries its own texture, so a rock's debris is rock.
	void shipDestroyed(wgpu2d::Texture texture, glm::vec4 cell, glm::vec2 position,
		glm::vec2 facing, glm::vec2 velocity, float size);

	// A rock breaking (gameplay roadmap A4): a pale puff of dust that swells
	// and fades like a blast, without the fire or the flash. The pieces are the
	// rock's own business -- real rocks that fly apart -- so there is no debris.
	void rockBurst(glm::vec2 position, float size);

	// Game time.
	void update(float gameDeltaTime);

	// The debris, under alpha; each piece knows its own texture. Draw with
	// the enemies, so a wreck sits where ships sit.
	void drawDebris(wgpu2d::Renderer2D &renderer);

	// The fireballs. **The caller must have set `BlendMode::Additive`**, as for
	// bullet glows: they go in that same pass.
	void drawGlow(wgpu2d::Renderer2D &renderer);

	// A missile's lock: a dashed red box round the target, its dashes marching
	// and pulsing, so it reads as live. `size` is the box's side; `time` drives
	// the march. Plain rectangles, under alpha.
	void drawTargetBox(wgpu2d::Renderer2D &renderer, glm::vec2 centre, float size, float time);

	// The ram's trail, called every frame of the surge: afterimages of the ship
	// dropped behind every few hundredths of a second, fading, and thin streaks
	// of light left in space for the ship to rush past. `shipCell` is the
	// player's texture coordinates in the ship sheet.
	void ramTrail(glm::vec2 shipPos, glm::vec2 direction, float shipSize, glm::vec4 shipCell,
		float gameDeltaTime);

	// The afterimages, under alpha. Draw before the ship, so it sits on top of
	// its own ghosts. (The streaks go in drawGlow.)
	void drawAfterimages(wgpu2d::Renderer2D &renderer, wgpu2d::Texture shipSheet);

	// An enemy's sight (gameplay roadmap C5): a faint wedge from its nose, out
	// to its range and as wide as its cone, tinted by what it knows -- grey
	// unaware, amber searching, red engaged. **The caller must have set
	// `BlendMode::Additive`**; draw before the ships so the cones sit under
	// them.
	void drawSight(wgpu2d::Renderer2D &renderer, const Enemy &enemy);

	// A small diamond over an enemy that knows about the player: solid red
	// while engaged, pulsing amber while searching, nothing while unaware.
	// Under alpha. `time` drives the pulse.
	void drawAwareness(wgpu2d::Renderer2D &renderer, const Enemy &enemy, float time);

	// The world shaking: a ram's impact. `strength` 1 is one hit; hits add, up
	// to a limit. The world is shaken by moving the camera -- the HUD keeps its
	// own screen camera, so it stays still -- and `shakeOffset` is that
	// movement, advanced by `realDeltaTime`: wall time, like the HUD's shake,
	// so a slowed game still shakes at full speed.
	void shake(float strength);
	glm::vec2 shakeOffset(float realDeltaTime);

	void debugUi();
}
