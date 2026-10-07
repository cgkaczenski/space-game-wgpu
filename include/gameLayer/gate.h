#pragma once

// The extraction gate (gameplay roadmap L5): the way out of a round.
//
//   Closed     until the closing circle reaches its final ring. Dim, still.
//   Open       hover inside it, uncloaked, for a few seconds to start it. An
//              arc round it fills while you do; leaving or cloaking empties it.
//   Charging   the gate spins up on its own, pulsing faster and faster as it
//              fills. The player can leave and fight meanwhile.
//   Ready      bright and steady: fly into it, uncloaked, and you are gone.
//
// With every enemy dead there is nothing left to wait out: the gate goes
// straight to Ready, whatever the closing circle is doing, and flying in
// extracts at once.
//
// An enemy shot that reaches the ship -- through the shield or not -- sends
// the gate back to Open from Charging or Ready, and the start has to be done
// again. The gate only reports that the player went through; what leaving
// means (banking the hold, the warp) is the game's, as it was for the debug
// button.
//
// Its look is four things: the black-hole art spinning at its centre, faster
// as it charges; the ring and arc here; the world swirling round it
// (a field in the cloak's pass, see cloak.h), and the HUD arrow pointing to it
// while it is off screen (hud.h).

#include <render/wgpu2d.h>

namespace gate
{
	enum class State { Closed, Open, Charging, Ready };

	// Loads the gate's art. Call once from initGame, after the renderer
	// exists. False if the texture failed to load.
	bool init();
	void cleanup();

	// A new round. `exists` false: this level has no gate, and nothing here
	// does anything.
	void start(bool exists, glm::vec2 position);

	bool exists();
	glm::vec2 position();
	float radius();
	// How far round the gate every field is cleared (sight roadmap W1):
	// level::fieldsAsPlayed's `gateClearing`.
	float clearingRadius();
	State state();

	// Game time. `open` is whether the gate may be used yet; `enemiesCleared`
	// makes it ready at once; `playerCanUse` whether the player is there to use
	// it -- present, in control, and not cloaked. True on the frame the player
	// enters a ready gate.
	bool update(float gameDeltaTime, bool open, bool enemiesCleared, glm::vec2 playerPos,
		bool playerCanUse);

	// An enemy shot reached the ship: whatever was started is lost.
	void playerShot();

	// 0 .. 1, the gate's pulse right now, for things that pulse with it (the
	// HUD arrow).
	float pulse();

	// Its colour now: grey closed, cyan open, whitening as it charges, gold
	// when ready.
	glm::vec3 colour();

	// How hard the world swirls round it, 0 .. 1, and the swirl field's reach
	// in world units. 0 while closed.
	float swirlStrength();
	float swirlRadius();

	// The black hole: the gate's body, drawn with ordinary alpha, under the
	// ships.
	void drawBody(wgpu2d::Renderer2D &renderer);

	// The ring and the arc. **The caller must have set `BlendMode::Additive`.**
	void draw(wgpu2d::Renderer2D &renderer, float zoom);

	void debugUi();
}
