# WebGPU learning outline

A table of contents for the render port. Each section is one concept cluster: LearnWebGPU chapter, where it landed in this repo, and the commit that introduced it. Expand in place later.

The game-facing API is `r2d` (`include/render/renderer.h`). Almost all GPU work lives in `src/render/wgpuContext.cpp`, with the ImGui half in `src/render/wgpuImgui.cpp`. The GLFW loop in `src/platform/glfwMain.cpp` only owns init / begin / end / ImGui.

**The rule the port follows:** mirror gl2d's *signatures* name for name, never its implementation. gl2d transforms every corner on the CPU and hands the shader NDC; here the vertex buffer keeps world pixels and the camera is a matrix on the GPU. That is why the game files changed by an include and a namespace only.

Guide: [Learn WebGPU for C++](https://eliemichel.github.io/LearnWebGPU/index.html). Use the **With webgpu.hpp** tab. Our wrapper is compiled in one TU: `src/render/webgpuImpl.cpp`.

The guide is 3D; this project is a 2D sprite batch. Same objects, different use.

Related: [webgpu-port-plan.md](webgpu-port-plan.md).

---

## How to read this

Each block is: **concepts → LearnWebGPU chapters → where it landed → commit**.

---

## 0. Stack and window (before drawing)

**Concepts:** CMake + FetchContent, wgpu-native vs Dawn, GLFW with `GLFW_NO_API`, glfw3webgpu surface, C++ wrapper vs `webgpu.h`.

**LearnWebGPU:** [Project setup](https://eliemichel.github.io/LearnWebGPU/getting-started/project-setup.html) · [Hello WebGPU](https://eliemichel.github.io/LearnWebGPU/getting-started/hello-webgpu.html) · [Opening a window](https://eliemichel.github.io/LearnWebGPU/getting-started/opening-a-window.html) · [C++ idioms](https://eliemichel.github.io/LearnWebGPU/getting-started/cpp-idioms.html) (the wrapper, `Default`, `StringView`)

**Code:** `CMakeLists.txt` (`RENDERER_WEBGPU`, FetchContent of WebGPU-distribution + glfw3webgpu) · `src/platform/glfwMain.cpp` window hints · `src/render/webgpuImpl.cpp`

**Pins:** WebGPU-distribution `v0.3.0-gamma`, which fetches prebuilt **wgpu-native v24.0.3.1** · glfw3webgpu `v1.3.0-alpha` · GLFW 3.4 (`thirdparty/glfw-3.4`; the unused 3.3.2 tree is still in the repo). The two tags were tested together on the GLFW 3.4 line.

**Read the header, not your memory.** The C API churns between wgpu-native releases (string views instead of `const char*`, surface configuration instead of swapchains, `ShaderSourceWGSL` instead of the old chained descriptors). The vendored copies:

- `build-webgpu/_deps/wgpu-macos-x86_64-release-src/include/webgpu/webgpu.h` — the spec header
- `.../include/webgpu/wgpu.h` — wgpu-native's own extensions
- `build-webgpu/_deps/webgpu-distribution-src/wgpu-native/include/webgpu/webgpu.hpp` — the C++ wrapper

**Build directories:** `build/` is the OpenGL configuration, `build-webgpu/` is `-DRENDERER_WEBGPU=ON`. Both stay working until the port reaches parity.

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

**Concepts:** Atlas UV math (CPU only). Pixel padding so filtering does not bleed cells. **CPU mipmaps** (WebGPU has no `glGenerateMipmap`; the guide’s GPU version is compute). Game files switch via `r2d`, not a new API.

**LearnWebGPU:** [Loading from file](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/texturing/loading-from-file.html) (stb_image) · [Mipmap Generation](https://eliemichel.github.io/LearnWebGPU/basic-compute/image-processing/mipmap-generation.html) (CPU half of that chapter, not the compute pass)

**Code:** `downsampleRGBA8` · `createPaddedTextureFromFileData` · `computeTextureAtlas*` · `Texture::loadFromFileWithPixelPadding` · `include/render/renderer.h` (the `r2d` alias) · game: `include/gameLayer/{tiledRenderer,bullet,enemy}.h` and `src/gameLayer/{gameLayer,tiledRenderer,bullet,enemy}.cpp`

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

| Guide | Why skipped |
|---|---|
| Depth buffer, lighting, PBR, cube maps | 2D, depth off |
| Compute pipeline (except as mipmap reading) | CPU mips instead |
| Instancing, render bundles, MSAA | one growable buffer + draw runs |
| Building for the Web | native Metal only |
| Milestones 9–10 (text, render targets) | unused by the game; still in the plan |

---

## File cheat sheet

| File | Role |
|---|---|
| `docs/webgpu-port-plan.md` | milestone list and risks |
| `include/render/wgpuContext.h` | platform: init / begin / end / shutdown |
| `include/render/wgpuFrame.h` | other render TUs: device, pass, texture bind groups |
| `include/render/wgpu2d.h` | gl2d-shaped game API |
| `src/render/wgpuContext.cpp` | instance → batch flush |
| `src/render/wgpuImgui.cpp` | ImGui geometry |
| `resources/shaders/quad.wgsl` | sprites |
| `resources/shaders/imgui.wgsl` | UI |
| `src/platform/glfwMain.cpp` | window + frame bracket |
| `src/render/webgpuImpl.cpp` | the one TU that defines the wrapper's bodies |
| `include/render/renderer.h` | the `r2d` alias the game includes |
| `include/render/wgpuMetalLayer.h` / `.mm` | macOS: pin the `CAMetalLayer` color space |
| `docs/render-port.md` | call-flow and frame-lifecycle diagrams |

## Reference shelf

Guide appendices that matter for a native wgpu-native port, none of them part of the milestone path: [Debugging](https://eliemichel.github.io/LearnWebGPU/appendices/debugging.html) · [Custom extensions with wgpu-native](https://eliemichel.github.io/LearnWebGPU/appendices/custom-extensions/with-wgpu-native.html) (what `wgpu.h` beside `webgpu.h` is for) · [Memory model](https://eliemichel.github.io/LearnWebGPU/appendices/memory-model.html) · [References](https://eliemichel.github.io/LearnWebGPU/appendices/references.html)
