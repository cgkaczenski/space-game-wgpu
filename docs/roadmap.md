# Roadmap

Planned work. The companion to [`webgpu-learning-outline.md`](webgpu-learning-outline.md),
with the opposite lifetime: **the outline only grows and only describes things
that exist; this file shrinks.** An item is deleted from here when it lands, and
a block describing it appears in the outline in its place. Nothing on this page
is built.

Every item says where it **lands**: *library* (`src/render/`), *engine*
(`src/engine/`, created in R7), *game* (`src/gameLayer/`) or *app*
(`src/platform/`). See **The shape features take** below for what each one
means and what the test is.

That tag is the point of the tagging, not bookkeeping: it forces the question
"would another project want this?" before the code is written rather than
after.

**Numbers are stable and never reused.** A gap means the item landed and now
has a milestone block in the outline instead. R1 (error scopes and debug
groups) and R2 (the pipeline cache) are gone that way; so is N1, which R1
covered.

---

## The standing goal: `wgpu2d` as a library

`wgpu2d` should be liftable out of this repo the way
[gl2d](https://github.com/meemknight/gl2d) is. **Liftable like gl2d, not
frozen at gl2d.** Mirroring its signatures was a port tactic — it let the game
files switch by include and namespace, and that job finished at milestone 7. It
was never a ceiling on what a 2D drawing library may grow. `BlendMode` (12) was
the first addition past it and `LayerEffect` (R3) the second; both live in
`wgpu2d.h`, because one public header is a rule R6 can turn into a link error
and a second game-facing render header would need an exception. Exceptions are
how `hudShake.cpp` happened.

It is closer than it looks — a survey of the current code found the inward
direction already clean: nothing under `src/render/` includes anything from
`src/gameLayer/`. What stands in the way is four specific things, not a general
mess:

1. **No target boundary.** `CMakeLists.txt` does `file(GLOB_RECURSE MY_SOURCES
   src/*.cpp)` into one executable. There is no wall, so nothing can be caught
   leaning on it. → **R6**
2. **The library reads the app's resource layout.** `RESOURCES_PATH
   "shaders/quad.wgsl"` (`wgpuContext.cpp:564`) and the same in
   `wgpuImgui.cpp:156`. A library has to carry its own shaders. → **R6**
3. ~~**A game effect lives in the library folder.**~~ **Done.** R3 split the
   mechanism into `wgpu2d::LayerEffect` and R4 moved the policy to
   `gameLayer/hud`. Every include from `gameLayer/` into `render/` is now
   `render/wgpu2d.h`, which is the state R6 needs to enforce.
4. **Who owns the device is unsettled.** `wgpuContext.h` takes a `GLFWwindow*`
   and owns instance, surface, device and the frame bracket. gl2d owns none of
   that — it is handed a live context. Deciding this is most of R6's design
   work, and it is worth deciding before N2/N4 add features and limits to
   device creation.

**Not everything here should travel.** The HUD, the ships, the tuning constants
and the game's ImGui panels are this game's. The renderer, the camera, the
batch, the target and post-process machinery are the library's. The ImGui
*backend* is a third thing — reusable, but not part of a 2D drawing API — and
probably wants its own target rather than a home in either.

---

## The shape features take

Everything on this page is an instance of one split, and naming it is worth more
than any individual item: **a feature is a mechanism plus a policy.** The
mechanism is parameterized and reusable and knows nothing about this game; the
policy is a struct of constants plus the wiring that composes it. The split has
already turned up independently three times, which is why it is written down as
a rule rather than proposed as a scheme:

| Feature | Mechanism (general) | Policy (this game) | Item |
|---|---|---|---|
| HUD shake | flush a layer through a target, draw it back transformed | decay 10/s, 9px, 19 and 24 Hz, 1.4°, triggered by damage | R3 / R4 |
| Camera | the transform and its slots, **and `follow` as a function** | the *call*: `follow(data.playerPos, 550, 0, 0, w, h)` | R5 |
| Collision | `ICollisionSystem`, overlap dispatch, `Hitbox` variant | `shipHitbox()` — *ships* | R7 |
| Movement | input-independent integrator with a momentum option | `playerMoveSpeed = 2000.f`, WASD | R8 |

### A mechanism is not always one home

The camera row above was wrong in an earlier version of this file, which filed
`follow` itself as policy. Only the **call** is policy. `Camera::follow` is
pure position maths — a chase with a dead zone (`min`), a leash (`max`) and
speed easing near the target — with no drawing and no knowledge of this game;
`w, h` only centre the target in the view. It is a parameterized behaviour, the
same shape as R8's integrator. It ended up as a method on `wgpu2d::Camera`
because gl2d put it there, not because a drawing library needs it. That is the
`shipHitbox` leak a second time.

So the rule needs one more turn of the screw: **a mechanism can span two
homes.** Both of the features this file spends the most words on are three
pieces, not two.

| | `render/` | `engine/` | `gameLayer/` |
|---|---|---|---|
| **HUD** | flush a layer through a target, draw it back transformed | layout maths — already `glui`, and already out of tree | this health bar, these textures, `xLeftPerc(0.65)`, `fill = data.health`, decay 10/s, the damage trigger |
| **Camera** | position, zoom, rotation, `viewProj`, the dynamic-offset slots, `pushCamera`/`popCamera` | `follow` — chase a point with speed / min / max | `follow(data.playerPos, 550, 0, 0, w, h)` |

Two things follow from that, and both are cautions against inventing work:

- **There is no HUD system for `engine/` to own.** R3 and R4 already cut it
  correctly; the middle column is `glui`, which is a separate CMake target
  under `thirdparty/` and included only by `gameLayer.cpp`. Moving it into
  `src/engine/` would mean vendoring a third-party library into our own tree,
  which is worse than leaving it. It moves if and when something in-tree wants
  it, not to prove the folder exists.
- **Same feel, different home.** A world-camera shake (nudge the view's
  position) would be another `engine/` behaviour. The HUD shake stays a
  `render/` layer effect. One mutates a transform, the other composites a
  texture, and no amount of shared vocabulary makes them the same piece.

`collisionSystem.h` is the instructive one because it is already **half right**:
the interface and the overlap dispatch are exactly what a reusable system should
look like, and then `shipHitboxRadius()` and `shipHitbox()` sit at the bottom of
the same header. Nothing is wrong with that code — it is the boundary that is in
the wrong place. Copy its shape, not its file layout.

### Four homes, not three

`AGENTS.md` describes three homes because three folders exist. Movement,
inventory, combat, enemy AI and in-game UI fit none of them: they are not
drawing, so `render/` is wrong, and burying them in `gameLayer/` welds them to
this game, which is the thing the whole exercise is trying to avoid. They need a
fourth home, created in R7.

| Home | Holds | Travels? |
|---|---|---|
| `src/render/` | `wgpu2d` — the drawing library | yes, one day (R6) |
| `src/engine/` *(created in R7)* | movement, collision, inventory, combat resolution, AI behaviours, UI widgets | maybe, later |
| `src/gameLayer/` | ships, enemy types, tuning numbers, HUD content, composition | no — this *is* the game |
| `src/platform/` | window, input backend, loop, ImGui wiring | no |

The test for `engine/` is not "is this generic code." It is **"could a different
game use this without editing it?"** An integrator with a momentum option
passes. `playerMoveSpeed = 2000.f` does not. `follow` passes; the call to it
does not — see **A mechanism is not always one home** above, which is where
that distinction gets applied to the two features it matters most for.

`engine/` gets tenants when something needs to live there, never to prove it
exists. Collision is the first because it is already shaped correctly and only
needs moving; `follow` is the second for the same reason.

Two of the planned features do not sit in one row, and it is cheaper to know
that now:

- **In-game UI is two things.** Layout and widgets are engine — `glui` is
  already exactly this, reduced to `Frame` and `Box` when its gl2d half was
  dropped. *Which* screens this game has is `gameLayer`. ImGui debug panels are
  neither: they are app.
- **Combat is mostly policy.** The general part is thin — apply damage, resolve
  a hit, handle death. Damage values, what a hit does, who may hit whom: game.
  Build the thin part and stop, or it becomes a config file with extra steps.

---

## Now — the render track

R3–R6. Ordered: each unblocks the ones after it. All of it lives under
`src/render/`.

R1 and R2 have landed (outline milestones 11 and 12), and R2 came with a
correctness fix its machinery made cheap: render targets are now composited
with `BlendMode::Premultiplied`, so a translucent target no longer has its
coverage applied twice. That changes R3 below — the shake's round trip is
already correct, so generalizing it is purely a factoring job now.

### R3. Generalize the shake into a layer effect

**Lands in:** library

`hudShake.cpp` is two things wearing one name: a **mechanism** (flush the
pending batch through a render target, draw the target back through a transform)
and a **policy** (decay 10/s, 9px, 19 and 24 Hz, 1.4°, triggered by taking
damage). The mechanism is general and belongs in the library; the policy is this
game's HUD.

Split it so the library exposes "flush a layer through a target and draw it back
transformed" and the caller supplies the transform. F2's vignette, hit flash and
death warp are the same move with a different transform, which is why this is
not speculative generality — it has three known second users.

### R4. A HUD module

**Lands in:** game

The HUD content has no home. The healthbar's layout, textures and two draws are
inline at `gameLayer.cpp:490-518`, inside a `gameLogic()` that runs from line 155
to 566 across ten `#pragma region`s. New HUD elements have nowhere to go except
further into that function.

`src/gameLayer/hud.{h,cpp}` owns the layout, the textures, the draws, the shake
policy from R3 and the damage trigger. `gameLogic` calls it once. The test for
whether this worked: adding a second HUD element touches one file.

**Stop there.** R3 and R4 are the whole cut — there is no third, general "HUD
system" for `engine/` to own. The only travelling piece is layout, and that is
already `glui`, already out of tree. Another game wants a layer effect; it does
not want a health bar.

### R5. Strip the camera down to a transform

**Lands in:** library (and, for now, game)

`wgpu2d::Camera` is a position, a zoom, a rotation that is accepted and
ignored, and a `follow()` that has no business being a method on it. Milestone
6b already built the hard part underneath — several cameras per frame in
dynamic-offset uniform slots.

**The item is one sentence: `Camera` becomes a transform, and `follow` stops
being a method.** What is left on the struct is position, zoom, rotation,
`viewProj` and the slot machinery — how sprites get on screen, which is what a
drawing library is for. `follow` is a behaviour that never draws.

**Where `follow` goes, and the trap.** Its home is `engine/`, which R7 creates.
Until then it becomes a free function in `gameLayer/`, and R7 moves it in
alongside collision. It must **not** be parked somewhere in `render/` in the
meantime under a different name — that is precisely how `hudShake.cpp` came to
be a general drawing mechanism sitting in the library with one game's numbers
compiled into it. This is also what `AGENTS.md` already prescribes: until
`engine/` exists, a system goes in `gameLayer/` with mechanism and policy in
separate files, so the move is a move.

**Signature matters here.** `follow` must not take a `Camera&`, or `engine/`
ends up including `wgpu2d.h` and the boundary is lost on day one. It takes and
returns plain maths — roughly `nextPosition = follow(current, target, params,
viewSize)` — and the game assigns the result into `currentCamera.position`.
Verified as safe: `follow` is called only from `gameLayer.cpp:80` and `:214`,
and nothing under `src/render/` uses it, so removing it from the library breaks
nothing inside the library.

Rotation is a separate decision in the same file: implement it or delete the
field. Carrying an ignored one is worse than either.

F3 (parallax layers) is the first real consumer and the honest test of the
design.

### R6. Split the library out as its own target

**Lands in:** build

`add_library(wgpu2d)` over `src/render/`, the game links it, and the remaining
blockers listed at the top get fixed in the process — shaders embedded rather
than loaded from `RESOURCES_PATH`, and the device ownership question answered.
Blocker 3 is already gone. The payoff is that the boundary stops being a rule
in `AGENTS.md` and becomes a link error.

**What the target exports is `wgpu2d.h` and nothing else.** `wgpuContext.h` is
the platform layer's, `wgpuFrame.h` is internal, and `wgpuImgui.h` /
`wgpuMetalLayer.h` are app. That single public header is what makes the wall
checkable rather than a convention, so a new drawing capability goes into it
rather than beside it.

Do it after R3–R5, not before: the split is easy once the things that cross the
line have been moved, and painful while they still do.

---

## Now — the game track

R7–R11. Independent of the render track: different folders, no shared files, so
the two can be interleaved in any order. R7 comes first within this track
because it decides where everything after it lives.

### R7. A fourth home, and collision as its first tenant

**Lands in:** engine (new)

Create `src/engine/`, and move `collisionSystem` into it — splitting the policy
out on the way. `ICollisionSystem`, the overlap dispatch and the `Hitbox`
variant are engine; `shipHitboxRadius()` and `shipHitbox()` go to `gameLayer`,
because a ship is not a general concept.

Doing this first, with code that already exists and already works, means the
pattern is established and demonstrated before anything new is written against
it. It is a move, not a design.

**Second tenant, in the same item:** `follow`, which R5 will have already
detached from `wgpu2d::Camera` and parked in `gameLayer/`. It arrives here
already shaped — plain maths in, plain maths out — so this is a file move, not
a redesign either.

**Ordering note.** R5 and R7 look like they contradict each other: R5 is on the
render track and `follow`'s home is a folder R7 has not created yet. They do
not, because R5 does not try to land `follow` in its final home — it only takes
it off the struct. The alternative, running R7 before R5 so `follow` moves once
instead of twice, works too and costs one fewer move; it was not taken because
it couples the two tracks, and the render track should stay about drawing. A
free function moving between two files is the cheapest thing on this page.

While in there: `separation()` is declared on the interface, implemented, and
called from nowhere in the repo. Either wire it up to the ship-ship resolution
its comment describes, or delete it — an untested virtual on an interface is a
design claim that nothing has checked.

### R8. Movement as a feature with options

**Lands in:** engine (the integrator) + game (the tuning and the key bindings)

Today movement is roughly forty lines inline at `gameLayer.cpp:170-209`, and the
reason it cannot take a momentum option is not the physics — it is that three
separable things are fused:

1. **input → intent** — WASD / arrows into a direction vector
2. **intent → velocity** — the integrator. Momentum lives here: instant,
   acceleration + drag, thrust relative to facing
3. **velocity → position** — apply, clamp, resolve

The block calls `platform::isButtonHeld` directly, so 1 and 2 are the same code.
The concrete cost: **an enemy can never use this movement, because enemies do
not have keyboards** — and so `Enemy::update` (`enemy.cpp:70`) hand-rolls a
second integrator that shares nothing with the first. Two implementations of one
idea already.

So the rule that makes it a feature: **the integrator must not know where the
intent came from.** Once that holds, the player and the enemy are both
consumers, and momentum is a field in an options struct rather than a rewrite.

Fix while here: `gameSpeedMultiplier()` is applied *inline* at the player's
movement site but *passed as an argument* into `Enemy::update` and
`Bullet::update`. Two conventions for one concept, and the integrator is where
they should become one.

### R9. Entity data, behaviour and drawing are three things

**Lands in:** game, mostly

`Enemy` is simultaneously state, AI (`update`, with `turnSpeed`, `fireRange`,
`fireTimeReset`), a hitbox, and a draw call — `render(renderer, sprites, atlas)`,
which is why `enemy.h` includes `render/wgpu2d.h`. `Bullet` is the same shape,
smaller.

Enemy AI cannot become a feature of its own while the struct that holds it also
holds a renderer dependency. Separating update from render is the prerequisite,
not a tidy-up — and it is what lets a behaviour be swapped, tested, or reused
without dragging `wgpu2d` along.

### R10. Ownership: assets, state, and the globals block

**Lands in:** game

`gameLayer.cpp:39-60` is a file-scope block holding the renderer, four texture
handles, two atlases, a `Sound`, and five debug flags. This is what will fight
every extraction on this page: **a feature cannot be self-contained while its
state is a global in another translation unit.**

The real question inside it is *who owns assets*. Textures are loaded in
`initGame` and then passed by reference into `Enemy::render`. That is the same
unanswered question as "who owns the device" in R6, one layer up, and it wants
the same kind of answer rather than another set of globals.

Related and currently unanswered: `restartGame()` resets `data` and nothing
else — not the camera, not the shake, not the debug flags. As features acquire
state, *what restart means* needs a real definition.

### R11. Each feature owns its debug UI

**Lands in:** game and engine

The ImGui block at `gameLayer.cpp:524-554` is the HUD problem forming a second
time: six controls inline, one per feature, growing. Every item on this page
will want debug controls.

A feature exposes its own debug entry point; the panel calls them and holds
nothing itself. Small, and much cheaper decided at six controls than at twenty.

### Smaller things, worth doing while nearby

- **Two clocks, currently implicit.** `gameSpeedMultiplier()` scales simulation
  time, while `hudShake` deliberately uses `steady_clock` so the debug slider
  cannot slow the shake — a distinction that exists only as a comment in
  `hudShake.cpp`. Name it before more features pick a clock by accident.
- **`Camera::rotation`** is accepted and ignored. Implement it or delete the
  field; carrying an ignored one is worse than either (noted in R5 too).

---

## Then — guide chapters worth doing

Guide chapters that have a genuine use in a 2D sprite renderer, ranked by what
they teach per unit of work rather than by guide order. Each block is the same
shape as a milestone — **concepts → chapters → where it would land** — with a
note on why it earns its place and what to establish before starting. All of
these land in the **library** unless the block says otherwise.

The state they all start from: one render pipeline, one sprite shader, a CPU
growable vertex batch split into texture/camera runs, dynamic-offset camera
uniforms, render targets, and a hand-written ImGui backend. No compute, no
instancing, no storage buffers, no depth, no multisampling, no query sets. The
device is created with `requiredFeatureCount = 0` and `requiredLimits = nullptr`
(`wgpuInit`), so anything needing a feature or a raised limit starts by changing
that call.

**Order, once R3–R6 are done:** N2 → F1 → N5 → N3 → N4 → F5. Instrumentation
before optimisation. F1 is now the cheapest thing on this page: R2 already
built `BlendMode::Additive` and the run splitting, so all that is left is the
game choosing it.

---

### N2. Timestamp queries — a GPU frame time

**Concepts:** `QuerySet`, `timestampWrites` on the render pass descriptor,
`resolveQuerySet` into a buffer, a mapped readback buffer to get the numbers to
the CPU, and the frame of latency that implies (read last frame's result, not
this one's). First real use of `requiredFeatures` at device creation.

**LearnWebGPU:** [Benchmarking / Time](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/benchmarking/time.html)

**Would land:** a small timing module beside `hudShake`, read out in the
existing ImGui debug window; `deviceDesc.requiredFeatures` in `wgpuInit`.

**Check first:** `printAdapter` already lists `adapter.getFeatures`. Confirm
`TimestampQuery` is in the list on this AMD/Metal adapter before planning
around it; wgpu-native does not offer it everywhere, and it must be requested as
a required feature, not just be present.

**Why early:** it is the measuring instrument. N4, N5, N6 and N9 are all claims
about cost, and without this they are guesses.

---

### N3. Screen capture, and a headless context

**Concepts:** `copyTextureToBuffer` with the 256-byte `bytesPerRow` alignment
rule, `mapAsync` and the map-callback lifetime, `RenderAttachment | CopySrc`
usage flags. Headless is the same thing with no surface at all: no window, no
present, a texture as the only attachment.

**LearnWebGPU:** [Screen capture](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/screen-capture.html) (WIP) · [Headless context](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/headless.html)

**Would land:** a capture entry point in `wgpuContext.cpp` next to
`compositeScaledTarget`; a key binding in `glfwMain.cpp`.

**Why it earns its place:** this has now been written twice as throwaway
scaffolding — for the milestone-7 parity check, and again for milestone 12's
blend numbers. The second time also needed `CopySrc` added to render targets,
which this item should make permanent. Making it permanent turns the
verification norm in `AGENTS.md` into a committed tool — a fixed, time-independent
scene rendered headless to a PNG is a regression harness, not a demo.

---

### N4. Compute pipeline: GPU mipmaps, then a convolution filter

**Concepts:** compute pipeline and compute pass as a sibling of the render pass
on the same encoder, workgroup sizing, `dispatchWorkgroups`, storage textures
and `textureStore`, per-mip-level texture views, the `StorageBinding` usage
flag.

**LearnWebGPU:** [Compute Pipeline](https://eliemichel.github.io/LearnWebGPU/basic-compute/compute-pipeline.html) · [Mipmap Generation](https://eliemichel.github.io/LearnWebGPU/basic-compute/image-processing/mipmap-generation.html) · [Convolution Filters](https://eliemichel.github.io/LearnWebGPU/basic-compute/image-processing/convolution-filters.html)

**Would land:** replaces `downsampleRGBA8` in step one; step two is a blur over
the scaled world target from milestone 10, before `compositeScaledTarget`.

**Why it earns its place:** the largest single hole in the port — milestone 7
took the CPU half of that chapter and left the compute half. Doing mips first
keeps the new material to compute alone (the pixels are already known-correct);
the convolution pass then reuses milestone 10's target machinery, so again the
only new thing is the compute side.

---

### N5. Instanced drawing

**Concepts:** `VertexStepMode::Instance`, a second vertex buffer holding
per-instance data (rect, uv rect, colour, rotation), `draw` with an instance
count, and moving the corner and rotation maths from `pushQuad` into the vertex
shader.

**LearnWebGPU:** [Instanced Drawing](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/instanced-drawing.html) — **an empty TODO page.** The technique, not the chapter: the only guide material is the `stepMode` field noted in [A first Vertex Attribute](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/input-geometry/a-first-vertex-attribute.html), so read `WGPUVertexStepMode` and `wgpuRenderPassEncoderDraw`'s `instanceCount` in the vendored `webgpu.h` and work from the spec

**Would land:** `pushQuad`, `flushBatch`, `createQuadPipeline`'s vertex layout,
`quad.wgsl`.

**Why it earns its place:** `createQuadPipeline` already carries the comment
"advance once per vertex, not per instance" — this is the question that comment
raises. Roughly a fifth of the upload volume, and with N2 in place it becomes a
measured A/B against milestone 6a rather than a rewrite on faith. Per-quad
colour arrays (`Color4f colors[4]`) are the one part of the gl2d signature that
does not fit an instance record; the fallback is to keep both paths and pick per
run.

---

### N6. Multisampling

**Concepts:** `multisample.count` on the pipeline, a multisampled colour
attachment with `resolveTarget` set, and the constraint that a multisampled
texture cannot be sampled in a shader — it has to resolve first.

**LearnWebGPU:** [Multi-Sampling](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/multi-sampling.html) — **an empty TODO page.** Work from `WGPUMultisampleState` and `WGPURenderPassColorAttachment::resolveTarget` in the vendored header

**Would land:** `createQuadPipeline`, `ensurePassBegun`, `createRenderTarget`.

**Why it earns its place:** the visible aliasing in this game is the debug
hitbox outlines and `renderLine` quads. It also collides productively with
milestone 10 — a render target that is both multisampled and later sampled needs
two textures, which is the clearest possible demonstration of what a resolve is.

---

### N7. A depth buffer, used as a sort key

**Concepts:** depth attachment, `depthCompare`, `depthWriteEnabled`, the depth
format, and the reason a 2D renderer would want any of it: a per-sprite layer
value lets draws be reordered by *texture* without changing what the player
sees, which attacks the run splitting in `flushBatch` directly.

**LearnWebGPU:** [Depth buffer](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/3d-meshes/depth-buffer.html)

**Would land:** `createQuadPipeline`, `ensurePassBegun`, a depth field on the
batch vertex.

**The honest limit:** it only works for opaque sprites. Blended draws still have
to go back to front, so this splits the batch into an opaque pass (texture
sorted, depth tested) and a blended pass (draw order, depth test on, depth write
off). That split is the actual lesson.

---

### N8. 2D lighting with normal maps, via multiple render targets

**Concepts:** more than one colour attachment on a pass (albedo + normal), a
fragment shader returning a struct of `@location`s, and a second full-screen
pass that consumes both. Point lights for thrusters and bullets.

**LearnWebGPU:** [Normal mapping](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/lighting-and-material/normal-mapping.html) is written and real · [Deferred Shading](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/deferred-shading.html) is an empty TODO page, so the multi-attachment half comes from `FragmentState::targets` in the header, not from the guide

**Would land:** a second pipeline and shader pair; `createRenderTarget` grows a
multi-attachment variant.

**Why it earns its place:** the multi-attachment pass is genuinely new
machinery, and this is the one place in a 2D game where it is the natural
answer rather than an imported 3D habit. Needs normal maps for the sprite atlas,
which is art work, not code — the reason it sits this far down.

---

### N9. HDR and tonemapping

**Concepts:** a float colour target (`RGBA16Float`), values above 1, a tonemap
on composite, and what the surface can actually present.

**LearnWebGPU:** [High Dynamic Range Textures](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/hdr-textures.html) (WIP)

**Would land:** `createRenderTarget`, `compositeScaledTarget`, and the format
choice in `configureSurface`.

**Head start:** the surface-format survey above already records two 10/16-bit
formats offered on this machine. Also note the pipeline/format rule from
milestone 10 — every pipeline drawing into a float target must be built for that
format, so this is not a one-line change.

---

### N10. RAII

**Concepts:** the guide's wrapper-plus-RAII pattern, replacing paired
`create` / `release` calls.

**LearnWebGPU:** [RAII](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/raii.html)

**Would land:** `wgpuContext.cpp` throughout; `Context`'s fields.

**Why it earns its place:** several early-return paths in the file leak.
`createCameraBindGroup` releases correctly on failure and shows how much care
that takes by hand.

---

### Still out for good

Cube maps, IBL, PBR, tesselation, raytracing, procedural geometry and neural
networks have no 2D analogue worth the detour. Building for the Web is worse
than it looks: `wgpuMetalLayer.mm` and the wgpu-native pin both stand in the
way. Render bundles stay marginal here because the batch is rebuilt from scratch
every frame — the tiled background is the only static geometry, and it is nine
quads — and the guide's page for them is an empty TODO anyway.

A general warning about this whole section: most of Advanced Techniques is
unwritten. Instanced Drawing, Multi-Sampling, Deferred Shading, Render Bundles,
Shadow maps, Tesselation and Raytracing are all empty TODO pages today; Screen
capture, HDR and Debugging are partial. Only RAII and Benchmarking/Time are
finished. Basic Compute is in much better shape: Compute Pipeline, Mipmap
Generation and Convolution Filters are all written, which is another reason N4
is the best-supported large item here.

---

## Features that deepen what is already built

Game-facing rather than chapter-facing. Each one re-enters a finished milestone
from a new angle, and none of them needs a chapter that is not already read.
These are where the library/game line gets tested in practice, so each says
which side it falls on.

**F1. Additive bullets and explosions.** *(game)* Mostly done: R2 built
`BlendMode::Additive`, the pipeline variant and the run splitting, and outline
12 records what that taught. What remains is the game calling `setBlendMode`
where it wants light to add — which is a gameplay-side judgement about which
sprites those are, not render work.

**F2. A post-process chain on the world target.** *(library mechanism, game policy — the split R3 makes)* Milestone 10 built the
machinery and then used it twice (render scale, HUD shake). A damage vignette, a
chromatic-aberration hit flash, a warp on death — each is a different *shader*
over the same vertex format and the same attachment. It separates pipeline from
shader from target in a way a single-shader renderer cannot.

**F3. Parallax background layers through the camera stack.** *(library: the camera behaviour from R5; game: which layers and at what depth)* Milestone 6b built
dynamic uniform offsets and `pushCamera` / `popCamera`; the game uses one world
camera and one HUD camera. Three layers at 0.3x / 0.6x / 1.0x needs no new API
at all, but exercises the slot stride, the run splitting and `getViewRect` under
real pressure — and it is the cheapest thing here that makes the game look
better. It is also the first real consumer of R5, and the honest test of
whether that camera design is general or just rearranged.

**F4. Present mode from the debug window.** *(library setting, app control)* `configureSurface` picks one.
Switching Fifo / Immediate / Mailbox at runtime and watching what it does to
N2's numbers is a few lines, and it is the clearest way to learn what the
surface actually is.

**F5. Capstone: a GPU-driven starfield.** *(library: the compute-driven instanced particle system; game: that they are stars)* Star positions in a storage buffer,
advanced each frame by a compute shader, drawn as instanced quads that read that
buffer. Combines N4, N5 and storage buffers — none of which the port has — into
one thing the game visibly gains, replacing a tiled texture with a particle
count the CPU batch could not carry. Also the natural first place to exceed a
default limit and have to fill in `requiredLimits`.
