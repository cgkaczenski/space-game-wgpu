#pragma once

// Intent in, motion out: the one integrator the player and every enemy share
// (roadmap R8).
//
// **It does not know where the intent came from.** A direction of length 0..1
// is all it takes -- keys, an AI policy, a replay, a network packet. That is
// the rule that lets an enemy use the same movement as the player: enemies do
// not have keyboards. Turning keys into an intent, including thrust relative to
// facing, is the caller's business and happens before this.
//
// Plain numbers, like cameraFollow and cameraZoom: no wgpu2d types, no clock.
// The caller passes a delta on whichever clock it means -- for a ship, game
// time.

#include <glm/vec2.hpp>

namespace movement
{
	enum class Mode
	{
		// Velocity is the intent times maxSpeed, this frame. No inertia.
		Instant,

		// The intent accelerates; drag pulls velocity back toward zero. With
		// no intent the body coasts to a stop. Top speed is not a setting but
		// a consequence: acceleration / drag, where the two balance.
		Momentum,
	};

	struct Options
	{
		Mode mode = Mode::Instant;

		// Instant: the speed at full intent. Momentum: a hard cap, 0 for none.
		float maxSpeed = 0.f;

		// Momentum only. Units per second squared at full intent.
		float acceleration = 0.f;

		// Momentum only. Per second: with no intent, speed falls to 1/e of
		// itself in 1/drag seconds. 0 means no drag at all -- the body coasts
		// forever, and only maxSpeed bounds it.
		float drag = 0.f;
	};

	inline Options instant(float speed)
	{
		Options o;
		o.mode = Mode::Instant;
		o.maxSpeed = speed;
		return o;
	}

	inline Options momentum(float acceleration, float drag)
	{
		Options o;
		o.mode = Mode::Momentum;
		o.acceleration = acceleration;
		o.drag = drag;
		return o;
	}

	// The speed full intent settles at: maxSpeed for Instant, acceleration /
	// drag (capped by maxSpeed if set) for Momentum. 0 when unbounded.
	float topSpeed(const Options &options);

	// What a body is flying through: thick space that slows it, or nothing.
	// It scales the body's own top speed rather than replacing its Options,
	// so whatever set those -- tuning, a roll at spawn, a mode -- still
	// holds; the caller sets it each frame from wherever the body is.
	//
	// A body faster than the medium allows either snaps to the limit
	// (`settleHalfLife` 0) or loses the excess over time: it halves every
	// `settleHalfLife` seconds, so a fast body carries a little way in.
	// Thrust never takes it above what is left of the excess.
	//
	// A body with no top speed (Momentum with no drag and no cap) is
	// unbounded, and a medium cannot scale nothing: it flies on unchanged.
	struct Medium
	{
		float topSpeed = 1.f;        // of the body's own top speed
		float settleHalfLife = 0.f;  // seconds; 0 snaps
	};

	// `options` as they apply this step through `medium`, for a body moving
	// at `speed` now.
	Options through(const Options &options, const Medium &medium, float speed, float deltaTime);

	// Advances one body by `deltaTime`. An intent longer than 1 is shortened
	// to 1, so diagonal keys are not faster than straight ones.
	//
	// Momentum is integrated exactly rather than stepped, so the same second
	// of input lands in the same place at 240 fps or at 10.
	void integrate(glm::vec2 &position, glm::vec2 &velocity, glm::vec2 intent,
		const Options &options, float deltaTime);

	// ---- A body (gameplay roadmap P1) ------------------------------------
	//
	// Anything that flies itself is one of these, and `step` moves them all
	// the same way. Whatever decides where it goes -- keys, an AI policy --
	// only fills in an Intent. Facing and thrust are separate, so a body can
	// point one way and push another: strafe, back off, or fly where it looks.

	struct Body
	{
		glm::vec2 position = {};
		glm::vec2 velocity = {};
		glm::vec2 facing = {1.f, 0.f}; // unit: where the nose points
		Options move;                  // how thrust becomes motion
		float turnRate = 0.f;          // radians per second; 0 turns at once
		// For being pushed and bumping: the same blow moves a lighter body
		// more. Thrust is an acceleration, so mass does not change how a body
		// flies, only how it is knocked about.
		float mass = 1.f;
		// Whether it bumps into things at all. False, it passes through:
		// `collide` ignores any pair with a body that is not solid, and
		// anything else that pushes bodies about should ask this first. The
		// owner decides what makes its body intangible -- in this game, a
		// cloak.
		bool solid = true;
		// What `step` last pushed with, in the world's frame, length 0..1 --
		// for whatever draws an engine.
		glm::vec2 thrust = {};
		// What it is flying through, for `step`. Set by the caller.
		Medium medium;
	};

	struct Intent
	{
		// Where to point, any length. Zero keeps the current facing.
		glm::vec2 face = {};
		// Thrust in the world's frame, plus `forward` along the nose once it
		// has turned. Their sum is shortened to length 1 if longer.
		glm::vec2 thrust = {};
		float forward = 0.f;
	};

	// Turns toward `intent.face` -- by at most turnRate * deltaTime, or all
	// the way when turnRate is 0 -- then thrusts and moves through
	// `integrate`, with its Options through its medium.
	void step(Body &body, const Intent &intent, float deltaTime);

	// `facing` (unit) turned toward `want` (unit) by at most `maxRadians`,
	// the shorter way round.
	glm::vec2 turnToward(glm::vec2 facing, glm::vec2 want, float maxRadians);

	// A blow: `impulse` changes the velocity by impulse / mass, at once. A
	// blow of 1000 moves a body of mass 1 at 1000 more, one of mass 2 at 500.
	void push(Body &body, glm::vec2 impulse);

	// Two bodies as circles of radius `radiusA` and `radiusB`. If they
	// overlap, they are moved apart, the lighter further; if they are also
	// closing, they bump: the speed along the line between their centres is
	// exchanged as an equal and opposite impulse, so their total momentum is
	// kept, and `restitution` of the closing speed comes back as separating
	// speed (0 they stop together along that line, 1 a perfect bounce). Speed
	// across the line is untouched, so a glancing touch only deflects. If
	// either body is not `solid`, nothing happens: no contact.
	struct Contact
	{
		bool touched = false;
		// How fast they were closing along the line between them; 0 if they
		// touched without closing -- resting against each other. How hard the
		// bump was, for whoever decides what a bump costs.
		float impactSpeed = 0.f;
	};
	Contact collide(Body &a, float radiusA, Body &b, float radiusB, float restitution);
}
