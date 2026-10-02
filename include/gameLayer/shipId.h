#pragma once

// Who a ship is, for anything that has to point at one: who fired a bullet,
// who a missile chases (gameplay roadmap B1). An enemy's is its Enemy::id,
// counted up from 1 and never reused; the player has one reserved value; 0 is
// nobody.

using ShipId = unsigned int;

constexpr ShipId noShip = 0;
constexpr ShipId playerShip = ~0u;
