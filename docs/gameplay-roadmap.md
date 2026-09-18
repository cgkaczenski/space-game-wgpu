# Gameplay roadmap

The render track (`docs/roadmap.md`) built the engine. This file is for the
game: what the player does, why, and what stops them.

**How this file works.** The author drives the requirements here. Items start
loose on purpose and are refined together before any code, one milestone per
session, as `AGENTS.md` asks. Nothing below is decided unless it says so.

Two kinds of note appear under the items, and they are kept apart:

- **Open questions** — what has to be answered before building.
- **Engine ideas** — _suggestions only_, from what `wgpu2d` already makes
  cheap: per-quad effect parameters (F6), effects that read the world target
  (F2, the cloak), `LayerEffect` (the HUD shake), additive glow, render
  targets, compute. Take them or leave them.

The goal the items lead to is **a first playable level**. Combat comes first,
because a weapon or a shield can only be tuned against something the player is
trying to do — but the level is what gives it that.

---

## Now — combat

### C1. Energy: the shield and the cloak become gameplay

Today both are debug toggles with no cost. Wire them into play, possibly with a
second HUD bar (new textures, or the health bar's reused).

**Decided (first pass)**
- The shield is up by default, and energy is one blue bar under the health bar.
  It has its own art: the health bar hue-shifted, because a tint multiplies
  and a red fill tinted blue comes out near black.
- A hit on the shield is blocked, the bar drops to zero, and the shield breaks
  when that hit's ripple finishes. Hits during the ripple are blocked and ripple
  too, but do not delay the break.
- With the shield down, energy refills slowly (8 s, a debug slider) and the
  shield returns when it is full. A hit while it is down does damage and empties
  the bar again.
- **E** cloaks, and only with a full bar: energy to zero, shield breaks,
  thrusters off. The ship drifts on its momentum with no falloff and cannot
  thrust, be hit, or refill energy — the path is something to watch, not
  control — but the hull still turns, so a shot can be lined up. **Firing**
  uncloaks (the shot goes out), falloff returns, and the refill starts from
  empty.
- The shield breaks with a **dissolve**: the bubble burns away in patches with
  a bright edge, a computed-noise effect on its own quads.
- Health regeneration is a debug option, on by default.
- The camera starts as far out as the window allows.

**Still open**
- Will energy later power other things — weapons, a boost?
- Should a blocked hit shake the HUD? For now only damage does.

**Engine ideas**
- The shield already takes per-quad parameters. Energy can drive its rim
  brightness, and low energy can flicker the ripple, so the bubble itself shows
  how much is left before the HUD does.
- The cloak's refraction strength is one parameter. Scale it by energy and the
  cloak visibly thins as it runs out.
- The shield breaking could be a dissolve on the bubble: one threshold
  parameter plus a noise texture, nothing new in the renderer.
- A second HUD bar is what `hud.cpp` was shaped for, and the empty-energy
  moment can use the same `LayerEffect` shake as damage.

### C2. Multiple weapons with different behaviour

**Decided (first pass)**
- Four weapons, each with its own cooldown, held to auto-fire:
  1. **Burst laser** — two shots in quick succession, then the cooldown.
  2. **Heavy laser** — one larger shot doing triple a regular shot's damage.
  3. **Missile** — special ammunition, 5 for now (pickups later). Its flight
     is C3.
  4. **Laser** — a beam, C3; selectable in the meantime but does not fire.
- Keys **1–4** or the **wheel** (wrapping) select. An empty weapon can be
  selected but not fired. **Shift + wheel** zooms.
- A slot row centred along the bottom of the HUD: icon, cooldown shade,
  selected frame, ammo pips. No key numbers.
- Cooldowns longer than first proposed; all tunable in the debug panel.

**Still open**
- Do enemies get the same weapons?

**Engine ideas**
- Each weapon can have its own glow colour and capsule length for free; the
  bullet glow is one tinted texture.
- A charge shot can grow its glow while held. A beam is a stretched capsule.
- Weapons as data (fire rate, count, spread, speed, damage) with one fire
  function for player and enemies was sketched during R9.

### C3. Bullets that behave differently in flight

Homing, piercing, and so on — only if C2 wants them.

**Decided for C2's weapons**
- **Missile:** targets the enemy nearest the mouse when fired. It leaves at
  the player's speed, travelling parallel to the player, then steadily speeds
  up and homes. It cannot miss unless the target cloaks (enemies cannot yet).
  If the target dies first, it flies on in the same direction with no more
  homing, and can still hit something else.
- **Laser:** a beam across the screen that stops at the first thing it hits,
  doing constant damage while it touches. Blocked by shields, and cannot break
  one. Fires while held for up to 5 s, then cools down.
- Split in two: **C3a** the missile, **C3b** the laser.
- C3a, after the first playtest: the missile **faces the mouse** from the
  moment it fires and is **pushed sideways** out from alternating wings with
  its motor off (0.3 s, 700 u/s across the line of fire, keeping pace with the
  ship). Then the motor lights from zero and it **accelerates hard** — 14000
  u/s² to 6000 — while the slide fades, turning at 3 rad/s plus 6 rad/s for
  every second of chase. The exhaust brightens with its speed. With no target
  when fired, it keeps to where the player aimed. All debug sliders.
- C3b, the laser: **5 s of charge** that drains while the beam is on and is
  **kept when released**; only an empty charge starts the 4 s cooldown, whose
  end refills it. The HUD slot shows the charge draining. The beam is traced,
  not flown: it reaches the edge of the view unless an enemy's hitbox is in
  the way, and burns the first one at 0.4 life per second while it touches.
  Drawn as the glow capsule stretched along it plus the sheet's cyan beam
  segment tiled and scrolling, with a flickering burst where it hits. Firing
  it uncloaks. Shield blocking waits for enemies to have shields.
- Engine ideas taken: the beam as the capsule stretched, bloomed by the CRT
  glow, with the sheet's tileable beam segment available; the missile's
  exhaust as the ship's plume scaled down; a ripple ring on the locked target.

**Engine ideas**
- A motion kind on `Bullet` with a switch, not a class per bullet (R9).
- The glow already rotates to the bullet's heading, so a homing shot's curve
  reads with no extra work.

### C4. Hit feedback, enemy deaths, ship collision

Enemies currently vanish when killed, and ships pass through each other.

**Open questions**
- How should a kill feel — burst, dissolve, debris?
- Should ships collide with each other, and does ramming do damage?

**Engine ideas**
- A dissolve on death needs nothing new (see C1).
- The shield's ripple effect can run on an enemy hull to show where it was hit.
- A world-space shake, not just the HUD's, is the same `LayerEffect` pointed at
  the world.
- `collision::separation` exists and is tested; nothing calls it yet.
- Many flying fragments is where instancing (N5) starts to pay.

### C5. Cloak against enemy sight

Cloak can only matter once enemies can lose track of the player.

**Open questions**
- What can an enemy see: a range, a cone, line of sight?
- When the player cloaks, do enemies search the last known position?

---

## Next — a first playable level

### L1. Game states
Playing, paused, extracted, dead, restart. R10 defined what restart means.

### L2. Levels: placing resources and enemies
Hand-placed for now; eventually procedurally generated, so both should load the
same format.

**Known constraint:** enemies are despawned at 4000 units from the player, which
would delete a placed enemy before the player arrives. Levels have to change
that — and doing so also lifts the zoom-out floor.

### L3. Resources for extraction
For now, points earned.

**Engine ideas**
- Additive, pulsing glows read as valuable against the starfield.

### L4. Closing circle
Forces the player and enemies toward the centre.

**Engine ideas**
- Outside the circle, a full-screen effect over the world target (the cloak's
  path) can desaturate or distort the scene, so being outside is felt, not
  just shown.

### L5. Extraction warp gate

**Engine ideas**
- The gate can be a refraction effect like the cloak, swirled. The warp-out can
  be a final effect over the whole frame, the way the CRT is.

---

## Later

- **Enemy AI:** patrols, ambushing the player at resources, team attacks. Needs
  L2's places to exist.
- **In-game menu.** Also the natural home for the control scheme and settings
  that live in the debug panel today.
- **Procedural levels**, on L2's format.
- **Engine idea:** the GPU starfield (roadmap F5) as each level's backdrop.
