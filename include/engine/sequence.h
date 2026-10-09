#pragma once

// A sequence of steps, one at a time (hints roadmap H2): which step is
// current, how long it has been up, and moving on when it is finished.
//
// It does not know what a step is or what finishing one means. The caller
// says each frame whether the current step's condition holds, and whether
// the player skipped it; the sequence says what happened. A tutorial, a
// mission's objectives, a boss's phases would all drive one the same way.
//
// A finished step lingers for `finishSeconds` before the next one starts, so
// the player sees it go -- a tick, a flash -- instead of it vanishing the
// frame it is done. A skip does not linger: the player asked for it gone.

namespace sequence
{
	struct Sequence
	{
		int count = 0;
		int current = 0;          // == count once every step is done
		float inStep = 0.f;       // seconds since the current step began
		float finishing = -1.f;   // seconds left of a finished step's linger; < 0: not finishing
		bool entered = false;     // the current step's Entered has been reported
	};

	enum class Event
	{
		None,
		Entered,    // `current` has just begun: take what the step measures from now
		Finished,   // `current`'s condition held; it lingers, then the next begins
		Done,       // the last step has gone; reported once
	};

	void start(Sequence &s, int count);
	bool running(const Sequence &s);    // a step is current
	bool finishing(const Sequence &s);  // the current step is done and lingering

	// Advances by `dt`. `met`: the current step's condition holds this frame.
	// `skip`: move on now. One event per call, so a caller that acts on
	// Entered measures from the right frame.
	Event update(Sequence &s, float dt, bool met, bool skip, float finishSeconds);
}
