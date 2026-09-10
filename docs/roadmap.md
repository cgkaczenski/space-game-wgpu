# Roadmap

Planned work. The companion to [`webgpu-learning-outline.md`](webgpu-learning-outline.md),
with the opposite lifetime: **the outline only grows and only describes things
that exist; this file shrinks.** An item is deleted from here when it lands, and
a block describing it appears in the outline in its place. Nothing on this page
is built.

Every item says where it **lands**: _library_ (`src/render/`), _engine_
(`src/engine/`), _game_ (`src/gameLayer/`) or _app_
(`src/platform/`). See **The shape features take** below for what each one
means and what the test is.

That tag is the point of the tagging, not bookkeeping: it forces the question
"would another project want this?" before the code is written rather than
after.

**Numbers are stable and never reused.** A gap means the item landed and now
has a milestone block in the outline instead. R1–R7 and N1 are gone that way;
outline 11, 12 and 13 are where they went.

---

## The standing goal: `wgpu2d` as a library — reached

`wgpu2d` is a CMake target that links `webgpu`, `glm` and `stb_image`, knows
nothing about GLFW, carries its own shader, and cannot include this game's
headers. All four blockers are gone: the glob, `RESOURCES_PATH`, the game
effect in the library folder, and the window dependency. **Liftable like gl2d,
not frozen at gl2d** — mirroring its signatures was a port tactic that finished
at milestone 7; `BlendMode` and `LayerEffect` are additions past it, and new
drawing capabilities go in `wgpu2d.h` rather than beside it.

Two limits worth stating so nobody assumes more than is true. The wall is
one-directional: a library cannot see the game, but `engine` and `wgpu2d` can
see each other, because both sit under one `include/` root. And nothing has
actually built `wgpu2d` against a second application, which is the only real
test of a library — until something does, "liftable" is a claim.

**Not everything here should travel.** The HUD, the ships, the tuning constants
and the game's ImGui panels are this game's. The renderer, the camera, the
batch, the target and post-process machinery are the library's. Reusable
gameplay systems that draw nothing are `engine`'s.

---

## The shape features take

Everything on this page is an instance of one split, and naming it is worth more
than any individual item: **a feature is a mechanism plus a policy.** The
mechanism is parameterized and reusable and knows nothing about this game; the
policy is a struct of constants plus the wiring that composes it. The split has
already turned up independently three times, which is why it is written down as
a rule rather than proposed as a scheme:

| Feature   | Mechanism (general)                                         | Policy (this game)                                       | Item    |
| --------- | ----------------------------------------------------------- | -------------------------------------------------------- | ------- |
| HUD shake | flush a layer through a target, draw it back transformed    | decay 10/s, 9px, 19 and 24 Hz, 1.4°, triggered by damage | R3 / R4 |
| Camera    | the transform and its slots, **and `follow` as a function** | the _call_: `follow(data.playerPos, 550, 0, 0, w, h)`    | R5      |
| Collision | `ICollisionSystem`, overlap dispatch, `Hitbox` variant      | `shipHitbox()` — _ships_                                 | R7      |
| Movement  | input-independent integrator with a momentum option         | `playerMoveSpeed = 2000.f`, WASD                         | R8      |

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

|            | `render/`                                                                                | `engine/`                                              | `gameLayer/`                                                                                             |
| ---------- | ---------------------------------------------------------------------------------------- | ------------------------------------------------------ | -------------------------------------------------------------------------------------------------------- |
| **HUD**    | flush a layer through a target, draw it back transformed                                 | layout maths — already `glui`, and already out of tree | this health bar, these textures, `xLeftPerc(0.65)`, `fill = data.health`, decay 10/s, the damage trigger |
| **Camera** | position, zoom, rotation, `viewProj`, the dynamic-offset slots, `pushCamera`/`popCamera` | `follow` — chase a point with speed / min / max        | `follow(data.playerPos, 550, 0, 0, w, h)`                                                                |

Two things follow from that, and both are cautions against inventing work:

- **There is no HUD system for `engine/` to own.** R3 and R4 cut it that way; the middle column is `glui`, which is a separate CMake target
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
this game, which is the thing the whole exercise is trying to avoid. That is why
`src/engine/` exists.

| Home             | Holds                                                                       | Travels?                      |
| ---------------- | --------------------------------------------------------------------------- | ----------------------------- |
| `src/render/`    | `wgpu2d` — the drawing library                                              | yes — a CMake target since R6 |
| `src/engine/`    | collision and camera behaviours today; movement, inventory, combat, AI next | maybe, later                  |
| `src/gameLayer/` | ships, enemy types, tuning numbers, HUD content, composition                | no — this _is_ the game       |
| `src/platform/`  | window, input backend, loop, ImGui wiring                                   | no                            |

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
  dropped. _Which_ screens this game has is `gameLayer`. ImGui debug panels are
  neither: they are app.
