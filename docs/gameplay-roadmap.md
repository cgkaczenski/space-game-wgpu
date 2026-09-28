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
- C3b, revised in L3: a part-used charge **trickles back** after a second
  without firing, refilling over 8 s. Keeping what was left and never
  restoring it worked while the beam was only a weapon; once it also mines,
  short burns left a laser that could only be restored by wasting the rest of
  it. Holding the trigger regains nothing and an empty charge still pays the
  4 s cooldown, so running dry is still the mistake. Both times are sliders.
- Engine ideas taken: the beam as the capsule stretched, bloomed by the CRT
  glow, with the sheet's tileable beam segment available; the missile's
  exhaust as the ship's plume scaled down; a ripple ring on the locked target.

**Engine ideas**
- A motion kind on `Bullet` with a switch, not a class per bullet (R9).
- The glow already rotates to the bullet's heading, so a homing shot's curve
  reads with no extra work.

### C4. Hit feedback, enemy deaths, ship collision

Enemies currently vanish when killed, and ships pass through each other.

**Decided — C4a, deaths:** a small explosion — a white-hot flash inside an
orange fireball that swells and fades over 1.1 s — and the enemy's own sprite
cut into six pieces that fly apart spinning, then slow, darken and **stay** as
a wreck field, still turning a little. 400 pieces are kept (about 66 kills);
past that the oldest fade out. Every kill goes through one function, whatever
did it. Tunable under Explosions in the debug panel. The missile's lock is a
**dashed red box** round its target, dashes marching.

**Decided — C4b, the ram:** Space. A 0.1 s **wind-up** first: the ship dips
back as the shield gathers into a **prow** — two thick white-hot bars meeting in
a point ahead of the nose, no blue glass — then the ship surges toward the mouse
at about 6000 for 0.8 s, past its normal top speed, then its momentum settings
take over. **Afterimages** of the ship fall behind it and **streaks** of light
rush past; the **camera leans** ahead and eases back. Ships still pass through
each other outside a ram. An enemy rammed takes 0.4 damage (each at most once
per ram; one ram can hit several), is knocked **aside** — 45° off the ram,
toward the side it was on, faster than the surge, so the ship passes it rather
than running it down — and spins, disabled — no moving, no firing — for 2 s.
On each strike the game **freezes for 60 ms** (hit-stop), the prow flares and
the world shakes; the player takes no damage. It works whatever the energy bar
says and spends none, blocks shots from the front while the prow is out, and
uncloaks. 8 s cooldown, shown as a fifth slot with a shield icon set apart to
the left of the weapon row. All of it on debug sliders. The world shake and
the lean move the camera rather than running the world through the HUD's
LayerEffect: the same result, since the HUD keeps its own screen camera,
without a second full-screen target.

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

**Decided (first pass):** enemies see along a **cone** from the nose —
rushers 90° wide out to 2500, snipers 60° out to 3500 — and hear anything
within 400 at any angle. Three states: **engaged** (sees the player; fights as
before, and only an engaged enemy fires), **searching** (lost sight: flies to
the last known position and scans, sweeping its cone, for 4 s), **unaware**
(never saw, or gave up: wanders in slow curves at part speed, until levels
bring patrols). Any hit — bullet, missile, laser, ram — engages an enemy and
turns it toward the player. Cloaked, the player is in no one's sight; cloaking
in front of an enemy sends it to search where the player vanished. Cones are
drawn as faint wedges — grey, amber, red by state — behind a **Vision cones**
debug toggle, on by default; a small diamond over each enemy shows red engaged
or pulsing amber searching.

**Open questions**
- What can an enemy see: a range, a cone, line of sight?
- When the player cloaks, do enemies search the last known position?

---

## Next — a first playable level

### L1. Game states
Playing, paused, extracted, dead, restart. R10 defined what restart means.

**Decided (first pass):** no words on screen — every state is shown by what
the world does, since `wgpu2d` has no text yet.
- **Paused:** **Escape** toggles it, and losing window focus pauses. Only
  Escape resumes — coming back to the window stays paused. Game time stops, so
  everything on it freezes mid-motion: ships, bullets, explosions, the beam,
  the shield ripple. The world goes **desaturated and dim**; the HUD stays
  crisp. The camera, zoom and debug panel keep working.
- **Dead:** at zero health the player's ship explodes the way enemies do (C4a,
  fireball and its own sprite as debris). The world runs on at normal speed
  with the controls off and the camera on the wreck; enemies lose the player as
  if cloaked. Then the **CRT collapses to static**, like a set switching off,
  and the game restarts on its own — no key.
