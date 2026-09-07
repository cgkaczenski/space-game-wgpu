#pragma once

# Working agreements for coding agents

Read this before touching the repo. Claude Code and other agents that read
`AGENTS.md` pick it up automatically.

## Git: never commit unless asked

**Leave finished work uncommitted in the working tree.** The author reviews the
diff before it becomes history; a commit made on your own initiative takes that
away, even when the work itself is correct and even when previous, similar work
in the same session was committed on request.

- Do not `git commit` unless the author says so in that message. "Proceed",
  "go ahead", "do milestone N" are instructions to write code, not to commit.
- Permission to commit is per-commit. Being asked to commit one change does not
  authorize committing the next one.
- Never `git push`, force-push, rebase, amend, or reset without being asked.
- Never commit on `master`. Work happens on a branch (currently `webgpu-port`).
- When work is done, say what changed and where, and let the author look. If a
  commit seems due, offer one — with the message you would use — and wait.
- If you have already committed something unasked, say so plainly and offer
  `git reset --soft <last-approved-commit>` so the change goes back to being a
  reviewable diff.

Related: don't quietly delete or revert the author's uncommitted edits, and read
a file before patching it — the author edits committed files between sessions.

## What the project is

A 2D space shooter (GLFW + WebGPU). The render layer was ported from OpenGL +
gl2d to WebGPU (wgpu-native on Metal) as a deliberate learning exercise. The
concepts, and where each one landed, are in `docs/webgpu-learning-outline.md`.
Read the outline first — it is the map. Planned work, and the reasoning behind
it, is in `docs/roadmap.md`.

## Rules the author set for the port

- **One milestone per session.** Stop and wait after each one.
- **Explain the concepts and the API shape before writing code**, and leave room
  for questions. The author is learning graphics through this port; a correct
  patch with no explanation is a failed answer.
- **Never write wgpu API calls from memory.** The C API churns between releases.
  Read the vendored headers:
  - `build/_deps/wgpu-macos-x86_64-release-src/include/webgpu/webgpu.h`
  - `.../include/webgpu/wgpu.h` (wgpu-native extensions)
  - `build/_deps/webgpu-distribution-src/wgpu-native/include/webgpu/webgpu.hpp`
- **When something is broken, list candidate causes before proposing a fix.**

## The boundary: four homes

`wgpu2d` can be lifted out of this repo and used by another project, the way
[gl2d](https://github.com/meemknight/gl2d) is: it is a CMake target with its
own shader, no window library, and no way to include this game's headers. What
is still only a rule is stated per-bullet below.

- **Nothing in `src/render/` or `src/engine/` may know what this game is.** No
  ships, no health, no enemies, no `RESOURCES_PATH`. This one is enforced: the
  targets do not carry `include/gameLayer/`, so trying fails to compile.
- **Ask of every new file: would another game want this?** There are four
  homes, and the answer picks one:

  | Home | Holds | Test |
  |---|---|---|
  | `src/render/` | the drawing library | generic, reusable, game-agnostic |
  | `src/engine/` | reusable gameplay systems | could another game use it unedited? |
  | `src/gameLayer/` | this game | ships, HUD content, tuning constants |
  | `src/platform/` | the app shell | window, loop, ImGui wiring |

  `src/render/` and `src/engine/` are CMake targets, not just folders, and
  neither can include this game's headers — that fails to compile. The
  sideways direction is not enforced: they can include each other's, because
  both sit under one `include/` root. Movement, inventory, combat and AI belong
  in `engine/` beside collision and `cameraFollow`.

  `hudShake.cpp` was the standing counter-example: a mechanism worth keeping in
  the library, named and tuned for one game's HUD. R3/R4 split it — `LayerEffect`
  in `wgpu2d` is the mechanism, `gameLayer/hud` is the policy.
- **A feature is a mechanism plus a policy.** The mechanism is parameterized and
  knows nothing about this game; the policy is a struct of constants and the
  wiring. They go in different files, and usually different homes. `LayerEffect`
  / HUD shake, `Camera`, `collisionSystem` and movement are all instances — see
  **The shape features take** in `docs/roadmap.md`.
- **Features are self-contained or they are not features.** A HUD element, a
  camera behaviour or a post-process effect should be a pair of files another
  project can take or leave — not a block inlined in `gameLogic`.
- **The game includes `render/wgpu2d.h` and nothing else from `render/`.**
  New drawing capabilities (`BlendMode`, `LayerEffect`) go in that header.
  Matching gl2d is how the port landed, not a ceiling on the API.
  `wgpuContext.h` belongs to the platform layer; `wgpuFrame.h` is internal to
  the renderer; `wgpuImgui.h` / `wgpuMetalLayer.h` are app.
- Not everything here is meant to travel. Say which of the three homes a change
  lands in before writing it, and if it lands in `render/`, say what makes it
  general.

## Build and run

One configuration. WebGPU is the only renderer.

```bash
cmake -S . -B build
cmake --build build -j8
```

```bash
./build/spaceGame
WGPU_RENDER_SCALE=0.25 ./build/spaceGame   # low-res target, upscaled
```

The game runs until its window is closed, so cap it when running it yourself —
macOS has no `timeout`:

```bash
perl -e 'alarm 8; exec @ARGV' ./build/spaceGame
```

Redirected stdout is fully buffered; the renderer's reports flush explicitly, so
console evidence survives being killed.

## Verifying render work

`screencapture` from an agent session returns only the desktop wallpaper (the
terminal lacks Screen Recording permission), so **do not claim to have looked at
the window.** Two honest options:

- Console evidence: surface configuration, texture and pipeline creation, first
  flush, "first frame presented", absence of validation errors.
- A GPU readback: draw a fixed, time-independent scene and copy the frame back
  to the CPU (`copyTextureToBuffer` + `mapAsync`), then compare numerically.
  This is how the milestone 7-parity check was done and it is stronger than a
  screenshot; write PNGs and send them to the author to look at.

Temporary verification scaffolding is fine — say that it is temporary, keep it
out of the commit, and remove it when done.

## Layering

| File | Role |
|---|---|
| `include/render/wgpu2d.h` | the drawing library: sprites, cameras, targets, layer effects |
| `include/render/wgpuContext.h` | what the platform layer sees: init / begin / end / shutdown, no WebGPU types |
| `include/render/wgpuFrame.h` | what other render TUs see: device, queue, pass, texture bind groups |
| `src/render/wgpuContext.cpp` | instance through batch flush |
| `src/render/layerEffect.cpp` | `LayerEffect` implementation |
| `src/engine/` | reusable gameplay systems: collision, camera behaviours |
| `src/gameLayer/hud.{h,cpp}` | this game's HUD, including the shake's feel |
| `src/render/wgpuImgui.cpp` | the hand-written ImGui renderer backend |
| `src/platform/glfwMain.cpp` | window, frame bracket, ImGui wiring |

`wgpu2d` started by mirroring gl2d's signatures so the game could switch by
include. New drawing APIs belong in the same header; implementation can stay in
its own `.cpp`. Vertices stay in world pixels and the camera is a matrix on the
GPU.