- **Combat is mostly policy.** The general part is thin — apply damage, resolve
  a hit, handle death. Damage values, what a hit does, who may hit whom: game.
  Build the thin part and stop, or it becomes a config file with extra steps.

---

## The render track: done

R1–R6 have landed, plus R7 from the game track, which came with them because
the boundary work needed the fourth home to exist. Outline milestones 11, 12
and 13 are the write-ups.

What that bought, in one line each: validation errors now name the operation
that caused them; a pipeline is chosen per (target format, blend) instead of
there being exactly one; render targets composite correctly instead of
darkening translucent layers; `hudShake` is a general layer effect plus this
game's numbers in `gameLayer/hud`; `Camera` is a transform and `follow` is a
behaviour; and `wgpu2d` is a CMake target that cannot see the game.

**Two things it did not buy, both recorded rather than fixed:**

- **The sideways wall does not exist.** `engine` including `render/wgpu2d.h`
  compiles and links, because both sit under one `include/` root and the
  executable links both libraries. Only library-to-game is enforced. Fixing it
  means per-library include roots — headers under `src/render/include/render/…`
  and `src/engine/include/engine/…`, each target exposing only its own. A
  repo-shape change touching every header, deliberately not folded into R6.
- **There is nowhere to keep a test — half fixed.** Five harnesses were
  written and thrown away before N3 landed the capture path: the milestone-5
  matrix parity check, the blend readback in 12, the `follow` equivalence check
  in R5, `separation()` in R7, and the frame-capture probe rebuilt four times
  over the graphics features. Each caught or confirmed something worth having —
  the `follow` one caught a silent one-ulp change to the camera's easing.
  **Pixels now have a home**: `WGPU_SCREENSHOT_FRAME` plus `WGPU_OFFSCREEN`
  makes a scripted capture a committed tool. **Numbers still do not.** The
  arithmetic checks — matrix parity, blend sums, `follow`, `separation` — have
  no test target to live in, and each one is still written and deleted.

---

## The unreproduced stall

**Open.** A single observation: the frame rate fell from its usual 75 to about
15, with visible camera jitter, and did not recur across minutes of play after
a restart. No reproduction yet, so nothing has been changed to "fix" it.

Two things narrow it before any code moves.

**15 fps is exactly 75/5.** `PresentMode::Fifo` quantises to divisors of the
refresh rate, so a frame was taking between 53 and 67 ms — a large, specific
budget, not a small regression.

**The jitter is a symptom, not a second bug.** `camera::follow` can never keep
up while a direction is held: at 75 fps the player moves 26.7 px per frame and
the camera 7.3, so it rides the leash and snaps to exactly 150 behind. On a
turn, the distance dips under the leash for one frame and the *eased* branch
runs instead. Those two branches differ by 19 px at 75 fps and by 96 px at 15.
Same alternation, five times more visible — and gl2d had a disabled
anti-jitter block at exactly that spot, which R5 removed while noting the
jitter it targeted might still be there.

**Candidates, ranked by what the code can actually do:**

1. **A failed pipeline build retried forever.** `getQuadPipeline` does not cache
   a failure — it returns null and `flushBatch` skips the run — so a variant
   that cannot be built is re-attempted on *every draw run of every frame*: up
   to 14 `createRenderPipeline` calls a frame, each with a blocking error-scope
   drain. That alone reaches the right order of magnitude.
2. **A spuriously failed build.** `createQuadPipelineVariant`'s error scope
   catches any validation error open during its window, not only its own, so an
   unrelated error can make a good pipeline be discarded and rebuilt forever.
   Same storm, different trigger.
3. **A mid-frame stall** — a texture or render target allocated during a frame,
   or an error scope draining with 1 ms sleeps.
4. **Outside the process** — thermal throttling, GPU contention, another
   application.

**What was built instead of a fix: a slow-frame recorder** (`glfwMain`). A
frame over three times the running average *and* over 25 ms prints its counters
and the previous eight frames. Every counter is zero in a steady frame, so
whichever is not zero names the cause: `builds` large means 1 or 2, `errors`
means the device complained, `blocked` means it sat draining a callback,
`textures` means an allocation. **All zeros means 4**, which is worth as much,
because it eliminates the rest.

