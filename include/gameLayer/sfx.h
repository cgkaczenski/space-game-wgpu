#pragma once

// The game's sound effects: today, the shot.
//
// It owns the sound because it is the only thing that plays it (roadmap R10):
// an asset with one consumer belongs to that consumer. It also owns the rule
// that used to sit inline at the enemy's fire site -- that a volley of enemy
// shots does not stack into noise.

namespace sfx
{
	// Loads the sound. Never fails: a missing file means silence, not a game
	// that will not start.
	bool init();
	void cleanup();

	// Every player shot is heard.
	void playerShot();

	// An enemy shot is heard only if nothing is already playing.
	void enemyShot();

	void debugUi();
}
