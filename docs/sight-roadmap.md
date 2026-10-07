# Sight and space roadmap

Line of sight, a fog over what the player cannot see, levels that are mostly
asteroid field, and maps big enough to need fast ways across them. Work on
this happens on the `line-of-sight` branch.

**How this file works.** It works like `gameplay-roadmap.md`. The author sets
the requirements, items stay loose until the author and Claude refine them,
and each session does one milestone. What the author asked for is marked
**Asked**. Everything else is **Proposed** or a **Suggestion**, and none of
it is decided until it says so.

Each item has two kinds of note, kept apart:

- **Open questions:** what has to be answered before building.
- **Engine ideas:** suggestions based on what `wgpu2d` and `engine/` already
  make cheap.

Items carry a letter and a number: **S** for sight, **M** for the ship's
modes (fight and flight), **W** for the world (levels that are bigger and
mostly field, and getting across them). Numbers are never reused.

---

## What exists already

The sight work builds on code that is already in place. This is what it can
reuse:

| What                                  | Where                        | What it gives this work                                                             |
| ------------------------------------- | ---------------------------- | ----------------------------------------------------------------------------------- |
| Enemy sight (C5)                      | `enemyAi`                    | Cones, hearing, engaged / searching / unaware, and a last known position            |
| `blocksSight`, `raycast`, `hitCircle` | `asteroids`                  | Rock queries. **Each one loops over every rock.**                                   |
| The field "hidden" rule (A1b)         | `gameLayer.cpp`              | The player is hidden from every enemy while inside paint                            |
| Star-shaped polygons and fans         | `engine/polygon`             | A shape seen whole from one point is drawn as a fan from that point                 |
| Per-cell deterministic scatter        | `engine/scatter`             | Rocks from a hash of each cell, a bounds rectangle, `keepOut` circles, `density`    |
| A grade on the world                  | `worldGrade`                 | The world already goes into a target, and one quad brings it back through an effect |
| `renderTriangles`                     | `wgpu2d`                     | Any mesh, with a texture, UVs and a colour per vertex                               |
| Render targets                        | `wgpu2d::FrameBuffer`        | Keep their contents unless `clear()` is called, so drawing into one accumulates     |
| The outline effect                    | `outline`                    | Draws any sprite as a line around its shape                                         |
| Warp and speed looks                  | `gate`, `gameState`, `cloak` | The swirl field, the warp stretch, the ram's afterimages, the white fade            |
| Zoom easing                           | `engine/cameraZoom`          | The zoom eases toward a target, so it can follow speed                              |

---

## The rule, stated once

**Asked:** a ray goes out from the player in every direction, including
behind. Only asteroids and asteroid fields block it. When an enemy and the
player are both inside a field, they can see and shoot each other without the
field's rocks getting in the way. The field's core still blocks.

**Asked (refined): inside a field, vision inverts.** Inside a field, the
field is open space and its edge is the wall. Inside paint, you can't see
out. Outside, you can't see in.

**Replaced by M1:** field edges no longer decide anything for weapons. The
ship's mode does. The weapon paragraphs below are kept as history. Sight
still follows the rule.

**Asked: a weapon fired inside does not stop at the edge.** Bullets, missiles
and beams fired in the paint fly out through it. One fired outside still
stops when it reaches the field. Single rocks and cores always stop a
weapon. Inside paint, field rocks do not. **Both ways**, where the edge
stops a weapon leaving as well as entering, stays in the debug panel.

**Proposed as one rule, for sight.** A line from A to B is blocked by:

1. any single rock (A1);
2. any core;
3. **the edge of a painted area, crossed in either direction.**

The paint's edge is a wall from both sides. Inside the paint, the field's
rocks block nothing; outside it, there are no field rocks in the way. So two
points see each other only when they are on the same side of the edge, with
no single rock or core between them. What follows from that:

- **Two ships in the same continuous paint** see each other unless a core is
  between them. This is what the author asked for.
- **Inside paint, you can't see out. Outside, you can't see in.** Each side
  is a room, and the edge is the wall between them. A1b's tall grass becomes
  symmetric: a ship in a field is hidden from everything outside it, and
  everything outside is hidden from that ship.
- **Two fields that touch count as one.** A line that goes from one into the
  other never leaves paint, so nothing blocks it. Two fields with open space
  between them are different: sight is blocked where the line leaves the
  first. A shot flies out of the first and stops where it enters the second.
- **The fog (S3) inverts too.** In open space, the fields are grey; inside a
  field, the open space around it is grey.
- C5's cone, range and hearing still apply on top of this rule. The rule
  replaces only `blocksSight` and the `playerInField` shortcut.

**Why continuous paint and not "the same field":** a level that is 80% field
will be painted in many strokes, and a generator may make many fields. Two
fields that touch should not have an invisible wall between them.

**It is also what makes this cheap.** Inside a dense field, the only things
that block are cores and single rocks, which number in the tens rather than
the thousands. The edge itself is found with a grid lookup. Sight never needs
to test a field rock.

**Shots leave, they do not enter** _(replaced by M1)_. A shot stops where it crosses into paint.
One fired inside flies out: the edge does not stop it, and the field's rocks
do not either. Single rocks and cores always stop it. This holds for the
player's shots and for enemy shots alike.

- **Where a shot stops,** when it does: on the way in. The edge is the paint
  boundary, so a shot from outside can't slip in through a gap between the
  edge rocks. _Proposed_ for the look: if a field rock sits at the crossing,
  the shot strikes that rock, pushing it and hurting it as shots do today
  (A2, A4). If not, the shot bursts at the edge itself.
- **Missiles** follow the same rule. One fired inside can chase a target out
  of the field. One fired outside cannot chase a target in.

**Beams, the same rule** _(replaced by M1)_. A beam is not blocked by a field's rocks, and one
fired inside flies out through the edge. One fired outside stops at the
edge, and at a single rock or a core. This holds for the player's beam and
for enemy beams. A field rock is mined where an inbound beam meets it at
that edge, or once it has been knocked out of the paint and counts as a
single rock. The earlier recommendation, that the beam stop on every rock so
the interior could be mined, stays in the debug panel as **Mining tool**.

**Open questions.** Each of these is now a debug selection (see **The
options as debug selections** below). The questions are what each default
should be, and which option survives once they have been played.

- **Continuous paint, or the same field?** Continuous paint is recommended,
  for the reasons above.
- **The edge's thickness.** Is the wall the paint's exact edge, or a band a
  few hundred units deep, an "outer section" of rocks? A band would mean the
  outermost rocks block even for a ship inside, while the field's interior
  stays open. It would look more like a wall of rocks, but it is a second
  number to tune, and sight would need the mask to store distance to the edge.
  _Recommended:_ the exact edge first, and a band only if the edge looks too
  thin.
- **Rocks stop being pushed.** Shots fired inside a field no longer hit the
  field's rocks, and they fly out through the edge, so the field stays still
  during a fight inside it. A shot coming in from outside can still strike
  the edge. Is that fine?
- **Missile locks:** should a lock need sight? (See the suggestions.)
- **The player's outline in a field** currently means "no enemy can see
  you". Under the new rule it means "nothing outside can see you".
  _Suggestion:_ keep the mint outline, and turn it amber while an enemy
  inside the same paint has sight of you.

**Where it lands:** the rule is this game's, so it goes in a new
`gameLayer/sight`. The queries it needs are engine work (S1, S2).

---

## The options as debug selections

**Decided:** the author asked for each option to be a choice in the debug
panel, rather than settled in this file. That way the choices get compared
in play.

**How they're built.** Each choice is an enum in the module that owns it,
registered in that file's `tuning::Group`. Enums are kept as their int, so
the existing tuning system handles them. In the panel, each choice is a
radio group or a combo, wrapped in `tune::Highlight` so it turns amber when
it differs from the default. As R11 asks, each module draws its own section.

**Comparing them.** A named tuning set stores only what differs from the
defaults. So each variant can be saved as its own set, for example
`sight-rooms` or `sight-tallgrass`, and the picklist switches between them
during play. Applying a set restarts the round, which also rebuilds anything
grown at the start of a round, such as the mask.

**The defaults are the recommendations**, so a fresh build plays the proposed
rule. Each item below adds its own selections when it is built. A selection
that only makes sense after a later item does not appear before that item.

### Sight and shots: a new **Sight** section (S1)