**Deliberately not fixed yet: candidates 1 and 2 are real defects visible by
reading.** Patching them now would destroy the evidence — if one of them is the
bug, the recorder proves it on first recurrence; if they are patched blind and
the stall never returns, nothing is learned and nobody knows whether it is
gone. They are worth fixing on their own merits *after* the question is
settled, not before.

Run with `2>&1 | tee` so the report survives the session.

---

## Now — the game track

R8–R11. R7 landed early, alongside the render track, because the boundary work
needed `src/engine/` to exist; it is outline 13. Everything below now has a
home to go to.

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

Fix while here: `gameSpeedMultiplier()` is applied _inline_ at the player's
movement site but _passed as an argument_ into `Enemy::update` and
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

The real question inside it is _who owns assets_. Textures are loaded in
`initGame` and then passed by reference into `Enemy::render`. That is the same
unanswered question as "who owns the device" in R6, one layer up, and it wants
the same kind of answer rather than another set of globals.

Related and currently unanswered: `restartGame()` resets `data` and nothing
else — not the camera, not the shake, not the debug flags. As features acquire
state, _what restart means_ needs a real definition.

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

### N2. ~~Timestamp queries~~ — **not possible on this adapter**

**Checked before building, and the answer was no.** `printAdapter` now lists
features by name rather than counting them, and this AMD Radeon Pro 560X on
Metal via wgpu-native v24 reports 22 features without the one this item needs:

```
DepthClipControl, Depth32FloatStencil8, TextureCompressionBC,
IndirectFirstInstance, ShaderF16, RG11B10UfloatRenderable,
BGRA8UnormStorage, Float32Filterable, DualSourceBlending
+ 13 wgpu-native extensions (push constants, subgroups, binding arrays, ...)
```

`TimestampQuery` (0x3) is absent, and so are wgpu-native's own
`TimestampQueryInsideEncoders` (0x30024) and `TimestampQueryInsidePasses`
(0x30025). A feature that is not advertised cannot be requested at device
creation, so `QuerySet`, `timestampWrites` and `resolveQuerySet` are all
unreachable. This is a refusal, not a difficulty.

**The consequence, and it matters for later items:** N4 and N5 both make claims
about cost, and those claims cannot be settled by GPU timing on this machine.
Anything that says "measured A/B" about GPU work has to mean one of the three
below, or it is guessing.

#### What was built instead — all three, and they work

**N2a. Frame stats in the debug panel.** *(library + app)* **Landed.** The data
mostly existed and none of it was surfaced. `glfwMain` already computes `deltaTime` and
passes it to `gameLogic` without ever showing it; the renderer counts quads,
cameras and draw runs but prints them **once**, to stdout, on the first flush.
Making those live — CPU frame time, quads, runs, pipeline variants — would
catch regressions in the batch, the uploads and the run splitting, which is
where this renderer's cost actually sits. It measures the CPU side only, and
should say so.

**N2b. `wgpuQueueOnSubmittedWorkDone`.** *(library)* **Landed**, and the first
reading justified the label: `submit->done` came back at 8.56 ms against a CPU
frame of 8.30 ms, so it is tracking frame pacing rather than GPU work. The
panel says `submit->done` and not "gpu" for that reason -- a number called
"gpu" that is not one is worse than no number. Timing submit to callback gives a rough GPU-completion figure. It
includes queue waits and vsync, so it is not clean GPU work — but it is real
signal and it is the closest the API offers here.

**N2c. A Metal frame-capture trigger.** *(app, macOS)* **Landed, and the risky
part paid off.** wgpu-native exposes no `MTLDevice` -- there is no HAL escape
hatch in `wgpu.h` -- so the capture targets `MTLCreateSystemDefaultDevice()`
and bets that Metal device objects are per-GPU singletons and wgpu took the
default. Verified rather than assumed: the trace came back at 16 MB, and
`MTLBuffer-1098-0` is exactly 32768 bytes, which is what the renderer's own log
reports for the ImGui vertex buffer capacity. Those are our resources. A
capture of the wrong device would be empty.

One limit that could not be checked from here: the command stream lives in a
compressed `store0`, so whether R1's labels and debug groups actually render as
a readable tree can only be confirmed by opening the trace in Xcode.

The item whose groundwork was already paid for: R1 added 28 object labels and the debug groups a capture needs to be
readable, so a trace would already show "batch -> surface", "composite scaled
target", "imgui" rather than anonymous draws. What is missing is a way to
*start* one — today capturing means launching the CMake binary under Xcode.
`MTLCaptureManager` can start a capture programmatically into a `.gputrace`,
and `src/platform/wgpuMetalLayer.mm` is already an Objective-C++ TU with Cocoa
and QuartzCore linked, so the hook has a home and a key binding is the rest.

