# WebGPU learning outline

A table of contents for the render port. Each section is one concept cluster: LearnWebGPU chapter, where it landed in this repo, and the commit that introduced it. Expand in place later.

The game-facing API is `wgpu2d` (`include/render/wgpu2d.h`). Almost all GPU work lives in `src/render/wgpuContext.cpp`, with the ImGui half in `src/render/wgpuImgui.cpp`. The GLFW loop in `src/platform/glfwMain.cpp` only owns init / begin / end / ImGui.

**The rule the port follows:** mirror gl2d's *signatures* name for name, never its implementation. gl2d transforms every corner on the CPU and hands the shader NDC; here the vertex buffer keeps world pixels and the camera is a matrix on the GPU. That is why the game files changed by an include and a namespace only.

Guide: [Learn WebGPU for C++](https://eliemichel.github.io/LearnWebGPU/index.html). Use the **With webgpu.hpp** tab. Our wrapper is compiled in one TU: `src/render/webgpuImpl.cpp`.

The guide is 3D; this project is a 2D sprite batch. Same objects, different use.

---

## How to read this

Each block is: **concepts → LearnWebGPU chapters → where it landed → commit**.

---

## 0. Stack and window (before drawing)

**Concepts:** CMake + FetchContent, wgpu-native vs Dawn, GLFW with `GLFW_NO_API`, glfw3webgpu surface, C++ wrapper vs `webgpu.h`.

**LearnWebGPU:** [Project setup](https://eliemichel.github.io/LearnWebGPU/getting-started/project-setup.html) · [Hello WebGPU](https://eliemichel.github.io/LearnWebGPU/getting-started/hello-webgpu.html) · [Opening a window](https://eliemichel.github.io/LearnWebGPU/getting-started/opening-a-window.html) · [C++ idioms](https://eliemichel.github.io/LearnWebGPU/getting-started/cpp-idioms.html) (the wrapper, `Default`, `StringView`)

**Code:** `CMakeLists.txt` (FetchContent of WebGPU-distribution + glfw3webgpu) · `src/platform/glfwMain.cpp` window hints · `src/render/webgpuImpl.cpp`

**Pins:** WebGPU-distribution `v0.3.0-gamma`, which fetches prebuilt **wgpu-native v24.0.3.1** · glfw3webgpu `v1.3.0-alpha` · GLFW 3.4 (`thirdparty/glfw-3.4`; the unused 3.3.2 tree is still in the repo). The two tags were tested together on the GLFW 3.4 line.

**Read the header, not your memory.** The C API churns between wgpu-native releases (string views instead of `const char*`, surface configuration instead of swapchains, `ShaderSourceWGSL` instead of the old chained descriptors). The vendored copies:

- `build/_deps/wgpu-macos-x86_64-release-src/include/webgpu/webgpu.h` — the spec header
- `.../include/webgpu/wgpu.h` — wgpu-native's own extensions
- `build/_deps/webgpu-distribution-src/wgpu-native/include/webgpu/webgpu.hpp` — the C++ wrapper

**Build directory:** `build/`

**Commits:** `43b3255` GLFW 3.4 · `5861a4d` 1a · `21d6524` wrapper

---

## 1a. Instance, surface, adapter

**Concepts:** Instance, surface (window → GPU), adapter (which GPU), async `requestAdapter` + process events, adapter limits/properties.

**LearnWebGPU:** [The Adapter](https://eliemichel.github.io/LearnWebGPU/getting-started/adapter-and-device/the-adapter.html)

**Code:** `render::wgpuInit` · `onAdapterRequest` · `printAdapter` · `glfwCreateWindowWGPUSurface`

**Commit:** `5861a4d`

---

## 1b. Device, queue, surface config, clear, present

**Concepts:** Device + lost/error callbacks, queue, **surface configuration** (the guide’s old swapchain), command encoder, render pass clear, submit, present. Framebuffer size vs window size (Retina).

**LearnWebGPU:** [The Device](https://eliemichel.github.io/LearnWebGPU/getting-started/adapter-and-device/the-device.html) · [The Command Queue](https://eliemichel.github.io/LearnWebGPU/getting-started/the-command-queue.html) · [First Color](https://eliemichel.github.io/LearnWebGPU/getting-started/first-color.html)

**Code:** `onDeviceRequest` / `onUncapturedError` / `onDeviceLost` · `configureSurface` · `wgpuBeginFrame` / `wgpuEndFrame` · `Context` fields tagged `1a` / `1b`

**Commit:** `ad94126`

---

## 2. Shader module + render pipeline (one triangle)

**Concepts:** WGSL `@vertex` / `@fragment`, shader module from source, **render pipeline** as immutable GPU state (topology, blend, color target format). Hardcoded verts in the shader at this step; later moved to a buffer.

**LearnWebGPU:** [Hello Triangle](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/hello-triangle.html)

**Code:** `createShaderModuleFromFile` · `createQuadPipeline` · `resources/shaders/quad.wgsl`

**Commit:** `8fee100`

---

## 3. Vertex buffers and attributes (colored quad)

**Concepts:** GPU buffer, `queue.writeBuffer`, vertex layout (stride, `@location`, formats), interleaved vs separate attributes. Two triangles = one quad, six vertices, no index buffer (ImGui later adds indices).

**LearnWebGPU:** [Playing with buffers](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/input-geometry/playing-with-buffers.html) · [A first Vertex Attribute](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/input-geometry/a-first-vertex-attribute.html) · [Multiple Attributes](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/input-geometry/multiple-attributes.html)

**Code:** `struct Vertex` · vertex layout inside `createQuadPipeline` · `ensureVertexBufferCapacity` · `pushQuad`

**Commit:** `3872e7b`

---

## 4. Textures, samplers, bind groups (textured quad)

**Concepts:** Texture vs buffer, `writeTexture`, texture view, sampler (filter / address / mips), **bind group layout** = shader contract (`@group` / `@binding`), bind group = the actual resources. Y/UV origin: WebGPU top-left; gl2d was bottom-left. Converted at the API boundary, no flip on load.

**LearnWebGPU:** [A first texture](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/texturing/a-first-texture.html) · [Texture mapping](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/texturing/texture-mapping.html) · [Sampler](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/texturing/sampler.html) · [Loading from file](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/texturing/loading-from-file.html)

**Code:** `createSamplers` · `createLayouts` (group 0) · `createTextureFromPixels` · `Texture::loadFromFile` · `quad.wgsl` group 0

**Commit:** `5f83600`

---

## 5. Uniforms, projection, resize (pixel-space camera)

**Concepts:** Uniform buffer, bind group 1, orthographic matrix in the vertex shader (world pixels, y-down → clip space). Resize = reconfigure surface from **framebuffer** size, and on Metal the surface never reports itself `Outdated`, so the size is compared every frame in `wgpuBeginFrame` instead of waiting to be told (symptom when missing: sprites stretch and the visible world does not change). Color space: pick a non-sRGB surface format; pin the Metal layer.

**LearnWebGPU:** [A first uniform](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/shader-uniforms/a-first-uniform.html) · [Multiple uniforms](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/shader-uniforms/multiple-uniforms.html) · [Transformation matrices](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/3d-meshes/transformation-matrices.html) · [Projection matrices](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/3d-meshes/projection-matrices.html) · [Resizing the window](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/some-interaction/resizing-window.html) · [Camera control](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/some-interaction/camera-control.html)

**Code:** `buildViewProj` · `CameraUniforms` · `configureSurface` called from `wgpuBeginFrame` · `include/render/wgpuMetalLayer.h`

**Commits:** `5508972` camera · `095afaf` Metal sRGB pin

---

## 6a. CPU batch, one texture, blend

**Concepts:** Growable vertex buffer, accumulate quads on the CPU, one upload + one draw per flush. Blend state matching gl2d (`SrcAlpha` / `OneMinusSrcAlpha`, alpha `One` / `OneMinusSrcAlpha`). Origin, rotation, per-quad color still CPU-side.

**LearnWebGPU:** No dedicated chapter — Hello Triangle + buffers + pipeline blend. Closest “many draws” idea is later instancing (TODO in the guide).

**Code:** `batchVertices` · `pushQuad` · `flushBatch` · blend block in `createQuadPipeline` · `Renderer2D` in `include/render/wgpu2d.h`

**Commit:** `086bf22`

---

## 6b. Texture runs + camera stack (dynamic uniforms)

**Concepts:** Break the batch into **runs** (same texture + same camera). `setBindGroup` per run. **Dynamic uniform offsets** so several cameras share one buffer (`minUniformBufferOffsetAlignment`, slot stride). `pushCamera` / `popCamera`.

**LearnWebGPU:** [Dynamic uniforms](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/shader-uniforms/dynamic-uniforms.html)

**Code:** `ensureCameraSlotCapacity` · `batchQuadTextures` / `batchQuadCameras` / `frameCameras` · run loop in `flushBatch` · `Renderer2D::pushCamera` / `popCamera` / `getViewRect`

**Commit:** `f013edc`

---

## 7. Atlas, padded loader, mipmaps, game wiring

**Concepts:** Atlas UV math (CPU only). Pixel padding so filtering does not bleed cells. **CPU mipmaps** (WebGPU has no `glGenerateMipmap`; the guide’s GPU version is compute). Game files switched onto `wgpu2d` under gl2d's signatures.

**LearnWebGPU:** [Loading from file](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/texturing/loading-from-file.html) (stb_image) · [Mipmap Generation](https://eliemichel.github.io/LearnWebGPU/basic-compute/image-processing/mipmap-generation.html) (CPU half of that chapter, not the compute pass)

**Code:** `downsampleRGBA8` · `createPaddedTextureFromFileData` · `computeTextureAtlas*` · `Texture::loadFromFileWithPixelPadding` · `include/render/wgpu2d.h` · game: `include/gameLayer/{tiledRenderer,bullet,enemy}.h` and `src/gameLayer/{gameLayer,tiledRenderer,bullet,enemy}.cpp`

**Commit:** `b01187f`

**Still open in the plan:** 7-parity screenshot comparison.

---

## 8. ImGui on WebGPU

**Concepts:** Platform backend (GLFW) vs renderer backend (us). Same render pass, after the game. Index buffer (`drawIndexed`, with `ImDrawCmd::VtxOffset` as `baseVertex` so 16-bit indices survive past 64k vertices), scissor rectangles, packed `Unorm8x4` color. Reuse group-0 texture layout; own group-1 ortho, static, no dynamic offset.

Details worth remembering: ImGui positions are **screen points**, scissors are **framebuffer pixels**, so clip rectangles are the only thing multiplied by `FramebufferScale` (2× on the Retina panel) and clamped to the surface. `writeBuffer` copies whole 4-byte words, so an odd 16-bit index count is padded. `ImTextureID` is just a `wgpu2d::Texture` id, which is why the font atlas goes through `createFromBuffer` and its bind group comes from the texture registry. Colors are written unconverted to the non-sRGB surface, exactly as `imgui_impl_opengl3` writes them.

Docking is core ImGui and renderer-independent, so it is on (`DockSpaceOverViewport` with `PassthruCentralNode`, which keeps the host window and the empty central node from drawing over the game); `WindowBg` alpha is zeroed so the debug window's body is transparent. **Multi-viewport stays off** — every torn-off window would need its own WebGPU surface.

Bundled `imgui_impl_wgpu` (ImGui 1.89.5) is too old for wgpu-native v24: SPIR-V shader modules, `WGPUProgrammableStageDescriptor`, `const char*` labels. Upgrading ImGui would have churned the OpenGL path too, so the backend is hand written.

**LearnWebGPU:** [Simple GUI](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/some-interaction/simple-gui.html) (their `imgui_impl_wgpu`; this port reimplements the same job) · [Index Buffer](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/input-geometry/index-buffer.html)

**Code:** `include/render/wgpuImgui.h` · `src/render/wgpuImgui.cpp` · `include/render/wgpuFrame.h` (shared device/pass/texture groups) · `resources/shaders/imgui.wgsl` · `src/platform/glfwMain.cpp` around `wgpuImgui*`

**Commit:** `fa0e022`

---

## 10. Render targets (and a low-res render scale)

**Concepts:** There is no framebuffer object in WebGPU: a render pass's colour attachment *is* a texture view, so drawing to the screen and drawing to a texture are one operation with a different attachment. `wgpu2d::FrameBuffer` mirrors gl2d's shape over that, and its `texture` is an ordinary handle, so the result is drawn back with `renderRectangle` and needs no new pipeline or shader.

Three constraints that only show up here:

- **A pipeline is bound to its target's format.** `createQuadPipeline` bakes `colorTarget.format`, so a render target must use the surface's format for the sprite pipeline to draw into it. Wrong format is a validation error, not a wrong picture. *(Superseded in 12: the pipeline cache is keyed on the format, so a target may now use any format. 11 also corrected the second half — the mismatch is **not** reported at creation; it is reported when the pipeline meets the attachment inside a pass.)*
- **A pass belongs to one attachment and passes cannot nest.** `ensurePassBegun(target)` ends the open pass when the target changes and chooses the load operation: the surface and the low-res stand-in clear on their first pass of a frame, a user's FrameBuffer loads its contents like gl2d's. `wgpuCurrentRenderPass` therefore means *the surface* specifically, and requesting it while the frame sits in the low-res target composites that first, so the UI lands on top of the world.
- **More than one flush per frame means more than one buffer slice.** `writeBuffer` is ordered against the submit, not against the recorded draws, so two flushes writing at offset 0 leave the first pass's draws reading the second flush's data. Each flush appends its own slice of the vertex buffer and its own camera slots, both reset in `wgpuBeginFrame`. The symptom of getting this wrong was the whole atlas stretched across the target with the scene one quad out of step.

Also worth knowing: colours are straight, not premultiplied, alpha. An opaque scene round-trips through a same-size target byte-identically; a translucent draw that goes through a target and is then composited is multiplied by its coverage twice and comes out darker. *(Fixed in 12. The diagnosis here was half right: the source is straight alpha, but what a target **holds** is premultiplied, and the double multiply was in the composite, not the draw.)*

`WGPU_RENDER_SCALE=0.25` uses all of it for something real: the world is drawn into a quarter-size target and upscaled with nearest filtering, while the projection keeps using the surface's dimensions so framing, HUD layout and mouse mapping are unchanged. ImGui still draws to the surface at native size.

**LearnWebGPU:** no dedicated chapter — it is the pass and attachment machinery from [First Color](https://eliemichel.github.io/LearnWebGPU/getting-started/first-color.html) and [Hello Triangle](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/hello-triangle.html) pointed at a texture from [A first texture](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/texturing/a-first-texture.html).

**Code:** `createRenderTarget` · `ensureScaledTarget` · `ensurePassBegun(target)` · `compositeScaledTarget` · `flushBatch(target)` · `FrameBuffer` and `Renderer2D::flushFBO` in `include/render/wgpu2d.h`

**Commit:** `b15b5bb`

---

## 11. Error scopes and debug groups

**Concepts:** The device's uncaptured-error callback is a **catch-all** — it reports errors no scope claimed, cannot say which call produced them, and can arrive after the null handle it explains. An **error scope** is a stack on the device: `pushErrorScope(filter)` claims errors of one class, `popErrorScope` reports the first one captured. What it buys is not the text but **attribution**. **Debug groups** are the same idea one level down: a label names an object, a group names a *span of recorded commands*, which is what turns a GPU capture from a flat list of draws into a tree.

Three things this turned up that are worth more than the feature itself:

- **A rejected pipeline is not null.** `createRenderPipeline` returns a live handle in an *invalid* state, so `if (!pipeline)` passes and the failure only appears later as a per-frame cascade — `setPipeline` reports it, then `draw` reports "Render pipeline must be set". Only a scope can tell the difference at the point of creation.
- **Popping a scope is asynchronous.** The callback fires inside `instance.processEvents()`, the same mechanism `requestAdapter` and `requestDevice` use, so draining it costs a stall. That single fact decides the whole design: scopes wrap object **creation** and never per-frame recording. The two that sit on per-frame functions are past the early return that makes them growth steps.
- **A debug group must balance before its pass ends**, and must not span a target switch — `ensurePassBegun` ends the open pass whenever the target changes. `wgpuImguiRenderDrawData` also returns early between where a group would be pushed and popped. Both are why these are RAII guards rather than call pairs.

**What was already there, contrary to the plan:** this milestone was written believing a null handle was the whole diagnosis. It was not. wgpu-native's logger (`wgpuSetLogCallback` + `wgpuSetLogLevel`) and the uncaptured-error callback were both wired from the start, and labels were essentially complete — 27 of them across the two render TUs. Validation *text* already reached stderr. Only the attribution was missing. The lesson was cheap and worth having: check the code before writing the plan.

**LearnWebGPU:** [Debugging](https://eliemichel.github.io/LearnWebGPU/appendices/debugging.html) — still WIP; error scopes are covered, the capture-tool half is thin.

**Code:** `ErrorScope` and `DebugGroup` in `include/render/wgpuFrame.h` (header, so both render TUs share them) · `wgpuBeginErrorScope` / `wgpuEndErrorScope` / `onPopErrorScope` in `src/render/wgpuContext.cpp` · scopes on the creation sites in both TUs · groups in `flushBatch` and `wgpuImguiRenderDrawData` · `WGPU_LOG_LEVEL`

**Commit:** `d779a1c`

---

## 12. A pipeline cache, blend modes, and a correct composite

**Concepts:** Blending and the colour target's format are **baked into a render pipeline** and cannot be set on a pass, so "the pipeline" was always really one per (format, blend) pair. WebGPU has no pipeline-cache object — Vulkan does, `webgpu.h` does not — so this is a small vector of entries built on demand and scanned linearly at run boundaries. `flushBatch`'s run key gains a third component beside texture and camera: texture and camera change which *resources* are bound, blend changes which *pipeline* is bound.

**The key is deliberately two fields.** Sample count, depth state and the vertex layout all belong in it eventually; adding a field then is a few lines, and designing around four futures at once is how the wrong key gets built.

Both halves have a real consumer:

- **Blend.** `wgpu2d::BlendMode { Alpha, Additive, Premultiplied }` — an addition to gl2d's shape, not a mirror of it, since gl2d had one blend state for the whole program.
- **Format.** Milestone 10 forced a render target to use the surface's format because there was one pipeline built for it. Keying on the format lifts that, and makes the rule structural instead of a comment.

Two consequences of 11 are built in: the shader module is compiled once and shared (with one pipeline it could be released immediately; with N it cannot), and the cache **validates each variant with an error scope before storing it**, because a rejected pipeline is non-null and a cached invalid one would be re-served every frame.

**The composite was wrong, and not in the way milestone 10 described.** Drawing `(C, a)` into a cleared target leaves `rgb = C·a`, `a = a` — the alpha channel is `One / OneMinusSrcAlpha` in every mode, so **what a target holds is premultiplied whatever the source was**. Compositing it back with alpha blending applies coverage a second time: `C·a²`. `BlendMode::Premultiplied` (`src + dst·(1-a)`) is what compositing wants. `hudShake` is where it showed — its target is cleared transparent, so the HUD darkened for the length of every shake, and only during a shake. `compositeScaledTarget` is numerically identical today because the world target is cleared opaque and `a = 1` makes both modes agree; it is now correct rather than accidentally correct.

Measured rather than argued, with temporary `copyTextureToBuffer` + `mapAsync` scaffolding — 50% grey at alpha 0.5 over opaque black, centre pixel of a 64×64 target:

| | value |
|---|---|
| direct | 64 |
| via target, premultiplied | 64 |
| via target, alpha (the old path) | 32 |

Predicted `0.25 → 64` and `0.125 → 32`. Exactly half, which is the double multiply.

**Still short of exact:** the source is not premultiplied, so overlapping translucent draws *inside* a target are very close rather than exactly right. Fixing that means the shader emitting `rgb·a` on every draw.

**LearnWebGPU:** no chapter — this is the pipeline immutability from [Hello Triangle](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/hello-triangle.html) taken to its conclusion once one pipeline stopped being enough.

**Code:** `PipelineKey` / `PipelineEntry` / `getQuadPipeline` / `createQuadPipelineVariant` · the run loop in `flushBatch` · `batchQuadBlends` · `BlendMode` and `setBlendMode` in `include/render/wgpu2d.h` · `compositeScaledTarget` · `src/render/hudShake.cpp`

**Commits:** `002516c` cache and run key · `54a76b2` premultiplied composite

---

## This machine, and what it does differently

Facts established while porting, all specific to an Intel Mac with an AMD Radeon Pro 560X on Metal via wgpu-native. None of them are bugs in the renderer; each one cost time before it was understood.

| What you see | What it is |
|---|---|
| Adapter vendor, architecture and description strings are empty, `vendorID` is 0 | Normal on Metal; wgpu-native does not fill them |
| `minUniformBufferOffsetAlignment` is 256 | Sets the per-camera slot stride in 6b (`maxBindGroups` 8, `maxVertexAttributes` 31) |
| Surface never reports `Outdated` after a resize; the layer stretches the old drawable | Metal keeps presenting the configured size — compare `glfwGetFramebufferSize` every frame (see 5) |
| Surface formats offered: `BGRA8UnormSrgb`, `BGRA8Unorm`, two 10/16-bit ones | The port picks `BGRA8Unorm` so nothing gamma-corrects behind gl2d's back |
| Fullscreen on one secondary monitor looks slightly lighter until the menu bar is revealed | That monitor's profile on macOS's direct-to-display path. Pinning the `CAMetalLayer` color space (`095afaf`) did not change it; the OpenGL build is always composited, so it never shows it. **Run parity screenshots windowed on the main display.** |
| Redirecting the binary's output loses the last lines | stdout is fully buffered when redirected — the reports flush explicitly. macOS has no `timeout`; `perl -e 'alarm N; exec @ARGV' ./spaceGame` works |

---

## Not in the port (guide chapters skipped)

Skipped on the way through the milestone path. Several turned out to have a real
2D use once the port was finished; those rows say where they are picked up again
in [`roadmap.md`](roadmap.md).

| Guide | Why skipped |
|---|---|
| Lighting, PBR, cube maps, 3D meshes | 2D — cube maps and PBR stay out for good; lighting comes back in a 2D form as N8 |
| Depth buffer | depth off — but see N7, where depth is a sort key rather than an occlusion test |
| Compute pipeline (except as mipmap reading) | CPU mips instead — see N4 |
| Instancing | one growable buffer + draw runs — see N5 |
| Render bundles, MSAA | same — MSAA is N6; render bundles stay marginal |
| Building for the Web | native Metal only; `wgpuMetalLayer.mm` and the wgpu-native pin both stand in the way |
| Milestone 9 (text) | skipped: gl2d's font path is almost all CPU (stb_truetype pack, glyph quads through the existing batch), and the game draws no gl2d text — glui is layout only and every string is ImGui |

---

## What comes next

Planned work — refactors, guide chapters still worth doing, and features that
would deepen what is here — lives in [`roadmap.md`](roadmap.md), not in this
file. The two have different lifetimes: a roadmap item is deleted when it lands,
and a milestone block appears here in its place. This file only ever grows, and
only ever describes things that exist.

---

## File cheat sheet

| File | Role |
|---|---|
| `include/render/wgpuContext.h` | platform: init / begin / end / shutdown |
| `include/render/wgpuFrame.h` | other render TUs: device, pass, texture bind groups |
| `include/render/wgpu2d.h` | gl2d-shaped game API |
| `src/render/wgpuContext.cpp` | instance → batch flush |
| `src/render/wgpuImgui.cpp` | ImGui geometry |
| `resources/shaders/quad.wgsl` | sprites |
| `resources/shaders/imgui.wgsl` | UI |
| `src/platform/glfwMain.cpp` | window + frame bracket |
| `src/render/webgpuImpl.cpp` | the one TU that defines the wrapper's bodies |
| `include/render/wgpuMetalLayer.h` / `.mm` | macOS: pin the `CAMetalLayer` color space |

## Reference shelf

Guide appendices that matter for a native wgpu-native port, none of them part of the milestone path: [Debugging](https://eliemichel.github.io/LearnWebGPU/appendices/debugging.html) · [Custom extensions with wgpu-native](https://eliemichel.github.io/LearnWebGPU/appendices/custom-extensions/with-wgpu-native.html) (what `wgpu.h` beside `webgpu.h` is for) · [Memory model](https://eliemichel.github.io/LearnWebGPU/appendices/memory-model.html) · [References](https://eliemichel.github.io/LearnWebGPU/appendices/references.html)