- **Extracted:** the ship **warps out** — stretches and shoots forward along
  its heading with the ram's afterimages and streaks — the screen **fades to
  white**, and a new round starts. Until L5's gate exists, a debug button
  triggers it.
- **Restart** is R10's: the round's state goes, settings stay.

### L2. Levels: placing resources and enemies
Hand-placed for now; eventually procedurally generated, so both should load the
same format.

**Known constraint:** enemies are despawned at 4000 units from the player, which
would delete a placed enemy before the player arrives. Levels have to change
that — and doing so also lifts the zoom-out floor.

**Decided (first pass)**
- A level is a **circle** (radius set by the level, about 20000 for the
  first). Past the edge the ship is pushed back gently, and a faint ring shows
  where the edge is. L4's closing circle later shrinks this same radius.
- A level places **enemies** (position, rusher or sniper, facing — they start
  unaware), the **player start** (position and facing), **markers** for
  resources (L3) and the gate (L5), shown only in debug until those exist, and
  **scenery**: backdrop pieces, each with a parallax depth. Decoration, with no
  collision. _(After the first look:)_ the planets are background4.png's four,
  cut out and placed one at a time — that layer is gone from the starfield.
  The black-hole and shattered-planet art is too coarse up close and is only
  for small pieces far back. Depth 0 moves with the world, and reads as the
  furthest thing here, since the starfield's nearer layers move faster.
- With a level loaded, **waves are off** and nothing is deleted for distance.
  Enemies away from the view **sleep** — no update, staying where they are —
  and wake when they come within a margin of the view's edge, so everything on
  screen is alive and nothing wakes in sight. The zoom-out floor goes with the
  despawn ring. _(L2a, to confirm:)_ an enemy that is engaged or searching
  stays awake wherever it is, so a chase does not freeze just off screen.
- The format is a **plain text file**, one thing per line, read into a `Level`
  struct that a procedural generator will also fill later.
- An **in-game editor** in the debug panel: pick a type, click to place, drag
  to move, right-click to delete, with facing, radius and depth in panel
  fields. Editing freezes the game and frees the view to pan and zoom over the
  whole arena. Save writes the file; reload reads it back without restarting
  the game; "test from here" starts a round with the ship where the camera is.
- _(L2b, choices made while building — to confirm:)_ a right **click**
  deletes and a right **drag** pans, told apart by whether the mouse moves; the
  wheel zooms about the cursor, down to 0.01. Leaving the editor restarts the
  round from the level as edited, saved or not; the panel shows unsaved
  changes, and Reload throws them away. The start can be moved, not deleted.
  Scenery is picked and dragged where it is drawn, so a deep piece still moves
  under the cursor.
- The first level is sketched by Claude for the author to rework in the
  editor: the player near the edge, a few groups of rushers and snipers,
  markers for three resources and a gate, two or three planets.

### L3. Resources for extraction
For now, points earned.

**Decided (first pass)**
- Levels place **deposits** — `resource x y amount` — each worth what the
  level says. They are mined with the **laser beam and nothing else**: hold it
  on one and it drains into the hold. The beam's 5 s charge and 4 s cooldown
  mean a big deposit takes more than one pull, and firing uncloaks, so there is
  no mining unseen. A deposit **stops the beam** as an enemy does; ordinary
  shots pass through.
- A **hit interrupts** the drain for 0.6 s; what was taken stays taken.
- An emptied deposit is **destroyed the way a ship is** (C4a), through that
  same function: one blast and the rock's own sprite cut into pieces that fly
  apart, spin, slow and stay. Rubble is what marks a worked route.
- The **hold is unlimited**. Extracting turns it into points, which survive
  the next round. Dying **spills the hold at the wreck** as a deposit to be
  mined again.
- Ore does not appear in the hold. The beam knocks it off as **orbs** — one
  per 0.5 mined — thrown back toward the ship, which **hang still** where they
  stop. An orb within 2600 of the ship **comes for it**, chasing like a
  missile (C3a): accelerating, and turning harder the longer it chases.
  Further away it waits indefinitely, so ore mined from cover has to be
  collected afterwards.
- A killed enemy leaves the same orbs, worth very little each.
- A deposit is a **dim rock with a gold glow** pulsing over it, brighter while
  the beam is on it, and darkening as it empties. _(Tried and dropped:)_ the
  rock cut into a grid of chips that eroded from the beam's side, each orb one
  of those chips — it read worse than the plain glowing orbs, so the rock
  stays whole and breaks up once, at the end.
- The count is in the **debug panel** until there is a font to show it with.

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