| Selection       | Options                                                                                                                                                                                                              | Default         |
| --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- |
| Fields are      | **Continuous paint** (touching fields are one) · **Each field** (an edge between touching fields is a wall)                                                                                                          | Continuous      |
| Vision at edges | **Both ways** (rooms: the inverted rule) · **Into fields only** (the first proposal: inside, you see out) · **Rocks** (today: every rock blocks sight on its own outline; A1b's inside-is-hidden rule still applies) | Both ways       |
| Shots at edges _(goes away with M1)_ | **Into fields only** (a weapon fired inside flies out; one fired outside stops at the field) · **Both ways** · **Rocks** (today: every field rock stops every shot)                                                  | Into fields only |
| Wall            | **Edge** · **Band**, with a **Band depth** slider (100–1000 units) that shows only when Band is chosen                                                                                                               | Edge            |
| Shot stops _(goes away with M1)_ | **On an edge rock** (strikes the rock at the crossing if there is one, otherwise bursts at the edge) · **At the edge** (always bursts there, and never pushes rocks)                                                 | On an edge rock |
| Beam            | **Mining tool** (the first rock on its line; field edges do not stop it — fixed before S6) · **Like a shot** _(Rocks folded into Mining tool; goes away with M1: the mode decides)_ | Mining tool |
| Missile locks _(built with S2)_ | **Seen only** (locks only onto a ship its shooter sees; once locked, chases it seen or not) · **Any target** | Seen only |
| Hidden outline  | **Mint** (today) · **Mint, amber when seen** (when an enemy inside the same paint has sight of you)                                                                                                                  | Mint, amber     |
| Mask cell       | slider, 25–200 units                                                                                                                                                                                                 | 50              |

Vision and shots are separate selections on purpose, so the rule can be tried
on one before the other. The **Rocks** options exist so today's behaviour
stays one click away for comparison.

### What the player sees: in the **Sight** section (S2)

| Selection        | Options                                                                                                                     | Default     |
| ---------------- | --------------------------------------------------------------------------------------------------------------------------- | ----------- |
| Sight range      | **Fixed**, a range slider (500–20,000) _(View and Unlimited dropped)_ | 4000 |
| Slices           | slider, 180–2048                                                                                                            | 720         |
| Rock silhouettes | **Exact outline** · **Bounding circle** (cheaper, cruder; for measuring the difference)                                     | Exact       |
| Cloaked sight    | **Unchanged** · **Shorter**, with a multiplier slider                                                                       | Unchanged   |
| Show polar map   | checkbox: draws each slice's end point as a debug overlay                                                                   | off         |
| On a rock _(added)_ | **See out** (the rock under a ship does not block its own view) · **Blind** | See out |
| Missiles _(added; goes away with M1, which makes Cores only the fight rule)_ | **Cores only** (through every rock and edge; bursts on a core) · **Like a shot** | Cores only |

### The fog: a **Fog** section, beside the grade's (S3)

| Selection         | Options                                                                                      | Default |
| ----------------- | -------------------------------------------------------------------------------------------- | ------- |
| Fog               | **Off** · **Grey** · **Grey and dim** · **Black** (classic fog of war, for comparison)       | Grey    |
| Grey and dim      | a desaturation slider and a brightness slider, used by Grey and by Grey and dim              | to tune |
| Edge              | **Hard** · **Soft**, with a width slider                                                     | Soft    |
| Unseen enemies    | **Hidden** · **Greyed** (debug) _(Shown dropped: anything drawn in the world is graded)_ | Hidden  |
| Unseen explosions | **Greyed** · **Hidden**                                                                      | Greyed  |

### Ghosts: a **Last known** section (S4)

| Selection    | Options                                                                                               | Default       |
| ------------ | ----------------------------------------------------------------------------------------------------- | ------------- |
| Ghost lasts  | **Until checked** (gone once the spot is seen empty) · **Fades**, with a seconds slider · **Forever** | Until checked |
| Ageing       | **None** · **Fade** _(Dashed dropped: the outline shader draws a solid line)_ | Fade |
| Heading line | checkbox                                                                                              | on            |
| Colour       | colour picker                                                                                         | muted red     |

### Later items

These are listed now so the shape is known. Each one arrives with its item.

- **M1, fight and flight:** a new **Flight** section with two speed
  multipliers. It removes **Shots at edges**, **Shot stops**, **Edge rock
  reach**, **Beam**, and the missiles' row.

- **W2, interior movement:** **Normal** · **Speed cap**, with a multiplier
  slider · **Cap and drag**, with both sliders. Default: **Speed cap**.
- **W4, lane behaviour:** **Current** (pushes along the lane) · **Rail**
  (steers along the lane). Plus a strength slider and a top-speed multiplier.
- **W5, gate transit:** **Instant** · **Short transit**, with a seconds
  slider.

---

## Now: line of sight

### S1. One sight rule for everyone — _built, to playtest_

`asteroids::blocksSight` and the `playerInField` shortcut are replaced by
`sight::clear(from, to)`, which implements the rule above. Enemies use it
together with their cones (C5). Each frame, a shot walks the path it flew
since the last frame. If that path crosses into paint, the shot stops there.
A shot that starts inside and leaves the paint keeps going.

**Decided**

- **The grid is the truth.** `asteroids::inField`, sight, shots and beams all
  read the painted area's grid. `AsteroidField::contains` is used only to
  build it, and the editor's drawing.
- **The band is deferred** (S1c). The Wall selection waits until it exists.
- **The rock spatial hash is split out** (S1b). It changes no behaviour, and
  W1 is where it matters.
- _(Both weapon bullets below are replaced by M1: edges don't affect
  weapons, and the mode decides.)_
- **Beams follow the shot rule.** The default is **Like a shot**: inside
  paint, field rocks do not stop a beam. **Mining tool** stays selectable.
  Enemy beams use the same selection as the player's.
- **Weapons fired inside fly out.** The default for **Shots at edges** is
  **Into fields only**. Bullets, missiles and both beams share it. **Both
  ways** stays selectable. Vision stays **Both ways**.
- **A field rock knocked outside the paint acts as a single rock.** It is
  solid to sight, shots and beams until its spring brings it home. "Outside"
  is decided by the cell under its centre.

**What was built**

- **engine, `region::Mask`** (`engine/regionMask`):
  - `build` paints the stamps into a grid, layer by layer. Each stamp visits
    only the cells under its own square.
  - `labelAt` looks up a point.
  - `march` walks a segment through the grid (Amanatides–Woo), reporting
    every cell it touches in order, with the distance at which the segment
    entered it. A segment that starts outside the grid is clipped to it
    first.
- **game, `sight`**: the rule, and every S1 selection in a **Sight** section
  of the debug panel, registered with the tuning system. `clear` and
  `blockedAt` are for vision, `shot` for one frame of a shot's flight, and
  `beam` for both beams. Two debug views: **Show mask** (the grid's painted
  cells, coloured by field) and **Show sight lines** (green from each awake
  enemy to the player, red past the point where the line is blocked).
- **asteroids**:
  - owns and builds the mask when a round starts;
  - `hitCircle` and `raycast` take a `Which` (`All`, `Solid`, `InPaint`);
  - the cell size is a **Mask cell** slider among the field sliders, which
    rebuilds only the grid.
- **Bullet** carries `sweptFrom`, which is set to the shooter's centre when
  fired. A ship outside the paint can't fire in through a nose that pokes
  across the edge. A ship inside fires out.
- **outline** takes an optional colour, for the amber warning.

_(Choices made while building — to confirm:)_

- **Mask cell lives under Asteroids, not Sight.** The grid is the fields'
  area, and asteroids owns it (R11).
- A shot that crosses an edge strikes the nearest field rock in paint within
  its radius plus **Edge rock reach** (60 units, a Sight slider) of the
  crossing. With no rock there, it ends, as a shot hitting a rock always has.
  There is no burst effect yet.
- **Amber** means: the player is in paint, and an engaged enemy has a clear
  line to them this frame.
- **"Rocks" vision is today's rule**, with one change: the paint test is the
  grid, not the stamps.
- **Solid rocks are still tested only where a shot is now**, as before S1.
  Only the edge is swept.
- **Missile locks wait for S2**, since "seen" only means something once the
  player has sight of their own.

**Verified:**

- A temporary program, linked against `engine` and then deleted, ran on all
  three levels at cells of 25, 50 and 200.
- **The mask:** every cell agreed with `contains`, 125,000 cells in all.
- **`march`:** 18,000 random segments, including axis-aligned ones and ones
  starting outside the grid. Each matched an exact reference built from the
  closed-form grid-line crossings.
- **The game:** it ran with no validation errors.
- **Playing it** is the author's part: the rule is untested in play.

**Where it landed:** engine (`regionMask`); game (`sight`, plus the
`asteroids`, `outline` and `Bullet` changes, and the call sites in
`gameLayer.cpp`).

### S1b. A rock index — _built_

Every question about rocks used to loop over all of them. Now they look in
buckets.

**Decided:**

- One cell walk, shared between the mask and the buckets.
- A full rebuild, with slack added once the measurements called for it.
- Exact equivalence with the old loop.
- The bucket size is a slider.
- A temporary 80% level for measuring, then deleted.

**What was built**

- **engine, `gridWalk`:** the DDA, moved out of `region::march`, which is now
  a thin wrapper over it.
- **engine, `spatialGrid`:** circles in buckets.
  - Two flat arrays, rebuilt by a counting sort.
  - A stamp per item, so a query hands each item out once.
  - Box queries, and ray queries walking near to far.
  - Bounds come from the items, and the cells grow rather than the grid
    passing about 4 million.
- **asteroids:** one index serves `hitCircle`, `raycast`, `coreContact`,
  `shadowOn`, `blast`, `ram`, the bumping in `update`, and the drawing. The
  `unordered_map` hash in `update` is gone.
  - Each rock is listed under a circle round its centre of mass that holds
    both its outline (whatever its turn) and its bump circle, plus **slack**
    (100 units).
  - The buckets are rebuilt only when a rock has moved further than the slack,
    or rocks broke or were grown.
  - **Rock buckets** (400) and **Bucket slack** are sliders with the field
    sliders.

**Unchanged on purpose:**

- `hitCircle` returns the lowest-numbered rock touched, as the loop did.
- `raycast` gives an exact tie to the later rock, as the loop did.
- Drawing sorts what is visible, so overlapping rocks keep their order.

**One behaviour change:** in the bumping, each pair now meets once a frame,
in index order. The old hash could meet a pair once per cell they shared.

**Verified:**

- **Engine** (a temporary program, deleted afterwards):
  - `march` after the refactor matched S1's exact reference on all three
    levels;
  - the grid handed out every item a box or ray could touch, exactly once,
    near to far, on 20,000 random circles at three bucket sizes.
- **In the game** (a temporary brute-force mode, removed): every query and
  300 random ones a frame ran both ways. Mismatches were zero, including
  with rocks moving, breaking and being renumbered. Results on `level3` and
  on the 80% level (unoptimised, the default build):

| level | rocks | hit tests vs every rock | rebuild |
| --- | --- | --- | --- |
| `level3` | 1,558 | 6–21× faster | 0.19 ms |
| 80% field | **65,224** | 50–320× faster (`raycast` 1.85 ms → 0.006 ms) | 8.9 ms |

**What the measuring found:**

- **A rebuild is 8.9 ms unoptimised against 0.6 ms at -O2.** The game builds
  with no optimisation (an empty `CMAKE_BUILD_TYPE`), so the unoptimised
  number is the one played.
- **Slack cut the rebuilds by about two thirds** under a strength-2 blast
  every second. With nothing moving, the 80% level ran 30 s with no slow
  frames.
- **What is left for W1**, open: on a mostly-field level, a frame where many
  rocks fly is a rebuild. The ways to cut that:
  - build the `engine` target optimised even in the default build (a CMake
    change, and harder to step through);
  - re-list only the rocks that moved (a structure that can update one item);
  - stable rock ids, so a break does not renumber everything after it.

### S1c. _(Deferred)_ The wall as a band

The **Wall** selection, with a band depth: field rocks within the band are
solid even from inside, so the edge is a wall of rocks rather than a line.
For this, the mask needs each cell's distance to the edge, from a two-pass
distance transform.

### S2. What the player can see: a visibility polygon — _built_

**Asked:** a raycast in every direction from the player.

**Proposed: a polar map**, which is a one-dimensional shadow map. The circle
around the player is cut into N slices, for example 720 slices of half a
degree each. Each slice stores how far the player can see along it, starting
at the sight range. Two things fill it in:

- **Rocks:** each single rock or core in range writes its outline's edges
  into the slices they cover, and a slice keeps the nearest distance. The
  result is the rock's exact silhouette, not a circle drawn around it.
- **Paint:** each slice marches outward through the paint mask and stops at
  the first cell on the other side of the edge from the player: into paint
  from outside, or out of paint from inside.

The result is N distances, and that one structure has two uses:

1. **Rules:** "can the player see this point?" means comparing the point's
   distance with its slice's distance. Enemies, bullets and ghosts (S4) all
   ask this question, so the drawing and the rules share one answer.
2. **Drawing:** the end points of the slices form a polygon around the
   player. By construction, every point of it is visible from the player, so
   it is star-shaped from there. A triangle fan from the player draws it, the
   same way A1 draws a rock.

**Why not just cast 720 rays at the rocks?** That would work with the spatial
hash, but each ray would test the rocks along it. The polar map visits each
nearby rock once. In a mostly-field level, S1's rule leaves very few rocks
that block at all.

**Cost, as a guess to measure:** with a range of 4000 and 50-unit cells, the
paint march is 720 slices × 80 cells, about 58 thousand lookups. The rocks
are tens of outlines with about 28 edges each. This should come in well under
a millisecond.

**Open questions**

- **Sight range:** fixed (for example 4000), the size of the view, or a stat
  of the ship?
- Does the cloak change what the player can see?
- **Is the terrain fogged?** _Proposed:_ no. Rocks are always drawn, and grey
  outside the polygon. Grey means "not seen now", not "never seen". W6 covers
  "never seen".

**Engine idea:** a GPU version, in which a compute shader writes edges into a
storage buffer of slices with `atomicMin`. That is N4 territory and a good
exercise. But the rules need the answer on the CPU in the same frame, and
reading it back arrives a frame late (outline 15). So the CPU comes first,
and the GPU version only as practice.

**Decided**

- **Everything that blocks is a segment.** Field edges are the sides of the
  grid's cells that the vision rule says block, seen from the player. A line
  from the player crosses a side from the cell on the player's side of it, so
  which cell that is decides **Into fields only**. This replaces a march per
  slice, and it is one scan of the box round the player.
- **On a rock: See out.** A ship over a single rock looks out of it. The rock
  under it doesn't block its own view, though it still hides the ship and
  stops its shots. This applies to everyone's sight, enemies' included.
  **Blind** is the alternative.
- **Wedges** at shadow edges, one slice wide, are accepted for now.
- **Locks:** a missile, the player's or an enemy's, locks only onto a ship
  its shooter can see. Once locked, it chases that ship, seen or not. A
  cloak still breaks a lock, as it always has.
- **Missiles pass every rock and every edge, and burst on a field's core**
  (`effects::fireball`). That's a selection, **Missiles**: Cores only, or
  Like a shot.
- **Range is fixed and adjustable**: 4,000 by default, from 500 to 20,000 in
  the panel. View and Unlimited were dropped.

**What was built**

- **engine, `visibility`:** the polar map. `begin`, `addSegment` (one line
  intersection per slice the segment covers), `blockAll`, `corner`, and
  `sees`, which tests the triangle the fan draws there, so the rules and the
  drawing are the same shape.
- **game, `sight`:**
  - `updatePlayer` rebuilds the player's map once a frame, after the ship
    moves. `playerSees` and `playerMap` read it.
  - `blockedAt` gained See out.
  - `shot` takes a missile flag.
  - The panel gained a **What the player sees** block: Sight range, Slices,
    Rock silhouettes, Cloaked sight, On a rock, Show polar map, and the last
    build time.
- **asteroids:** `Which::Cores`; `raycast` can ignore one rock (the one
  underneath); `outlinesNear` hands out world-space outlines from the
  buckets.
- **weapons:** `nearestEnemy` takes an eligibility test.
- **effects:** `fireball`.

_(Choices made while building — to confirm:)_

- **The corner seal.** Each side segment reaches 1/100,000 of a cell past its
  corners. Without it, a line exactly through a corner where two walls meet
  could slip between them through rounding. A seal of 1/1,000 of a cell
  caught near-misses too, and blocked 100 times more slices than it should.
- **A missile's burst** is 0.6 × ship size × the missile's size.
- **An enemy's lock** follows the same sight that decides whether it is
  hidden from the player this frame: the S1 rule plus cloak.

**Verified** (temporary check, removed): from the player and from random
points over the fields, under all four combinations of Both ways or Into
fields only with Continuous or Each field, every slice of the map was
compared with `blockedAt` along that slice's centre line.

- **How many agree:** all but about 1.4 slices in a million. Every exception
  is a line passing exactly through a grid corner, where the walk counts a
  zero-length clip of the corner cell and the map does not.
- **Wall leaks:** none. The exceptions are not lines escaping through a wall.
  The two cases where the map saw further were a line going diagonally from
  paint into paint past a one-cell notch.
- **`sees`** matched the slice distances everywhere.
- **Build cost:** 0.22 ms on `level3` and 0.40 ms on a 65,224-rock field,
  unoptimised.
- **Playing it** is the author's part. Nothing visible changes yet except the
  **Show polar map** overlay, locks, and missiles passing rocks.

**Where it landed:** engine (`visibility`); game (`sight`, plus the
`asteroids`, `weapons` and `effects` changes and the call sites).

### S3. The fog: greying what can't be seen — _built_

**Asked:** grey out what the player's rays can't see.

**Proposed, building on what `worldGrade` already does.** `worldGrade` already
flushes the world into a target and records one quad that brings it back
through the grade effect. The fog adds one draw to that:

1. The full-screen quad comes back **greyed**. This is a third grade,
   "unseen", next to "paused" and "outside the circle", with its own
   desaturation and brightness.
2. Then the **S2 fan** is drawn with the same world texture and without the
   unseen grade. Each vertex's UV is its position on the screen divided by
   the screen size, worked out on the CPU from the camera, the way the cloak
   places its field. Inside the fan the world shows in colour; outside it,
   the grey quad shows.

That is `renderTriangles` with a texture and UVs, plus `setEffect`. **No new
library API**, and no extra full-screen copy, because the world is already in
that target.

- **A soft edge:** a second ring of triangles just past the fan's edge, with
  vertex alpha going from 1 to 0. Colours blend across a triangle, so a soft
  edge costs one ring of vertices.
- **The pause and outside-the-circle grades** apply on top, as they do now.
  The cloak pass comes after and bends the fogged world, as it does now.
- **Cost:** today the grade sends the world through a target only when the
  game is paused or the view crosses the circle. With fog, that happens every
  frame of play. Measure it at 2560×1440. The render-scale control exists if
  the extra target costs too much.

**Fog hides some things instead of greying them.** A grey enemy still tells
the player where it is. So enemies, their shots, beams, plumes, shields,
cones and awareness marks are **not drawn** in unseen space. Each one asks
S2's polar map. A hull is tested at its centre plus its radius, so an enemy
at the edge of the polygon appears as soon as its nose comes into view. The
part of it still in shadow comes out grey, which reads correctly.

- **Explosions and debris:** _proposed_ to show greyed, since that is how the
  player learns something died out there.
- **Enemy shots coming out of the fog** are drawn once they are in sight. A
  shot appearing out of the grey is a sniper's whole threat.

**Alternative:** pixel-exact hiding. Enemies would draw into their own target,
composited through the fan. That costs a target, and the layering is awkward,
because field rocks draw over ships. Not the first version.

**Open questions:** how grey, given the world has to stay readable? Does
unseen space also dim? Fog is off in the editor, presumably.

**Decided**

- **The fog follows the wreck while dying or extracting.** It is off in the
  level editor, and with **Fog** set to Off nothing is hidden for being out
  of sight either.
- **A missile's lock box** on an enemy the player cannot see is not drawn.
  The lock still holds.
- **An unseen enemy's beam** is drawn from where the player's sight first
  reaches it.
- **The look starts at Grey**: 85% desaturation, 70% brightness, a soft edge
  of 60 px.
- **The look's selections live under World grade** (a **Fog** block). The
  "unseen" rules live under Sight.

**What was built**

- **`worldGrade.wgsl`:** a third grade, unseen, in `d` (desaturation,
  brightness, how much).
- **`worldGrade::apply`** takes the player's polar map. The whole view comes
  back with the unseen grade, then the fan comes back without it, from the
  same target, in screen pixels. Each corner's texture coordinate is its
  pixel over the view size. The soft edge is a ring of triangles past the
  corners, its vertex colour going from (1,1,1,1) to (0,0,0,0), which scales
  the premultiplied colour.
- **The Fog block:** Off · Grey · Grey and dim · Black, sliders for the two
  greys, and Hard or Soft with a width.
- **`sight`:**
  - `playerSeesShip`: the centre, or eight points round the hull.
  - `hidesUnseenEnemies`: **Unseen enemies**, Hidden or Greyed (debug).
  - `explosionShown`: **Unseen explosions**, Greyed or Hidden.
  - `beamSeenFrom`: 64 samples along the beam.
- **`gameLayer`** works out once a frame which enemies are in sight. An
  enemy out of sight draws nothing: no hull, cone, plume, shield, awareness
  mark, lock box, burn flash, or cloak field (the world bending round a
  cloaked enemy would give it away). Enemy shots and beams follow the same
  rule. Ram afterimages and streaks follow the enemies' rule, and explosions
  and wrecks the explosions' rule (through a `Shown` test the `effects` draws
  now take).

