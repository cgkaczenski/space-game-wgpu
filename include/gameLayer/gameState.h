#pragma once

// Where a round is (gameplay roadmap L1). No words on screen: every state is
// shown by what the world does.
//
//   Playing     the controls are live.
//   Paused      Escape, or the window losing focus. Game time stops, so the
//               whole simulation freezes where it is; the world is drawn grey
//               and dim. Only Escape resumes.
//   Dying       health reached zero. The ship has exploded, the controls are
//               off, and the world runs on around the wreck. Then the picture
//               switches off like a CRT and the round restarts by itself.
//   Extracting  the ship warps out, the screen goes white, and the round
//               restarts. A debug button until the gate (L5) exists.
//
// This holds the rules and the timings. It does not draw and does not
// restart anything: `update` says when the game should, and the game owns the
// round. Timings are real time -- the pacing of a death is how it feels to
// watch, and should not stretch with the debug game-speed slider.

namespace gameState
{
	enum class State
	{
		Playing,
		Paused,
		Dying,
		Extracting,
	};

	struct Input
	{
		bool escapePressed = false;
		bool focused = true;
	};

	// Real time. Returns true on the frame the round should restart: the
	// screen is fully switched off, or fully white, so the cut is hidden. The
	// picture then comes back up over the new round on its own.
	bool update(float realDeltaTime, const Input &input);

	State current();

	// The game clock is held (gameClock::setPaused follows this).
	bool paused();

	// The player's controls act: Playing only.
	bool controlsLive();

	// The ship is there to be seen and hit: Playing or Paused. Dying, it is
	// wreckage; extracting, it is leaving.
	bool playerPresent();

	// From Playing. Starts the linger, then the switch-off.
	void playerDied();

	// From Playing. Starts the warp, then the white.
	void extract();

	// The warp's surge, from how far into it the ship is: its speed along its
	// heading, and how long the hull is drawn (1 = as normal). Both start
	// gently and run away, so it reads as the drive catching.
	float warpSpeed();
	float warpStretch();

	// 0 .. 1, eased: how far the world is into its paused look.
	float pauseLook();

	// 0 .. 1 each, for the CRT: how far the picture is switched off, and how
	// white it is. Rising into a restart, falling after it.
	float switchOff();
	float whiteOut();

	// Straight to Playing with no transition: the debug panel's reset, and
	// the first round. Leaves a pause alone.
	void reset();

	void debugUi();
}
