#pragma once

// A flat rigid body: something with a shape that moves and turns when pushed
// (gameplay roadmap A2). Plain maths, like movement and polygon -- nothing
// here knows it is a rock.
//
// **State:** where its centre of mass is, how fast that moves, which way it
// is turned, and how fast it turns. **Properties:** how hard it is to move
// (mass) and how hard to turn (moment of inertia), both kept inverted, since
// that is how every formula below uses them, and so something immovable is a
// plain 0 rather than an infinity.
//
// **A push at a point** does two things. Δv = J / m, whatever the point; and
// Δω = (r × J) / I, where r runs from the centre of mass to where the push
// lands. A push through the centre has r parallel to J and turns nothing; the
// same push at an edge also spins it. The 2D cross product is a single
// number: the z of the 3D one.
//
// **Force against impulse.** A shot is an impulse -- a change of momentum at
// once. A steady push (the beam) is a force, and a force for dt is the impulse
// force × dt, so it is applied the same way, every step.

#include <glm/vec2.hpp>
#include <vector>

namespace rigid
{
	struct Body
	{
		glm::vec2 position = {};   // the centre of mass, in the world
		glm::vec2 velocity = {};
		float angle = 0.f;         // radians
		float spin = 0.f;          // radians per second
		float inverseMass = 0.f;
		float inverseInertia = 0.f;
	};

	struct MassProperties
	{
		float area = 0.f;
		glm::vec2 centroid = {};   // the centre of mass, in the shape's own frame
		float mass = 0.f;
		float inertia = 0.f;       // about the centroid
	};

	// From a polygon drawn as a fan from `apex`: each triangle (apex,
	// outline[i], outline[i + 1]) is summed as a solid of `density` mass per
	// unit area. Every triangle's area is half a cross product, its centroid
	// the mean of its corners, and its second moment about the origin has a
	// closed form. The parallel axis theorem then moves the total from the
	// origin to the centre of mass.
	MassProperties fromFan(const std::vector<glm::vec2> &outline, glm::vec2 apex, float density);

	// A body of those properties at rest, its centre of mass at `position`.
	Body makeBody(const MassProperties &properties, glm::vec2 position, float angle = 0.f);

	// `impulse` at `worldPoint`: velocity and spin both change at once.
	void applyImpulse(Body &body, glm::vec2 worldPoint, glm::vec2 impulse);

	// A force for `deltaTime`: the same thing, as the impulse force × dt.
	void applyForce(Body &body, glm::vec2 worldPoint, glm::vec2 force, float deltaTime);

	// Moves it on by `deltaTime`. Semi-implicit Euler (velocity is already
	// changed; position moves with the new one). Damping is exponential, per
	// second, like movement's drag: with damping d, speed falls to 1/e in 1/d
	// seconds, whatever the frame rate.
	void integrate(Body &body, float deltaTime, float linearDamping, float angularDamping);

	// Two bodies treated as circles of radii `ra` and `rb`: if they overlap,
	// they are moved apart (the lighter one further) and, if closing, given
	// equal and opposite impulses along the line between them.
	// `restitution` is how much of the closing speed comes back: 0 dead, 1 a
	// perfect bounce. That impulse runs through both centres, so alone it spins
	// neither. `friction` adds the sideways part: at the point of contact, the
	// surfaces' sliding past each other -- spin included -- is resisted by an
	// impulse along the tangent, no bigger than friction × the bounce's. It is
	// applied at the contact point, so a glancing blow hands spin from one to
	// the other. Below `restingSpeed` of closing speed there is no bounce at
	// all: bodies pressed gently together (by a spring, say) would otherwise
	// bounce off each other every frame and buzz. True if they touched.
	bool collideCircles(Body &a, float ra, Body &b, float rb, float restitution,
		float friction = 0.f, float restingSpeed = 0.f);
}