_(Choices made while building — to confirm:)_

- **Unseen enemies has two options, not three.** Anything drawn in the world
  goes through the grade, so "Shown" (drawn and not greyed) would need a
  different layer. **Greyed** is the debug view.
- **The player's own ram afterimages** follow the same test. One behind a
  rock's shadow is not drawn.

**Verified**

- **Captures:** frames read back from the GPU at 2560×1440, with fog off,
  Grey with a hard edge, and Grey with a soft edge.
  - With the ship inside `level3`'s field, the field is in colour and
    everything outside the paint is grey.
  - The core casts a grey shadow.
  - The hard edge follows the grid's stair steps.
- **Alignment:** a 4× crop across the edge shows the nebula's pixel blocks
  and a rock straddling the edge continuous from grey into colour. There is
  no shifted or doubled copy, so the texture coordinates line up.
- **No pixel-by-pixel comparison.** Two runs of the same settings differ in
  12% of pixels (the shaders' time, the enemy), so frames from different runs
  cannot be compared.
- **Cost:** no slow frames in 20 s at 2560×1440, fog on or off. The adapter
  has no GPU timers, so that is a lower bound.
- **Hiding enemies has not been seen yet.** It wants playing against
  enemies; the one in `level3` was not in the captures.

**Where it landed:** game (`worldGrade` and its shader, `sight`, `effects`'
`Shown` tests, the draw loops). No library change.

### S3b. Looking out of a field — _built, to playtest_

**Asked:** from inside a field, the player and enemies see a cone outside it,
the way they look. Looking out is still reduced, but less than looking in
from outside.

**Decided**

- **The look-out cone.** Within the viewer's cone, a line that leaves the
  field is not stopped at the edge. It sees on past it for the reduction,
  and anything after that still blocks: another field's edge, a rock.
  Outside the cone the edge is a wall, and from outside looking in it is a
  wall as ever. This applies only with vision **Both ways**: **Into fields
  only** already sees out everywhere.
- **The player's cone follows the mouse aim**, 90° wide by default. An
  enemy's cone is its own sight cone (rushers 90°, snipers 60°).
- **The reduction first defaulted to a distance past the edge:** 1,500
  units. It is now **Arc round the ship** (see the second playtest below).
  The alternatives are selections:
  - **Peeking:** the full distance at the edge, less the deeper inside, none
    at the peek depth.
  - **Fraction of range:** the whole range cut to a fraction, measured from
    the viewer.
- **Full colour.** The looked-out area is drawn like any other sight; only
  its shorter reach shows it's reduced.
- **Shots are unchanged.**

**What was built**

- **`sight::Look`** holds the facing, half-angle and range.
- **`blockedAt` and `clear`** take an optional look. Inside the cone they walk
  with `edgeLookingOut`: the first crossing, if it leaves the paint, sees on
  for the reduction, and any crossing after it blocks.
- **The player's map** sends the sides a line leaves a field by into a second
  map. Slice by slice, it takes the leaving point plus the reach inside the
  cone, and the leaving point alone outside it.
- **Enemies'** sight of the player passes their own look. The debug sight
  lines carry each enemy's look, so they show the same rule.
- **Selections** in the Sight section, under **Looking out of a field**: Off ·
  Distance past edge · Peeking · Fraction of range, with **Past the edge**,
  **Peek depth**, **Looking-out range** and **Player's cone**.

**Verified** (temporary check, removed): from random points over `level3`'s
field, facing random ways with random cone widths, in each reduction mode,
the player's map was compared with `blockedAt` along every slice. They
disagreed on 8 of 6.9 million slices. That's the rate of S2's grid-corner
cases, though these were not inspected one by one. **Not yet seen in play.**

**A consequence worth watching:** shots fired inside a field already fly
out, so with sight out as well, a field is a place to ambush from, limited
by the cone and the reach.

**After the first playtest: the whole cone is seen.** Fog wedges showed in
the cone where it was "blocked by asteroids". There were two causes:

1. **The field coming back.** `level3`'s field has lobes and holes, so a line
   leaving one lobe soon enters another inside the reach, and the rule made
   that re-entry a wall. The fog began where that lobe's rocks began.
2. **Edge rocks counted as outside their field.** 10 of `level3`'s 1,557
   field rocks were, because the grid cell under them is on the unpainted
   side of the stair step. They counted as solid, cast shadows, and could
   stop a shot flying out.

**Decided:** within the cone, if a line's first crossing leaves the field,
nothing blocks it out to the reach: no rocks, no cores, no paint it comes back
into. The earlier rule is the selection **In the cone: Rocks and edges
block**. A field rock is out of its field when its own field's stamps say so,
checked as it moves, not by the grid cell under it.

**Verified:**

- The temporary check again, in both In the cone modes: the map and
  `blockedAt` disagreed on 8 of 7.3 million slices, the same corner cases.
- A 2560×1440 capture with the ship inside `level3`'s field shows the cone
  wedge in full colour out past the edge, with no fog in it. The core still
  shadows outside the cone.

**After the second playtest: the cone ends in an arc.** With "Distance past
edge" each line's reach was measured from where *it* left the field, so the
cone's far end was the field's ragged edge pushed outward. **Decided:** a new
default, **Arc round the ship**. In the cone, a line that leaves the field
sees out to a fixed radius from the ship (**Cone radius**, 2,500 by default),
so the far end is an arc whatever shape the field is. Deep inside a field,
with the edge beyond that radius, the cone shows nothing outside: looking out
means coming near the edge. Distance past edge, Peeking and Fraction of range
stay as selections. Arc is appended to the list, so a saved set's numbers
mean what they did.

**After the third playtest: jitter, seeing in, and the beam.**

- **Fog jitter.** The likeliest cause is the fixed slice rays crossing the
  grid's stair steps as the ship moves, and the cone's sides switching whole
  slices as the mouse moves. **Decided:**
  - The fog is drawn from an eased copy of the map: each slice eases to
    where it now is (**Fog smoothing**, 0.06 s, real time). The rules keep
    the exact map, so the drawing trails them by a few frames.
  - The cone turns toward the aim (**Cone turn**, 0.08 s). It eases by
    angle: blending the two directions, as first built, left a cone facing
    away from the aim stuck for many frames, and a capture caught it
    pointing the wrong way.
  - The cone's sides taper (**Cone softness**, 8°): a slice near a side gets
    part of the reach.
- **Seeing into a field (player only).** **Decided:** in the player's cone, a
  line whose first crossing enters a field sees on to the same reach. The
  field's own rocks cast shadows there, and it stops at the field's far edge,
  so destroying rocks opens sight deeper in. Outside the cone, and for
  enemies, the edge is a wall. **Looking into a field**: Off · Rocks block,
  to the reach. A field rock under the viewer is looked out of, as a solid
  one is (S2's See out).
- **The beam's mining was lost.** There were two causes:
  1. The beam's default was **Like a shot**, which passes field rocks inside
     the paint, so it mined nothing from inside a field, and from outside
     only a rock within 60 units of where it crossed the edge.
  2. The fog greyed the rock being mined.

  **Decided:**
  - The beam defaults to **Mining tool** again.
  - **Beam lights:** a circle of sight (300 units, a slider) round what the
    player's beam burns. `sight::reveal` holds it for the frame; the fog
    draws it in colour and the hiding rules count it as seen. Later items
    (a flare, a sensor ping) can reuse it.

**Verified** (temporary checks, removed):

- **Map against walk:** looks with and without seeing in were compared, from
  random points in and out of the fields, in every reduction mode.
  - A first run disagreed on 884 slices. The cause was a viewer standing
    over a field rock poking past the paint: the map saw its far side, the
    walk blocked at 0. Ignoring that rock, as See out does, brought it back
    to 10 of 6.9 million, the grid-corner cases.
- **Capture:** with the aim forced left for a capture, the cone is full colour
  out to its arc, with tapered sides.
- **Not captured:** seeing in, and the beam lighting what it burns. Both need
  the ship outside a field or firing, which a hidden run cannot do.

### S4. Last known positions — _built_

**Asked:** an outline of each enemy at its last known position.

**Proposed:**

- **A ghost:** when an enemy leaves sight, its hull stays where it was last
  seen, at the facing it had, drawn by `outline` in a muted red. It is drawn
  after the fog, so it is not greyed. `outline` is the module that already
  outlines the player in a field.
- **When the ghost goes:**
  - the enemy is seen again (the ghost is replaced by the live ship);
  - the player's sight covers the ghost's spot and the enemy is not there, so
    you looked and it was gone (S2 answers this);
  - after some time (open: never, or a 20-second fade).
- **Aging:** the ghost fades slowly, or its outline turns dashed after a few
  seconds, so an old position reads as old.
- _Optional:_ a short line from the ghost along the velocity it had, showing
  which way it was heading.

**Mechanism and policy.** The mechanism is a **contact memory**: for each id,
the last position, facing, velocity and time it was seen. It is updated from
the set of things seen each frame, and cleared by "looked here and found
nothing". It knows nothing about ships, so it goes in engine. C5's "last
known position" is the same mechanism pointed the other way, and the enemy
AI could use it too.

**Where it lands:** engine (contact memory); game (drawing the ghosts).

**Decided**

- **A ghost is armed before it can be cleared.** It clears only after its
  spot has been out of sight and comes back into sight empty. Cleared on
  "spot in sight", a ghost would go the frame after it is made, because its
  spot is where the enemy was last seen.
- **An enemy cloaking in sight leaves a ghost** where it vanished.
- **An enemy that dies out of sight keeps its ghost** until it is checked or
  fades. Removing it would tell the player something they could not know.
- **With the fog off there are no ghosts.**
- **"Dashed when old" is dropped.** The outline shader draws a solid line, and
  Fade says "old" as well.

**What was built**

- **engine, `contactMemory`:**
  - `observe` keeps each thing's last seen state, and leaves a ghost on the
    frame it goes from seen to unseen.
  - `check` arms ghosts whose spot is out of sight, clears armed ones back in
    sight, and forgets by age.
  - Nothing in it is about ships. S5 is the same memory pointed the other
    way.
- **game, `lastKnown`:**
  - The policy: lost means hidden by the fog or cloaked in sight.
  - Each enemy's sprite cell is kept, so its ghost outlives it.
  - Ghosts are drawn with `outline` in a muted red, with a heading line
    (velocity × 0.6 s), after the fog's grade so they keep their colour.
  - A **Last known** section: Ghost lasts (Until checked · Fades · Forever),
    Ageing (None · Fade, with Fades over and Faintest), Heading line and its
    reach, Ghost colour.

**Verified**

- **The memory** (a temporary unit test, removed): a never-seen thing leaves
  no ghost; a lost one leaves exactly one, at its last seen state; it stays
  while its spot is in sight unarmed; it is armed when the spot leaves sight
  and cleared when it comes back; Forever never clears; seen again removes
  it; forgetting by age.
- **The drawing** (a temporary override that counted `level3`'s enemy as lost
  after 60 frames, removed): a 2560×1440 capture shows the hull's red outline
  where it was at frame 60, over the field rocks, with its heading line
  pointing toward where the live enemy has gone.
- **Not yet seen in play** against an enemy really slipping out of sight.

**Added after S4: arrows to ghosts off screen.** **Asked:** for a ghost off
screen, an indicator like the warp gate's pointing toward it.

- **Built:** the HUD's gate chevron became one function (`drawChevron`), and
  the HUD took a list of off-screen markers for the frame
  (`hud::markOffScreen`). Each ghost off screen gets a chevron in the ghosts'
  colour, at 0.75 of the gate's size so the two do not read as the same
  thing. It fades as the ghost does. **Off-screen arrows** in Last known
  turns them off.
- **Verified:** with `level3`'s enemy counted as lost and its arrow's target
  pushed 6,000 units right (temporary, removed), a capture shows the red
  chevron at the right edge pointing out toward it.
- **Worth knowing:** a chevron near a corner can sit over the HUD's bars, as
  the gate's can.

**Where it landed:** engine (`contactMemory`); game (`lastKnown`, its calls in
`gameLayer.cpp`).

### S4b. The long-range scope — _built, to playtest_

**Asked:** hold V for a long-range scope, to make better use of S4.

- The ship comes quickly to a rest.
- The view zooms further out and leans off the ship toward the pointer: the
  nearer the screen's edge, the further.
- A narrow cone of full-colour sight opens along the aim.
- An enemy the scope sees leaves a ghost when V is let go, with the HUD's
  arrow pointing to it.

**Decided**

- **The ghosts come for free from S4.** The scope only makes sight reach far.
  Enemies in its cone are seen; when it lets go they drop out of sight, and
  S4 leaves a ghost at each one's spot. Those spots are out of sight then,
  so the ghosts are already armed, and off screen once the view is back, so
  the arrows point to them.
- **The cone follows sight's rules, only longer.** Rocks and field edges
  block, looking out of a field and into one included. Past an edge it
  reaches all the way to its range, not by the Looking out selection.
- **The all-round sight shrinks to a small circle** while scoped: looking far
  is a risk.
- **No firing and no ramming while scoped.** It is for finding.
- **The aim is measured from the ship while scoped,** because the view leans
  off it. Unscoped it is the screen's centre, as before.

**What was built**

- **engine, `cameraFollow`:**
  - `ease` closes a share of the way per moment rather than at a speed, so a
    big lean neither crawls nor snaps.
  - `pointerLead` is the pointer from the screen's centre as a share of half
    the screen, times a share of half the view. It is measured on the
    screen, so there is no feedback.
- **game, `scope`:** the policy and its sliders (a **Scope** section):
  - cone 20° wide, reaching 12,000;
  - sight round the ship 800 while scoped;
  - zoom 0.35× the normal;
  - lean up to 0.8 of half the view;
  - speed halving every 0.1 s;
  - 0.25 s in and out (smoothstep);
  - the view following at 8 per second.
- **`sight`:**
  - `updatePlayer` takes a `Scope`, which shrinks the all-round range and
    adds the cone.
  - The cone is a second polar map built over its own bounding box only
    (a few tens of thousands of grid cells, against hundreds of thousands
    for a full circle that far). It is merged into the player's map by the
    further distance per slice, blended at the cone's soft sides and by how
    far in the scope is. Two shapes seen from one point make one still all
    visible from it, so the fog, `playerSees`, hiding and the ghosts need no
    change.
  - `reachPast` now takes the look, with `reachToRange` for the scope.
  - `asteroids::outlinesIn` gives a box's rocks.
- **gameLayer:**
  - V is read before movement.
  - The aim is measured from the ship's screen position while held.
  - The ship drifts (no thrust) and brakes.
  - The trigger and the ram are off.
  - The view eases toward ship + lean while scoped and on its way back;
    otherwise the chase, leash and all, is unchanged.
  - The zoom is scaled by the scope's.

**Verified**

- **The scope's map against `blockedAt`** (temporary check, removed): from
  random points, facing random ways, along every slice in the cone, they
  disagreed on 3 of 672,000. Its rays are three times longer than normal
  sight's and cross more grid corners; these three were not inspected.
- **A 2560×1440 capture with the scope held** (the pointer forced to the
  left, temporary, removed) shows:
  - the view zoomed out and leaning left, with the ship off to the right;
  - the narrow cone full colour 12,000 units out of the field;
  - the sight round the ship shrunk to a small patch;
  - the enemy's ghost left beside the ship, because the shrinking sight lost
    it.

**Not yet felt in play:** the braking, the lean as the pointer moves, and
letting go.

**After the first playtest: a periscope.** **Asked:** slower, like a
periscope: the view lags the pointer, the zoom goes further out the nearer
the pointer is to the edge, the cone reaches further and narrows the further
out the player looks, and down-scoping still leaves ghosts and arrows.

- **Built:**
  - The scope eases its own copy of the pointer (**Follows the pointer**,
    0.5 s), and that "how far out" drives three things between the screen's
    centre and its edge:
    - the zoom, 0.5× to 0.2×;
    - the cone's width, 30° to 10°;
    - its range, 6,000 to 16,000.
  - The cone swings toward the aim (**Turns to the aim**, 0.6 s, by angle),
    and the view follows its lean at 2 per second instead of 8.
  - Easing in and out takes 0.4 s.
- **Down-scoping already leaves ghosts.** A scripted run (temporary,
  removed) moved `level3`'s enemy 9,000 units right, beyond normal sight,
  held the scope pointed right from frame 30 to 180, then let go:
  - the enemy was seen from frame 120, once the cone had stretched out to
    it;
  - it dropped out of sight on letting go, leaving a ghost;
  - the capture at frame 300 shows the ghost's chevron at the right edge.
- **Read as the pointer, not as the cone's shape:** "narrows the further the
  player looks" is built as the pointer's distance setting a longer,
  narrower cone. A cone that is itself wide near the ship and narrow far out
  (a spearhead) is the other reading, and would be a few lines in the merge.

### S5. What enemies think, shown — _built_

_Splinter Cell: Conviction_ draws a ghost of the player where the enemies
believe the player to be. Searching enemies here already fly to the player's
last known position (C5). Drawing a faint outline of the player's own ship
there mirrors S4: it shows where they will search, and it makes slipping away
into a field something the player can read.

**Decided**

- **The enemies' own memory, not `contactMemory`.** A searching enemy already
  holds `lastKnown`, the spot it flies to and scans. Engaged, it knows where
  the player is; unaware, it has given up. So a ghost is drawn exactly for
  each searching enemy, and a second store of the same thing would be a
  second truth.
- **Every searching enemy's belief**, as _Conviction_ does: the player learns
  they were seen, even by an enemy they never saw. "Enemies you know of"
  (seen, or holding an S4 ghost) is the stricter selection.
- **Facing as it was** when the player was lost. `Enemy::lastKnownFacing` is
  set where `lastKnown` is; `alert()`, a hit that gives the player's
  position, keeps the old facing.
- **No off-screen arrow** for now. The ghost is usually near where the player
  just was, and the red arrows mean "enemy".

**What was built**

- **game, `theirGhost`:**
  - searching enemies' beliefs, merged within 300 units;
  - stronger for each extra enemy behind one: 0.45 plus 0.15 per extra;
  - drawn as the player's hull with `outline` in a pale blue-white, after
    the fog;
  - a **Their ghost** section: Show (Off · Every searching enemy · Enemies
    you know of), merge distance, strength, per-extra, colour.
- **`lastKnown::remembers(id)`** for the stricter option.
- **`enemyAi`** records the player's facing with its position.

**Verified:** with `level3`'s enemy forced into Searching, believing the
player 600 units right and facing up (temporary, removed), a 2560×1440
capture shows the pale outline of the player's hull there, facing up.
**Not yet seen in play** with an enemy really losing the player.

### Fix before S6: mining from outside a field

**Reported:** from outside a field the beam would not mine; it stopped at the
field's edge.

**Causes found:**

1. The beam's default, **Mining tool**, stopped at every rock but also took
   the shots' edge rule. Shots are Into fields only, so from outside the
   beam stopped where it entered the paint, and the paint's stair-step edge
   lies in front of the rocks inside it.
2. At that edge it burned a field rock within 60 units of the crossing: one
   *beside* the line, not the rock aimed at.

A probe (temporary, removed) fired a beam from outside into `level3`'s field:
the edge was at 800, the first rock on the line at 1,268, and the beam
stopped at 800, burning another rock.

**Fixed:** as a mining tool the beam stops at the first rock on its line, in
a field or out; field edges do not stop it. The same probe now stops at
1,268, burning the rock on the line. "Rocks" behaves the same as Mining tool
now and is folded into it (a saved set that chose it still works); "Like a
shot" remains.

**A consequence:** a beam fired from outside can reach an enemy inside a field
through a gap between rocks, and an enemy's beam the player. Field rocks are
dense, so this should be rare.

### S6. Cones that stop at rocks — _built_

C5 drew each cone as a wedge that passed through everything: one pre-made
45° sector texture, stretched. Now each cone is a polar map computed from the
enemy, over its cone's bounds and only for enemies drawn, so it takes the
shape of what the enemy can actually see. That is the stealth-game readout of
_Mark of the Ninja_ or _Commandos_.

**Decided**

- **The cone is the rule.** It is built with the same look that
  `sight::clear` uses when deciding whether the enemy sees the player: cut by
  single rocks and cores, stopped at field edges, and, from inside a field,
  seeing out of it within its cone (S3b). What is drawn never disagrees with
  when the enemy spots the player.
- **A flat fill with a brighter rim** along the outline, the side rays
  included, in the awareness colours: grey unaware, amber searching, red
  engaged.
- **No hearing ring**: it would be clutter.

**What was built**

- **`sight::coneView`:** one viewer's map over its cone's box. The box helper
  is shared with the scope's.
- **`effects::drawSight`:** takes that map, and draws a fan through the slice
  ends inside the cone plus two exact side rays, as a flat fill, with the rim
  as lines. The sector texture is gone; a white pixel replaces it.
- **Sliders**, under Explosions: **Cone fill** (0.5), **Cone rim** (1.0,
  raised from 0.6 after a capture showed an unaware rim barely visible),
  **Cone rim width** (1.5 px).
- **`gameLayer`** builds each drawn enemy's view (not cloaked, not stunned, in
  the player's sight) and hands it over.

**Verified**

- **The map against the walk** (temporary check, removed): enemy-like looks,
  60° to 90° wide, 2,500 to 3,500 long, from random points facing random
  ways. They disagreed on 3 of 2.16 million cone slices, the corner cases.
- **A 2560×1440 capture** shows `level3`'s unaware enemy's cone as a stepped
  grey outline where its sight is cut, from inside its field.
- **Not yet seen in play** with an enemy turning past single rocks.

---

## Next: fight and flight

### M1. Fight and flight — _built, to playtest_

**Asked:** Tab switches the player's ship between two modes.

- **Fight:** shield up, normal speed. The beam does not hit rocks.
- **Flight:** faster, with no shield. The beam hits rocks.
- **Enemies stay in fight mode** for now. That changes with the W items.

**Why:** running away should be a real option. The rules for weapons and
rocks should also be simple. Today they are four debug selections (**Shots
at edges**, **Shot stops**, **Beam**, and the missiles' row), with a fix on
top (**Fix before S6**), all built on field edges. No single sentence says
when a shot stops or when the beam mines. With M1, the mode is the rule, and
field edges are only about sight.

**Decided**

- **Field edges never affect weapons.** Shots, missiles and beams don't
  test the paint at all: no edge stops them, in either direction, and no
  edge rock is struck. Edges are only for sight.
- **In fight mode, weapons go through rocks.** Shots, missiles and the beam
  pass through every rock, single rocks and field rocks alike, and stop only
  at a core. They don't push or burn rocks on the way. In a fight, every
  weapon follows one rule: _a core stops it, nothing else does._
- **In flight mode, only the beam changes.** Guns and missiles follow the
  fight rule. The beam stops at the first rock on its line, in a field or
  out, and burns it (today's **Mining tool**).
- **The flight beam still hurts ships.** Flight mode is fight mode with more
  speed, no shield, and a beam that stops at rocks.
- **The rule in one sentence:** _weapons stop only at cores, except the
  flight-mode beam, which stops at the first rock and mines it._
- **Switching to fight mode starts the shield empty.** Energy goes to Down,
  and the shield comes back when the bar refills, as it does after a break.
  Tab can't raise a shield just before a hit lands.
- **Switching to flight mode keeps the bar.** The shield's break plays, and
  the bar keeps its level. From a full bar, the player can cloak straight
  away.
- **Flight speed is two multipliers:** one on top speed and one on
  acceleration, each a debug slider.
- **The cloak and the ram work in flight mode.** With no shield, the bar
  still fills, so the cloak can be used when the bar is full. The ram works
  at flight speed.
- **Showing the mode:**
  - **A HUD slot:** the shield's rim icon (`shield::drawIcon`, as in the
    ram's slot), lit in fight mode and dimmed in flight mode.
  - **The switch:** going to flight mode plays the shield's break. Going to
    fight mode starts the bar empty, so the shield eases back in as it
    refills.
  - **A beam colour for each mode**, so the player can see whether the beam
    will stop at a rock.

**What this means:**

- **Rocks are cover from sight, not from fire.** Enemies are in fight mode,
  so their shots and beams pass through rocks too. Sight is unchanged:
  rocks and field edges still hide the player. An enemy that can't see the
  player doesn't aim at the player, so hiding still works. Staying behind a
  rock in plain view doesn't. Only a core is a real wall.
- **Mining is a flight-mode activity.** To mine, the player drops the
  shield.
- **A fight leaves the rocks alone.** Shots stop pushing rocks around (A2)
  and wearing them down (A4) during fights. Flight mode's beam still does
  both.
- **A shot from outside can hit a ship inside a field**, and the other way
  round, as long as no core is in the way. To aim, though, the shooter has
  to see the target, and the edge still blocks sight.
- **Several debug selections go away:** **Shots at edges**, **Shot stops**,
  **Edge rock reach**, **Beam**, and the missiles' row. The mode replaces
  all of them. Weapon code no longer reads the mask.

**What exists already:**

- **A ship with energy and no shield** is a state `energy` already supports
  (B1). The bar fills and empties by the same rules, and nothing blocks.
  Flight mode is the player's `Energy` with `hasShield` off, switched during
  play. Fight mode's empty shield is energy's Down state.
- **Lowering a visible shield breaks it.** It dissolves in patches with a
  burning edge. Raising a shield eases it in (`shield::setActive`), so the
  switch already has a look.
- **Speed is `movement::Options`** (`maxSpeed`, `acceleration`, drag). The
  player's momentum top speed is 2000. Flight mode multiplies two of those
  fields. W2 proposes the same for field interiors, and W4 for lanes.
- **Tab** is already read by the platform (`Button::Tab`), and the game does
  not use it yet. The game should ignore it while ImGui has the keyboard,
  because ImGui uses Tab to move between its fields.
- **Missiles' Cores only** (S2) is already the fight-mode rule, written once
  for one weapon. Shots and the beam can use the same test.

**Open questions:** none left before building. Tuning the two speed
sliders is a playtest.

**What was built**

- **game, `shipMode`:** the mode, Tab's toggle, the two flight multipliers
  (`flight.topSpeed` 1.6×, `flight.acceleration` 1.5×) and a **Flight**
  debug section with a Switch button. Each ship keeps its own mode: the
  player's is in the session (a new round starts in fight mode), and each
  enemy's is on its `Enemy`, always fight mode for now.
- **`energy::setShield`:** taking the shield away drops it and keeps the
  bar. Giving it back empties the bar into Down, and uncloaks if cloaked.
- **`sight::shot(at, radius)`** stops only at a core. **`sight::beam`**
  takes `mines`: all rocks when mining, otherwise cores only. Weapons no
  longer read the mask, and `Bullet::sweptFrom` only feeds shot noticing.
  The **Shots at edges**, **Shot stops**, **Edge rock reach**, **Beam** and
  **Missiles** selections are gone. Saved sets that name them have those
  keys skipped.
- **`playerMove::update`** takes the mode and applies
  `shipMode::movementFor` to the tuned options.
- **HUD:** a mode slot to the right of the weapon row (`hud::showMode`). It
  shows the shield icon in fight mode. In flight mode the icon is dimmed,
  with two cyan chevrons over it.
- **Beam colour:** the player's beam is amber in flight mode
  (`bulletLook`'s `mining`), and cyan as before in fight mode.
- **Verified:** it builds and runs with no validation errors. It has not
  been played.

**Debug selections**, in a new **Flight** section:

| Selection            | Options                   | Default |
| -------------------- | ------------------------- | ------- |
| Flight top speed     | multiplier slider, 1–3×   | to tune |
| Flight acceleration  | multiplier slider, 1–3×   | to tune |

The rest is decided, so it isn't a selection.

**Engine ideas** (suggestions, not in M1):

- **Speed looks:** the warp stretch and the ram's afterimages on the ship,
  and `camera::Zoom` easing out as speed rises. W4 lists the same ideas for
  lanes, so flight mode would try them first.
- **Shots through rocks** could show it: a short spark on the rock's far
  side as a shot passes through, so going through reads as deliberate and
  not a missed collision.

**Later, with the W items:** enemies switch modes too. They can use flight
mode to cross a lane (W4), to flee, or to chase, and fight mode inside
fields (W2). An enemy in flight mode has no shield, which gives the player
an opening. For that, the mode is kept on each ship, as `Energy` is: the
player's in the session, and each enemy's on its `Enemy`.

**Where it lands:** the game. A new `gameLayer` module holds the mode, its
tuning and the switch rule, kept on each ship as `energy` is. The weapon
rule goes in `gameLayer/sight` beside `sight::beam`, which takes the mode.
Nothing in `render/` or `engine/` changes.

---

## Next: levels that are mostly field

### W1. Levels that are 80% asteroid field

**Asked:** levels where about 80% of the area is asteroid field, as
preparation for procedural generation.

**What breaks at that size today:**

- **Rock count: measured at 65,224** (S1b). That's one stamp covering 80% of
  a 20000-radius arena, at `level3`'s density (max size 90, gap 40). Each rock
  has its own outline, mesh and body.
- ~~**Every hit test loops over every rock.**~~ S1b's buckets fixed it: 50 to
  320 times faster at that count. What remains is the rebuild while rocks
  fly (see S1b).
- ~~**`inField` checks every stamp of every field.**~~ S1's mask fixed it.
- **Painting 80% of a level with a brush is tedious.**

**Proposed steps:**

1. **The spatial hash and the mask**, both from S1. They come first.
2. **Paint the clearings, not the field.** An editor action fills the arena
   with one huge paint stamp, and the Shift-erase brush then carves out
   clearings and lanes. The file format already supports this, because paint
   and erase stamps are applied in order. Nothing changes in the file.
3. **Rocks only near the camera.** `engine/scatter` was built for this. Each
   rock comes from its own cell's hash, and `scatter()` takes a bounds
   rectangle. So rocks can be made for the chunks near the view, dropped when
   the view moves away, and made again identically when it comes back.
   - **What doesn't come back on its own:** rocks that were broken or mined
     (A4). A per-chunk record of which rock seeds are gone handles that.
     Rocks that were only moved spring home anyway (A2).
   - **A caveat:** each layer checks its gaps against nearby rocks from
     coarser layers. A chunk therefore has to be scattered with a margin,
     keeping only the rocks that land inside it. Test that a chunk made alone
     comes out the same as that chunk made together with its neighbours.
4. **Cores:** each field has one core, at its middle, so one huge field would
   have a single core. _Suggestion:_ let the level place several cores in a
   field, or let density place them.

**Open questions:** 80% of what: the arena circle? How big are the clearings?
Do the clearings hold the resources and the gate? Does a huge field need
several cores?

**Where it lands:** engine (chunked scatter, the region mask); game (what
persists); the editor (the fill action).

### W2. The interior is slow

**Asked:** inside the fields, ships move slower and there is more combat.

**Proposed:** paint carries a movement modifier. In paint, a ship's top speed
is capped lower and its drag is higher; in the open it moves normally; in a
lane (W4) it moves faster. `movement::Options` (mode, `maxSpeed`, drag) is
already the setting for this, and the game picks a body's Options each frame
from the mask. Enemies get the same treatment, so a chase into a field slows
both sides.

**Open questions:** a speed cap only, or drag as well, so momentum carries a
ship a little way in? Does the ram still work inside? (Bullets keep their
speed.)

**Engine idea:** rock shadows already darken ships (A3's `shadowOn`), and a
foreground layer of debris already exists (`drawForeground`). Making the
debris denser over fields would help sell "the interior".

---

## Then: bigger maps, and getting across them

### W3. Bigger arenas

**What already scales:** the arena radius is a single number, the closing
circle uses rings, and enemies away from the view sleep (L2).

**What doesn't:**

- the rocks (W1);
- the HUD, which points only to the gate;
- float precision. At 100 000 units from the origin, a float resolves about
  0.008 of a unit, which is fine. At a million it starts to show, because
  vertices are sent in world units and the camera is a matrix on the GPU.

**Open question:** how big is the target: 2×, or 5×?

### W4. High-speed lanes

**Asked:** high-speed lanes or gates, so the player can cross quickly between
areas.

**Proposed shape:** a lane is a polyline with a width, stored in the level
file as a `lane width speed` line followed by `point x y` lines, the way a
field is followed by its stamps.

- **A current** along the lane pushes ships forward: an acceleration along
  the lane's direction, added in the integrator, with a higher top speed. A
  ship that enters at an angle is turned into the current.
- **Lanes are open space, cut out of the fields.** Scatter's `keepOut`
  circles, which exist already for cores, are placed along the lane, and the
  mask marks lane cells as not paint. Under S1's rule a lane is a corridor:
  its walls are paint edges, so ships in the lane and ships in the field
  beside it can't see each other. Along a straight stretch, though, a lane
  can be seen from end to end. **Fast but exposed to whoever else is in the
  lane, against slow and close in the fields:** to ambush the lane, you wait
  in it, at a bend or an exit. That comes straight out of the rule, with no
  extra code.
- **Leaving a lane** throws the ship out at speed. Momentum carries it on,
  and W2's interior drag slows it again.

**Engine ideas:**

- streaks along the lane, made with the generated-gradient pattern the plume
  and the bullet glow use, drawn additive and scrolled;
- the warp stretch and the ram's afterimages on the ship at lane speed;
- the camera zooming out with speed, since `camera::Zoom` eases toward a
  target;
- FinalGlow blooming the streaks;
- a chromatic flash on entering, which is an F2 effect.

**Open questions:** does the lane steer the ship like a rail, or only push it
like a current? Can ships fight in a lane? Do enemies use lanes? Do lanes run
both ways?

### W5. Gates: jumping between areas

This can replace lanes or sit alongside them: paired gates, where flying into
one brings the ship out of the other. The extraction gate (L5) already has
the look: the spinning black hole, the swirl in the cloak's pass, the warp
stretch, and the white fade from L1's "extracted". A jump gate plays that
sequence without ending the round.

**Open questions:** is the jump instant, or a short transit? Is the exit
protected? Can enemies follow? Does a gate charge, like the extraction gate?

### W6. _(Suggestion)_ A map of what has been seen

A `FrameBuffer` the size of a minimap. Each frame, S2's fan is drawn into it
at map scale. A target keeps what was drawn into it unless it is cleared, so
the fans build up into an "explored" area. The minimap is drawn in a HUD
corner, with lanes, gates, the closing ring and S4's ghosts on top: black
where nothing has been explored, grey where something was explored but is not
seen now, colour where it is seen. It costs one small fan per frame.

---

## More suggestions in line with this

- **Sensor ping.** The player spends energy, and a ring expands from the ship
  (additive, like the wave of the shield ripple). Everything it passes is
  revealed for a second, through rocks and paint, and every enemy it touches
  learns where the player is. It trades stealth for sight.
- **Noise.** Firing makes noise. An unseen enemy that fires shows a short
  flash at its muzzle through the fog. The player's own fire alerts enemies
  within an earshot that depends on the weapon: missiles loud, the beam
  quiet. C5's hearing for enemies already exists.
- **A scout drone, or a flare.** A drone is a second point the player sees
  from, with its own polar map and its own fan, drawn in the same fog pass:
  two fans are just two draws. A flare lights a radius around where it lands
  for a few seconds.
- **A lamp.** The asteroid shader lights each rock from a direction (A3 and
  A5, `EffectParams.b`). A point light at the ship lighting the rocks around
  it would make the interior of a field dark and the player a torch in it.
  Each rock quad already carries its own parameters (F6). Switching the lamp
  off would be stealthier.
- **Ambushes at lane exits, and patrols along lanes.** This is the enemy AI
  from **Later** in the gameplay roadmap, now with places for it to happen.
- **Missile locks need sight.** A missile locks only onto an enemy the player
  can see. If sight is lost, the missile flies to the ghost.
- **Procedural generation.** The generator decides where to erase (clearings,
  lanes) using scatter's `density`, then places cores, resources and enemies
  by clearing. All of it writes the same `Level` struct.

---

## What the render API already gives this

Only one row asks for anything new in the library.

| Want                                    | Existing mechanism                                                         | New library work                          |
| --------------------------------------- | -------------------------------------------------------------------------- | ----------------------------------------- |
| Draw the visible area                   | A `renderTriangles` fan, as for rocks                                      | none                                      |
| Grey everything outside it              | `worldGrade`'s target, a third grade, and the fan textured with the target | none                                      |
| A soft fog edge                         | Vertex alpha on a ring of triangles                                        | none                                      |
| A blurred, softer edge                  | A mask target, blurred by FinalGlow's compute blur                         | **an effect that reads a second texture** |
| Ghost outlines                          | `outline::begin` / `end`                                                   | none                                      |
| Cones cut by rocks                      | A fan from the enemy                                                       | none                                      |
| An explored map                         | A `FrameBuffer` that is never cleared                                      | none                                      |
| Lane streaks                            | Additive blend, generated gradient textures                                | none                                      |
| The speed look                          | Warp stretch, afterimages, `camera::Zoom`, FinalGlow                       | none                                      |
| A jump gate                             | The gate's body, the swirl field in the cloak pass                         | none                                      |
| The sensor ping's wave                  | Per-quad effect parameters, as in the shield ripple                        | none                                      |
| A lamp in the fields                    | The asteroid effect's parameters                                           | none                                      |
| Pixel-exact hiding of half-seen enemies | Their own target, composited through the fan                               | none; layering work only                  |

**An effect that reads two textures** (the world and a mask) is a general
capability: any game's lighting, fog or masking would want it. It is only
worth building if the fan's hard edge, softened with vertex alpha, turns out
not to be good enough.

Lane streaks would be the fourth module to generate its own gradient texture,
and four is where the roadmap says a shared helper "stops being speculative"
(F-notes, `docs/roadmap.md`).

---

## Suggested order

**S1 → S2 → S3 → S4 → M1 → W1 → W2 → W4 / W5 → W3 → W6.** The suggestions can go
anywhere along the way.

- **S1** changes the rules and can be tested without anything new on screen.
- **S2** is the structure that everything after it reads.
- **M1** comes before the W items. It settles the beam's rules before the
  levels fill with field, and W2's interior speed and W4's lanes are tuned
  against flight speed.
- **W1**'s spatial hash already arrives with S1.