**All three are in.** `MTL_CAPTURE_ENABLED=1` plus `F11` or
`WGPU_GPUTRACE_FRAME=N` records one frame; the panel carries the CPU and
submit-to-done figures and the batch counters.

**And a fourth thing came out of using them.** A frame-rate collapse from 75 to
15 fps was reported once and has not reproduced. Rather than guess, the frame
counters were extended into a **slow-frame recorder**: when a frame exceeds
three times the running average and 25 ms, it prints that frame's counters and
the previous eight frames. Every counter is zero in a steady frame, so whichever
is not zero names the cause — and all zeros means the cost was outside this
process, which eliminates the rest. See **The unreproduced stall** below.

---

### N3. A headless context — *(capture half landed)*

**Landed: screen capture.** `wgpuRequestFrameCapture` / `wgpuTakeFrameCapture`
on `wgpuContext.h`, `CopySrc` permanent on the surface and on render targets,
F12 for a human and `WGPU_SCREENSHOT_FRAME=N` for everything else. The library
hands back tightly packed RGBA and nothing else — no path, no encoder — with
the 256-byte row padding undone and the BGRA swizzle applied, because both are
the copy's rules and not the caller's problem.

That retires the debt this item was really about: the capability had been
rebuilt as throwaway scaffolding five times, and the fifth time it was lost
mid-task to a cleared scratchpad and had to be rewritten before the work could
continue.

**Substituted, deliberately: `WGPU_OFFSCREEN=1` hides the window** rather than
removing it. With the frame trigger that is a scriptable capture of a real
frame, ImGui included, with nothing appearing on screen — which is what the
verification norm in `AGENTS.md` actually needs.

**What remains is a genuinely surfaceless context, and it is not mostly
renderer work.** The renderer half is tractable: eight uses of `g.surface`,
each with an obvious offscreen branch — no `getCurrentTexture`, no `present`, a
texture as the only attachment. The application half is the real cost.
`glfwMain` is built around a window, ImGui's GLFW backend requires one, and the
game reads input through `platform::`, which is wired to GLFW callbacks. Doing
this means stubbing input and bypassing ImGui: app restructuring on top of a
renderer change.

**So the honest question before starting it is what it buys that a hidden
window does not**, and the answer is one thing: running on a machine with no
window system — CI, a container, a remote box. This project has no such
machine today. Until it does, this is a solution without a problem, and the
comment in `glfwMain.cpp` records the gap so nobody assumes it was finished.

**LearnWebGPU:** [Screen capture](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/screen-capture.html) (WIP) · [Headless context](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/headless.html)

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
value lets draws be reordered by _texture_ without changing what the player
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

**F1. ~~Additive bullets~~ — landed, and not the way this was written.** _(game)_
Making the bullet *sprite* additive was built, looked at, and reverted: it
replaced the art rather than lighting it, and `Bullet::render`'s five
overlapping trail quads summed to 4x and clipped the sprite into a featureless
white blob. What shipped instead is a generated capsule glow drawn *underneath*
the sprite, which stays in alpha and stays crisp — plasma for the player, ice
for the enemy, which land almost exactly on the existing atlas art.

The lesson generalised into outline 14: additive is nearly invisible on this
game's existing art, because every texture in it is binary alpha — 0% partially
transparent pixels — so re-blending only changes which equation a fully opaque
pixel goes through. Additive earns its place on soft-edged, overlapping content
over a dark ground, which had to be *generated* because none existed.

**F2. A post-process chain on the world target.** _(library mechanism, game policy — the split R3 made)_ Milestone 10 built the
machinery and then used it twice (render scale, HUD shake). A damage vignette, a
chromatic-aberration hit flash, a warp on death — each is a different _shader_
over the same vertex format and the same attachment. It separates pipeline from
shader from target in a way a single-shader renderer cannot.

**The wanted feature that lands here: a refraction cloak.** Not the fade-out
kind — that is one float of vertex alpha on the existing pipeline and needs
nothing from this item — but the shimmer, where the background _warps_ behind
the ship rather than showing through it. That has to sample the scene behind
the ship, which means the world goes to a render target and a shader offsets
its UVs. A blend mode cannot do it: blending combines a fragment with the
destination, it cannot _read_ the destination and move it, and core WebGPU has
no framebuffer fetch (outline 14).

