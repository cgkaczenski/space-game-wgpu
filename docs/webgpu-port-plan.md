# WebGPU render port plan

Port the render layer from OpenGL + gl2d to WebGPU (wgpu-native, Metal on macOS), keeping GLFW for windowing.

Stack:

- [WebGPU-distribution](https://github.com/eliemichel/WebGPU-distribution) (CMake, fetches wgpu-native)
- [glfw3webgpu](https://github.com/eliemichel/glfw3webgpu) for surface creation
- [WebGPU-Cpp](https://github.com/eliemichel/WebGPU-Cpp) for the C++ wrapper
- Following [LearnWebGPU](https://eliemichel.github.io/LearnWebGPU/index.html)

Revised 2026-09-01 after review.

## Working rules

- One milestone per session. Stop and wait after each.
- Before writing code for a milestone, explain the concepts and the API shape first. Assume a graphics beginner.
- Do not touch gameplay, input, or simulation code. Render layer only.
- The wgpu-native C API changes often. Read `webgpu.h` in our checkout and the LearnWebGPU sources before writing calls. Never generate API code from memory.
- When something is broken, list candidate causes before proposing a fix.
- Work on a branch. Keep the OpenGL path building until the WebGPU path reaches parity.

## Inventory: where the code touches OpenGL and gl2d

Direct OpenGL calls in game code (four places):

- `src/platform/glfwMain.cpp` lines 309-333: GL 3.3 core window hints, context current, vsync, glad load.
- `src/platform/glfwMain.cpp` line 494: `glViewport` before ImGui draws.
- `src/gameLayer/gameLayer.cpp` lines 163-164: `glViewport` and `glClear` at frame start.
- Both files include `glad/glad.h`.

ImGui is bound to OpenGL through the OpenGL3 backend in `glfwMain.cpp`, with docking and multi-viewport enabled. The bundled ImGui (1.89.5 WIP) ships a WebGPU backend, but it targets an old `webgpu.h` (SPIR-V descriptors, C-string labels, renamed structs) and does not support multi-viewport. Expect to upgrade ImGui.

gl2d usage in game files:

- `gameLayer.cpp`: global renderer, 8 textures, 2 padded atlases, camera, clear, flush, screen-space health bar.
- `tiledRenderer.cpp`, `bullet.cpp`, `enemy.cpp`: take a renderer reference plus texture and atlas by gl2d type; call `renderRectangle`. Tiled renderer reads `getViewRect` for parallax.
- Headers `tiledRenderer.h`, `bullet.h`, `enemy.h` include `gl2d/gl2d.h`.

glui: the game uses only `Frame` and `Box`, which are pure layout math. Nothing to port.

Build: `CMakeLists.txt` adds and links glad, gl2d, glui. The imgui target compiles the OpenGL3 backend and links glad. Bundled GLFW is 3.3.2, which exposes the Cocoa native window handle glfw3webgpu needs.

Untouched by GL: platformInput, collisionSystem, otherPlatformFunctions, raudio, safeSave, profilerLib.

## Effective renderer interface (the subset the game uses)

- **Lifecycle.** Init once, create renderer, update window metrics each frame with the framebuffer size, clear at frame start, flush once at frame end.
- **Textures.** Load from file, optionally with pixel padding between atlas cells. RGBA8, flipped vertically at load, clamp-to-edge, nearest filtering with generated mipmaps. Size query after load.
- **Atlas.** `TextureAtlasPadding(cols, rows, texW, texH).get(x, y)` returns a UV rect. Pure math.
- **Camera.** Position, zoom, `follow(pos, speed, min, max, w, h)`, `pushCamera()` / `popCamera()`, `getViewRect()`. Rotation exists but is never set.
- **Draw.** `renderRectangle(rect, texture, color, origin, rotationDegrees, uvs)`. Circle outlines are lines, lines are rectangles, so every primitive is a quad.
- **Fixed state.** Blend: color `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`, alpha `ONE, ONE_MINUS_SRC_ALPHA`. Depth test off. Submission order preserved. One draw per run of consecutive quads sharing a texture. Three non-interleaved vertex arrays (position vec2, color vec4, uv vec2), six vertices per quad, no index buffer.

Not used by the game: fonts and text, framebuffer render targets, custom shaders, particle system, 9-patch, camera rotation.

Two facts that shape the port:

1. gl2d does all transformation on the CPU and its vertex shader is a pass-through receiving NDC. "Port the camera" means writing a camera for the first time.
2. The world is y-down in pixel units. gl2d negates y and flips images at load to work under GL's bottom-left texture origin. WebGPU's texture origin is top-left.

## Milestones

Each milestone is one session and ends with a visible result. Console output counts.

| # | Milestone | Ends with | Type |
|---|-----------|-----------|------|
| 1a | Deps and adapter | CMake builds with WebGPU-distribution, glfw3webgpu, WebGPU-Cpp behind a renderer option (default OFF). Adapter properties printed to console. | Learning |
| 1b | Clear color | Device, queue, surface, surface config, command encoder, render pass, submit, present. Window shows a solid color. | Learning |
| 2 | One triangle | WGSL shader module, render pipeline. Vertices hardcoded in the shader. | Learning |
| 3 | Colored quad | Vertex buffer, vertex layout, attributes, queue upload. Two triangles with per-vertex color. | Learning |
| 4 | Textured quad | stb_image load, texture write, sampler, bind group layout and bind group. Decide Y/UV orientation here. | Learning |
| 5 | Pixel-space camera | Uniform buffer with projection matrix applied in the vertex shader. Resize reconfigures the surface. Framebuffer size, not window size (Retina). gl2d's y-down, zoom-about-center, and `follow` semantics. | Mixed |
| 6a | Many quads, one texture | CPU vertex accumulation, one growable reusable vertex buffer, per-quad color, origin, rotation, UVs. gl2d blend state. | Game-specific |
| 6b | Runs across textures | Draw ranges per texture run, camera push/pop stack, submission order preserved. Several textures on screen. | Game-specific |
| 7 | Atlas, padded loader, game wiring | Port pixel-padding loader and padded-atlas math. CPU mipmap generation (WebGPU has no generateMipmap). Switch game files to the new renderer. Real game draws on WebGPU, no ImGui. | Game-specific |
| 7-parity | Screenshot comparison | Same scene on the OpenGL and WebGPU builds (spawns off, fixed setup), compared side by side for gamma, orientation, mipmap shimmer, blending. Milestone 7 is not done until this passes. | Game-specific |
| 8 | ImGui on WebGPU | Upgrade bundled ImGui to a release whose WebGPU backend supports wgpu-native. Drop multi-viewport on this path. Render ImGui in the same pass after the game batch. Full-app parity. | Game-specific |
| 9 | Text | Font atlas from stb_truetype, glyph quads. Unused by the game today. | Optional, learning |
| 10 | Render targets | Draw to an offscreen texture and sample it back. Unused by the game today. | Optional, learning |

## Integration approach

The new renderer mirrors gl2d's **signatures** name-for-name for the subset above, in a new namespace. Game files change only by includes and a namespace alias, selected by the CMake renderer option. Both paths keep building. gl2d's `Colors_*` macros need equivalents in the new header.

The new renderer must **not** mirror gl2d's implementation. Vertex buffers hold world-pixel positions and the camera is a uniform matrix on the GPU. gl2d's CPU transform and pass-through shader are not ported.

## Risks to keep in view

- **Surface format.** wgpu-native on Metal will likely offer an sRGB format first. gl2d does no gamma correction, so sRGB will make everything look darker or washed out. Check this before touching colors.
- **Orientation.** Load-time vertical flip plus WebGPU's top-left UV origin. Decided once in milestone 4.
- **Mipmaps.** Backgrounds render at zoom 0.5 with nearest filtering and mipmaps today. Without CPU-generated mips they will shimmer.
- **Header churn.** Current wgpu-native uses string-view labels and surface configuration instead of swapchains. Check every call against the vendored `webgpu.h`.
- **ImGui version.** The bundled WebGPU backend will need replacing. Multi-viewport goes away on the WebGPU path.
- **macOS build.** glfw3webgpu compiles its source as Objective-C on Apple and links Metal and QuartzCore. Its CMake handles this. First thing to check if 1a fails to link.
- **Retina.** Surface size must come from the framebuffer size, not the window size, and must be reconfigured on resize.