So this item's real prerequisite is naming it: **the pipeline key has to grow a
shader.** Today it is `(format, blend)` because there is one shader. The first
effect with its own WGSL is what extends it, and a refraction cloak is that
effect. Small change — the key is a two-field struct with a linear scan behind
it — but it should be a deliberate one rather than something discovered while
writing a shader.

**F3. Parallax background layers through the camera stack.** _(library: the camera behaviour from R5; game: which layers and at what depth)_ Milestone 6b built
dynamic uniform offsets and `pushCamera` / `popCamera`; the game uses one world
camera and one HUD camera. Three layers at 0.3x / 0.6x / 1.0x needs no new API
at all, but exercises the slot stride, the run splitting and `getViewRect` under
real pressure — and it is the cheapest thing here that makes the game look
better. It is also the first real consumer of R5, and the honest test of
whether that camera design is general or just rearranged.

**F4. Present mode from the debug window.** _(library setting, app control)_ `configureSurface` picks one.
Switching Fifo / Immediate / Mailbox at runtime and watching what it does to
N2's numbers is a few lines, and it is the clearest way to learn what the
surface actually is.

**Landed outside this list, on request:** the engine plume
(`gameLayer/shipThruster`), the shield bubble (`gameLayer/shipShield`) and the
bullet glow (`gameLayer/bulletGlow`). None were roadmap items; all three are
the same pattern, which is worth naming because a fourth would be too. Each
generates its own gradient texture at init rather than loading art — a radial
disc, a fresnel sphere plus a ring, a capsule distance field — and each puts
the shape in the texture's alpha and the intensity in the vertex colour.

**Three modules now hand-roll a gradient generator.** A `wgpu2d` helper for
generated textures would serve all three, and is the obvious refactor when a
fourth arrives. Not done: three is where a pattern becomes visible, four is
where extracting it stops being speculative.

**F6. Effect shaders, with parameters.** _(library)_ The renderer can draw a
textured quad tinted by one colour. That is the whole vocabulary: `quad.wgsl`
is `in.color * textureSample(...)`, and the only per-quad channel is four
floats of vertex colour. Everything the shield could become runs into that wall
at once, so this is one item rather than several.

**What the shield wants, and what each thing actually needs:**

| want                                     | needs                                                         |
| ---------------------------------------- | ------------------------------------------------------------- |
| look _spherical_ rather than like a ring | **nothing new** — see below                                   |
| a specular highlight that tracks a light | nothing new: rotate the quad, the highlight rotates with it   |
| ripple outward from an impact point      | a shader + per-draw parameters (point, elapsed)               |
| several overlapping ripples              | the above, with an array — a uniform or storage buffer        |
| a dissolve / shatter on depletion        | a shader + one threshold parameter + a noise texture          |
| fragments flying apart                   | many quads with per-quad state — N5, or F5's machinery        |
| refraction, heat haze                    | F2: it has to _read_ the scene, so it needs the render target |

**The rounded look needs no renderer change at all, and is worth doing first.**
Fresnel — the reason a bubble looks like a sphere — is, for a screen-facing
sphere, a pure function of distance from the centre: the normal tilts as
`Nz = sqrt(1 - r²)` and the rim term is `(1 - Nz)^p`. A radial texture already
stores exactly "a function of distance from the centre", so a spherical-looking
bubble is a change to the texture generator in `shipShield.cpp`, not to the
pipeline. The same is true of a baked specular highlight, and rotating the quad
rotates the highlight for free.

**What actually needs building is two capabilities:**

1. **A shader per effect.** The pipeline key is `(format, blend)` because there
   is one shader. It grows a shader entry, and effects bring their own WGSL.
   F2 needs this too — it is the shared prerequisite, and whichever of the two
   is done first should build it.
2. **A per-draw parameter channel.** An impact point and an elapsed time are
   not a colour, and smuggling them through the vertex colour would cost the
   colour. Two honest options, and they are not new machinery: per-instance
   attributes (N5 — this is what instance data _is_), or a dynamic-offset
   uniform slot, which is exactly what milestone 6b already built for the
   camera. Either way a time uniform falls out for free.

**F5. Capstone: a GPU-driven starfield.** _(library: the compute-driven instanced particle system; game: that they are stars)_ Star positions in a storage buffer,
advanced each frame by a compute shader, drawn as instanced quads that read that
buffer. Combines N4, N5 and storage buffers — none of which the port has — into
one thing the game visibly gains, replacing a tiled texture with a particle
count the CPU batch could not carry. Also the natural first place to exceed a
default limit and have to fill in `requiredLimits`.
