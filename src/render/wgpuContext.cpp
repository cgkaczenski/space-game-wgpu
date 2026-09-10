// The WebGPU render context (render::wgpu*) and the gl2d-shaped API on top
// of it (wgpu2d::*). One translation unit on purpose: the API's methods
// need the context's internals and there is one renderer.

#include <render/quadShaderSource.h> // generated from resources/shaders/quad.wgsl
#include <render/wgpuContext.h>
#include <render/wgpu2d.h>
#include <render/wgpuFrame.h>
#ifdef __APPLE__
#endif

#include <webgpu/webgpu.hpp> // WebGPU-Cpp wrapper over webgpu.h (+ wgpu.h extensions)
#include <stb_image/stb_image.h>
#include <glm/glm.hpp>       // column-major like WGSL's mat4x4f

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>   // offsetof
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// File scope only, never in a header: wgpu:: names would otherwise leak into
// every includer. Inside this file, Device, Default, StringView, and the
// scoped enums all come from the wrapper. wgpu::Texture is always qualified
// because wgpu2d::Texture is the game-facing handle.
using namespace wgpu;

namespace render
{

namespace
{
	// What actually varies between sprite pipelines. Everything else the
	// descriptor sets -- topology, cull mode, vertex layout, bind group layout
	// -- is the same for every sprite draw, so it stays hardcoded in
	// createQuadPipelineVariant rather than becoming a key nobody varies.
	//
	// Deliberately narrow. Sample count (N6), depth state (N7) and the vertex
	// layout (N5) all belong here eventually; adding a field then is a few
	// lines, and guessing at four futures now is how the wrong key gets built.
	struct PipelineKey
	{
		TextureFormat format = TextureFormat::Undefined;
		wgpu2d::BlendMode blend = wgpu2d::BlendMode::Alpha;

		bool operator==(const PipelineKey &other) const
		{
			return format == other.format && blend == other.blend;
		}
	};

	struct PipelineEntry
	{
		PipelineKey key;
		// Null means this variant was tried and could not be built. That is
		// cached deliberately: flushBatch asks for a pipeline per *draw run*,
		// so a failure that is not remembered becomes a fresh
		// createRenderPipeline on every run of every frame -- a handful of
		// pipeline compiles per frame, each with a blocking error-scope drain,
		// which is enough to take a 13 ms frame past 50.
		RenderPipeline pipeline = nullptr;
		std::string label; // owns the text; useful in logs after creation
	};

	// ---------------------------------------------------------------------
	// Context: everything created once, plus the state of the current frame.
	// ---------------------------------------------------------------------
	struct Context
	{
		// 1a
		Instance instance = nullptr;
		Surface surface = nullptr; // borrowed: the app creates and releases it
		Adapter adapter = nullptr;

		// 1b
		Device device = nullptr;
		Queue queue = nullptr;
		TextureFormat surfaceFormat = TextureFormat::Undefined;
		int surfaceWidth = 0;
		int surfaceHeight = 0;
		bool surfaceConfigured = false;
		bool firstFramePresented = false;

		// 2, then R2: a render pipeline bakes in its colour target's format and
		// its blend state, so "the pipeline" is really one per (format, blend)
		// pair. They are built on demand and kept; there are a handful at most.
		// The shader module is compiled once and shared by every variant --
		// with one pipeline it could be released immediately, with N it cannot.
		ShaderModule quadShaderModule = nullptr;
		std::vector<PipelineEntry> quadPipelines;

		// 6a: the vertex buffer is rewritten every flush from the CPU batch
		// and grows (never shrinks) when a flush needs more than it holds.
		Buffer vertexBuffer = nullptr;
		uint64_t vertexBufferCapacityBytes = 0;

		// 10: how much of the vertex buffer and how many camera slots this
		// frame has already used. A frame can flush more than once now (a
		// render target, then the screen), and every draw in the frame reads
		// these buffers as they are at submit time -- a second flush writing
		// at offset 0 would silently rewrite what the first pass's draws
		// point at. So each flush appends instead, and both reset at
		// wgpuBeginFrame.
		uint64_t vertexBufferUsedBytes = 0;
		uint32_t cameraSlotsUsed = 0;

		// 4: created once at init, shared by every texture. Two samplers:
		// gl2d's "pixelated" (nearest) and its default (linear) filtering.
		Sampler samplerPixelated = nullptr;
		Sampler samplerLinear = nullptr;
		BindGroupLayout textureBindGroupLayout = nullptr; // group 0: texture + sampler
		PipelineLayout pipelineLayout = nullptr;

		// 5/6b: the camera uniform. One 64-byte matrix per camera used in a
		// frame, each in its own slot of cameraSlotStride bytes (the device's
		// minUniformBufferOffsetAlignment), selected per draw with a dynamic
		// offset. Buffer and bind group are recreated together when the slot
		// count grows.
		BindGroupLayout cameraBindGroupLayout = nullptr; // group 1: uniform buffer, dynamic offset
		Buffer uniformBuffer = nullptr;
		BindGroup cameraBindGroup = nullptr;
		uint32_t cameraSlotStride = 256;
		uint32_t cameraSlotCapacity = 0;
		uint32_t minUniformBufferOffsetAlignment = 256;
		bool runStatsPrinted = false;

		// 7: the frame in progress, between wgpuBeginFrame and wgpuEndFrame.
		bool frameOpen = false;          // begin succeeded; end must submit
		wgpu::Texture frameTexture = nullptr;
		TextureView frameView = nullptr;
		CommandEncoder frameEncoder = nullptr;
		glm::vec4 clearColor = {0, 0, 0, 1};   // recorded by Renderer2D::clearScreen

		// 10: a render pass belongs to one attachment, so the frame no longer
		// has "the" pass: it has the pass for whichever target was drawn into
		// last. Switching target ends the open pass and begins another.
		// framePassTarget is a texture registry id, 0 for the surface.
		RenderPassEncoder framePass = nullptr; // begun lazily by the first flush
		uint32_t framePassTarget = 0;
		bool surfaceCleared = false;     // the frame's first surface pass clears
		bool scaledCleared = false;      // likewise for the low-res stand-in

		// 10: optional low-resolution rendering. WGPU_RENDER_SCALE=0.25 makes
		// the game's draws land in a quarter-size target that is upscaled to
		// the surface at the end of the frame (ImGui still draws to the
		// surface at native size). The projection keeps using the surface's
		// dimensions, so the framing, the HUD layout and the mouse mapping
		// are untouched: only the rasterized resolution changes.
		float renderScale = 1.f;
		uint32_t scaledTargetId = 0;     // 0 when rendering straight to the surface
		bool scaledTargetDrawn = false;  // has content this frame, needs compositing
		bool compositing = false;        // guard while the composite draws
	};

	Context g;

	// Per-frame diagnostic counters. Reset in wgpuBeginFrame, folded into
	// FrameStats at the end. These exist to answer one question after the
	// fact: when a frame took five times as long as its neighbours, what did
	// it do that a normal frame does not?
	struct FramePerf
	{
		int pipelineBuilds = 0;
		int pipelineFailures = 0;
		int validationErrors = 0;
		int texturesCreated = 0;
		float blockedMs = 0.f;
	};
	FramePerf framePerf;


	// 10: defined below, needed by the pass bookkeeping above them.
	void flushBatch(uint32_t target);
	void compositeScaledTarget();

	// The target a plain flush goes to: the low-res stand-in when that is
	// enabled, otherwise the surface.
	uint32_t resolveTarget(uint32_t target)
	{
		if (target == 0 && g.scaledTargetId) { return g.scaledTargetId; }
		return target;
	}

	// The surface and the low-res stand-in are rebuilt every frame, so their
	// first pass clears; a user's FrameBuffer keeps its contents like gl2d's.
	bool isFrameTarget(uint32_t target)
	{
		return target == 0 || (g.scaledTargetId != 0 && target == g.scaledTargetId);
	}

	// One vertex as the GPU reads it: position, color, texture coordinate,
	// back to back. The pipeline's vertex layout below must describe exactly this.
	struct Vertex
	{
		float x, y;
		float r, g, b, a;
		float u, v;
	};
	static_assert(sizeof(Vertex) == 32, "vertex layout stride assumes tightly packed floats");

	// What the shader's Camera struct reads: one column-major mat4x4f.
	struct CameraUniforms
	{
		glm::mat4 viewProj;
	};
	static_assert(sizeof(CameraUniforms) == 64, "uniform layout must match the WGSL Camera struct");

	// The registry behind wgpu2d::Texture handles, indexed by id - 1.
	struct TextureEntry
	{
		wgpu::Texture texture = nullptr;
		TextureView view = nullptr;
		BindGroup bindGroup = nullptr; // group 0 for this texture + its sampler
		int width = 0;
		int height = 0;

		// R2: the format a pipeline must be built for to draw into this as an
		// attachment. Sampled-only textures carry it too, so nothing has to
		// guess which kind it is holding.
		TextureFormat format = TextureFormat::Undefined;

		// 10: render targets can also be drawn into. needsClear makes the
		// next pass on it clear instead of load, which is how FrameBuffer's
		// deferred clear and a freshly created (undefined) target work.
		bool pixelated = false;
		bool renderTarget = false;
		bool needsClear = false;
	};

	std::vector<TextureEntry> textures;

	// The CPU batch: gl2d's spritePositions / spriteColors / texturePositions
	// / spriteTextures, collapsed into one interleaved vertex vector plus,
	// per quad, its texture and the camera it was drawn under.
	std::vector<Vertex> batchVertices;
	std::vector<uint32_t> batchQuadTextures;
	std::vector<uint32_t> batchQuadCameras;   // index into frameCameras
	std::vector<wgpu2d::BlendMode> batchQuadBlends; // R2: the third run key

	// N2a: what the frame cost. Accumulated as it is recorded, snapshotted at
	// the end so a reader mid-frame sees a whole frame rather than a partial.
	wgpu2d::FrameStats statsInProgress;
	wgpu2d::FrameStats statsLastFrame;

	// N2b: submit-to-work-done, the closest this adapter offers to a GPU
	// figure. One outstanding at a time; a frame that submits while the last
	// is still pending simply does not measure itself.
	std::chrono::steady_clock::time_point submitAt;
	bool workDonePending = false;
	float lastGpuMillis = -1.f;

	void onQueueWorkDone(WGPUQueueWorkDoneStatus status, void *, void *)
	{
		workDonePending = false;
		if (status != QueueWorkDoneStatus::Success) { return; }
		const auto now = std::chrono::steady_clock::now();
		lastGpuMillis = std::chrono::duration<float, std::milli>(now - submitAt).count();
	}
	std::vector<wgpu2d::Camera> frameCameras; // every distinct camera used this frame, in order
	wgpu2d::Texture white1pxSquareTexture;    // gl2d's untextured path samples this

	// ---------------------------------------------------------------------
	// Small helpers
	// ---------------------------------------------------------------------
	const char *backendName(WGPUBackendType t)
	{
		switch (t)
		{
			case BackendType::Null: return "Null";
			case BackendType::WebGPU: return "WebGPU";
			case BackendType::D3D11: return "D3D11";
			case BackendType::D3D12: return "D3D12";
			case BackendType::Metal: return "Metal";
			case BackendType::Vulkan: return "Vulkan";
			case BackendType::OpenGL: return "OpenGL";
			case BackendType::OpenGLES: return "OpenGLES";
			default: return "Undefined";
		}
	}

	const char *adapterTypeName(WGPUAdapterType t)
	{
		switch (t)
		{
			case AdapterType::DiscreteGPU: return "discrete GPU";
			case AdapterType::IntegratedGPU: return "integrated GPU";
			case AdapterType::CPU: return "CPU";
			default: return "unknown";
		}
	}

	const char *formatName(WGPUTextureFormat f)
	{
		switch (f)
		{
			case TextureFormat::BGRA8Unorm: return "BGRA8Unorm";
			case TextureFormat::BGRA8UnormSrgb: return "BGRA8UnormSrgb";
			case TextureFormat::RGBA8Unorm: return "RGBA8Unorm";
			case TextureFormat::RGBA8UnormSrgb: return "RGBA8UnormSrgb";
			default: return "other";
		}
	}

	uint32_t ceilToNextMultiple(uint32_t value, uint32_t step)
	{
		uint32_t divide_and_ceil = value / step + (value % step == 0 ? 0 : 1);
		return step * divide_and_ceil;
	}

	// wgpu-native's own diagnostics (wgpu.h extension, C API: the wrapper does
	// not cover it). Warn and above is enough for now.
	void logCallback(WGPULogLevel level, WGPUStringView message, void *)
	{
		const char *tag = level == WGPULogLevel_Error ? "error"
			: level == WGPULogLevel_Warn ? "warn"
			: level == WGPULogLevel_Info ? "info"
			: level == WGPULogLevel_Debug ? "debug" : "trace";
		std::cerr << "[wgpu " << tag << "] " << StringView(message) << "\n";
	}

	// The wrapper keeps callbacks as plain C function pointers inside the
	// CallbackInfo structs (its std::function typedefs are unused in this
	// generation), so context still travels through userdata1.

	// Filled in by the request-adapter callback. Unsynchronized: the callback
	// only runs on this thread, inside wgpuInstanceProcessEvents (see mode below).
	struct AdapterRequest
	{
		Adapter adapter = nullptr;
		bool done = false;
	};

	void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter,
		WGPUStringView message, void *userdata1, void *)
	{
		auto *req = static_cast<AdapterRequest *>(userdata1);
		if (status == RequestAdapterStatus::Success)
		{
			req->adapter = adapter;
		}
		else
		{
			std::cerr << "WebGPU: requestAdapter failed (status " << status << "): "
				<< StringView(message) << "\n";
		}
		req->done = true;
	}

	// Filled in by the request-device callback. Unsynchronized for the same
	// reason as AdapterRequest: it only runs inside wgpuInstanceProcessEvents.
	struct DeviceRequest
	{
		Device device = nullptr;
		bool done = false;
	};

	void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device,
		WGPUStringView message, void *userdata1, void *)
	{
		auto *req = static_cast<DeviceRequest *>(userdata1);
		if (status == RequestDeviceStatus::Success)
		{
			req->device = device;
		}
		else
		{
			std::cerr << "WebGPU: requestDevice failed (status " << status << "): "
				<< StringView(message) << "\n";
		}
		req->done = true;
	}

	// Fires if the GPU connection dies (driver reset, device destroyed).
	void onDeviceLost(WGPUDevice const *, WGPUDeviceLostReason reason,
		WGPUStringView message, void *, void *)
	{
		std::cerr << "WebGPU: device lost (reason " << reason << "): "
			<< StringView(message) << "\n";
	}

	// Fires on every validation error the API catches. This is the main
	// debugging channel: a bad descriptor field, a wrong usage flag, a
	// mismatched format all show up here as text.
	void onUncapturedError(WGPUDevice const *, WGPUErrorType type,
		WGPUStringView message, void *, void *)
	{
		const char *kind = type == ErrorType::Validation ? "validation"
			: type == ErrorType::OutOfMemory ? "out of memory"
			: type == ErrorType::Internal ? "internal" : "unknown";
		std::cerr << "WebGPU " << kind << " error: " << StringView(message) << "\n";
		++framePerf.validationErrors;
	}

	// ---------------------------------------------------------------------
	// Error scopes and debug groups
	// ---------------------------------------------------------------------

	// onUncapturedError above is the catch-all: it reports errors that no
	// scope claimed, and it cannot say which call produced them. When five
	// objects are created in a row and one message appears, matching it to a
	// call site is done by eye, and the message can arrive after the null
	// handle it explains.
	//
	// An error scope is a stack on the device. pushErrorScope claims errors
	// of one class (Validation / OutOfMemory / Internal); popErrorScope ends
	// the scope and reports the first error it captured, or NoError. Errors
	// inside a scope do not reach onUncapturedError. What that buys is not
	// the text -- it is attribution: this operation, right here, failed for
	// this reason.
	//
	// popErrorScope is asynchronous. The callback fires inside
	// instance.processEvents(), the same mechanism requestAdapter and
	// requestDevice use in wgpuInit. Draining it means a stall, which is why
	// every scope in this file wraps object *creation* and none of them wrap
	// per-frame recording.
	struct ErrorScopeResult
	{
		const char *what = nullptr;
		bool done = false;
		bool failed = false;
	};

	void onPopErrorScope(WGPUPopErrorScopeStatus status, WGPUErrorType type,
		WGPUStringView message, void *userdata1, void *)
	{
		auto *result = static_cast<ErrorScopeResult *>(userdata1);
		result->done = true;

		if (status != PopErrorScopeStatus::Success)
		{
			// EmptyStack means a pop without a push; anything else is the
			// instance going away underneath us.
			std::cerr << "WebGPU: popErrorScope (" << result->what << ") failed with status "
				<< status << "\n";
			return;
		}

		if (type == ErrorType::NoError) { return; }

		const char *kind = type == ErrorType::Validation ? "validation"
			: type == ErrorType::OutOfMemory ? "out of memory"
			: type == ErrorType::Internal ? "internal" : "unknown";
		std::cerr << "WebGPU " << kind << " error creating " << result->what << ": "
			<< StringView(message) << "\n";
		std::cerr.flush();
		result->failed = true;
	}

	// The scope stack's names. Scopes are pushed and popped on one thread
	// inside creation calls, so a plain vector is enough.
	std::vector<const char *> errorScopeNames;

	// ---------------------------------------------------------------------
	// Surface
	// ---------------------------------------------------------------------

	// The surface is the application's. We configure it and present to it,
	// then drop the pointer. Calling release() here would double-free with
	// the app's wgpuSurfaceRelease.
	void forgetSurface()
	{
		if (!g.surface) { return; }
		if (g.surfaceConfigured)
		{
			g.surface.unconfigure();
			g.surfaceConfigured = false;
		}
		g.surface = nullptr;
	}

	// Configures (or reconfigures) the surface at the window's current
	// framebuffer size. Framebuffer size, not window size: on a Retina
	// display they differ by the scale factor.
	// The size comes from the caller. The library used to ask GLFW, which is
	// how it came to depend on a window system it has no business knowing
	// about; the application already has this number.
	void configureSurface(int w, int h)
	{
		if (w <= 0 || h <= 0)
		{
			return; // minimized; keep the old configuration
		}

		SurfaceConfiguration config = Default;
		config.device = g.device;
		config.format = g.surfaceFormat;
		// CopySrc so a finished frame can be read back (see wgpuRequestFrameCapture).
		config.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
		config.width = (uint32_t)w;
		config.height = (uint32_t)h;
		config.viewFormatCount = 0;
		config.viewFormats = nullptr;
		config.alphaMode = CompositeAlphaMode::Auto;
		config.presentMode = PresentMode::Fifo; // vsync, always supported

		g.surface.configure(config);
		g.surfaceWidth = w;
		g.surfaceHeight = h;
		g.surfaceConfigured = true;
		std::cout << "WebGPU surface configured at " << w << "x" << h << "\n";
		std::cout.flush();
	}

	// ---------------------------------------------------------------------
	// Shaders, samplers, layouts, pipeline
	// ---------------------------------------------------------------------
	bool readTextFile(const char *path, std::string &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) { return false; }
		std::stringstream ss;
		ss << file.rdbuf();
		out = ss.str();
		return true;
	}

	// Compiles WGSL source into a shader module. `label` names it in
	// validation messages and in a GPU capture.
	ShaderModule createShaderModule(const char *source, const char *label)
	{
		// The WGSL source is a chained struct hanging off the module descriptor.
		// Default sets chain.sType = ShaderSourceWGSL and chain.next = nullptr.
		ShaderSourceWGSL wgsl = Default;
		wgsl.code = StringView(source);

		ShaderModuleDescriptor desc = Default;
		desc.label = StringView(label);
		desc.nextInChain = &wgsl.chain;

		// WGSL compile errors arrive here with line and column numbers, named
		// to the module, instead of loose on the uncaptured-error callback.
		ErrorScope scope(label);
		return g.device.createShaderModule(desc);
	}

	// Same, from a file. The library's own shaders are compiled in (see
	// quadShaderSource.h); this exists for the application, which has a
	// resource layout and may keep its shaders in it -- the ImGui backend in
	// src/platform does exactly that through wgpuCreateShaderModuleFromFile.
	ShaderModule createShaderModuleFromFile(const char *path)
	{
		std::string source;
		if (!readTextFile(path, source))
		{
			std::cerr << "WebGPU: cannot read shader file " << path << "\n";
			return nullptr;
		}
		return createShaderModule(source.c_str(), path);
	}

	// Samplers: how a shader reads a texture. Separate from the texture
	// (unlike OpenGL), so two serve every sprite. Both clamp to edge like
	// gl2d. Pixelated = gl2d's GL_NEAREST / GL_NEAREST_MIPMAP_NEAREST;
	// linear = GL_LINEAR / GL_LINEAR_MIPMAP_LINEAR.
	Sampler createSampler(const char *label, FilterMode filter, MipmapFilterMode mipFilter)
	{
		SamplerDescriptor desc = Default;
		desc.label = StringView(label);
		desc.addressModeU = AddressMode::ClampToEdge;
		desc.addressModeV = AddressMode::ClampToEdge;
		desc.addressModeW = AddressMode::ClampToEdge;
		desc.magFilter = filter;
		desc.minFilter = filter;
		desc.mipmapFilter = mipFilter;
		desc.lodMinClamp = 0.0f;
		desc.lodMaxClamp = 32.0f;
		desc.compare = CompareFunction::Undefined;
		desc.maxAnisotropy = 1;

		ErrorScope scope(label);
		return g.device.createSampler(desc);
	}

	bool createSamplers()
	{
		g.samplerPixelated = createSampler("sampler pixelated", FilterMode::Nearest, MipmapFilterMode::Nearest);
		g.samplerLinear = createSampler("sampler linear", FilterMode::Linear, MipmapFilterMode::Linear);
		if (!g.samplerPixelated || !g.samplerLinear)
		{
			std::cerr << "WebGPU: createSampler returned null\n";
			return false;
		}
		return true;
	}

	// Bind group layouts: the contract between the shader's
	// @group(n) @binding(m) declarations and the resources a bind group
	// supplies. Group 0: a sampled 2D float texture (binding 0) and a
	// filtering sampler (binding 1), fragment stage. Group 1: the camera
	// uniform with a dynamic offset, vertex stage. The pipeline layout
	// lists them by group index.
	bool createLayouts()
	{
		// One scope for the whole function: the three layouts are created
		// together and a failure in any of them means the same thing.
		ErrorScope scope("bind group layouts");

		BindGroupLayoutEntry entries[2];

		// Each entry describes exactly one kind of binding. Default leaves the
		// four sub-layouts at Undefined, which is ambiguous; the three unused
		// ones must say BindingNotUsed explicitly.
		entries[0] = Default;
		entries[0].binding = 0;
		entries[0].visibility = ShaderStage::Fragment;
		entries[0].texture.sampleType = TextureSampleType::Float;
		entries[0].texture.viewDimension = TextureViewDimension::_2D;
		entries[0].texture.multisampled = false;
		entries[0].buffer.type = BufferBindingType::BindingNotUsed;
		entries[0].sampler.type = SamplerBindingType::BindingNotUsed;
		entries[0].storageTexture.access = StorageTextureAccess::BindingNotUsed;

		entries[1] = Default;
		entries[1].binding = 1;
		entries[1].visibility = ShaderStage::Fragment;
		entries[1].sampler.type = SamplerBindingType::Filtering;
		entries[1].buffer.type = BufferBindingType::BindingNotUsed;
		entries[1].texture.sampleType = TextureSampleType::BindingNotUsed;
		entries[1].storageTexture.access = StorageTextureAccess::BindingNotUsed;

		BindGroupLayoutDescriptor layoutDesc = Default;
		layoutDesc.label = StringView("texture bind group layout");
		layoutDesc.entryCount = 2;
		layoutDesc.entries = entries;
		g.textureBindGroupLayout = g.device.createBindGroupLayout(layoutDesc);
		if (!g.textureBindGroupLayout)
		{
			std::cerr << "WebGPU: createBindGroupLayout returned null\n";
			return false;
		}

		BindGroupLayoutEntry cameraEntry = Default;
		cameraEntry.binding = 0;
		cameraEntry.visibility = ShaderStage::Vertex;
		cameraEntry.buffer.type = BufferBindingType::Uniform;
		cameraEntry.buffer.hasDynamicOffset = true;    // one buffer, one slot per camera, offset per draw
		cameraEntry.buffer.minBindingSize = sizeof(CameraUniforms);
		cameraEntry.sampler.type = SamplerBindingType::BindingNotUsed;
		cameraEntry.texture.sampleType = TextureSampleType::BindingNotUsed;
		cameraEntry.storageTexture.access = StorageTextureAccess::BindingNotUsed;

		BindGroupLayoutDescriptor cameraLayoutDesc = Default;
		cameraLayoutDesc.label = StringView("camera bind group layout");
		cameraLayoutDesc.entryCount = 1;
		cameraLayoutDesc.entries = &cameraEntry;
		g.cameraBindGroupLayout = g.device.createBindGroupLayout(cameraLayoutDesc);
		if (!g.cameraBindGroupLayout)
		{
			std::cerr << "WebGPU: createBindGroupLayout (camera) returned null\n";
			return false;
		}

		PipelineLayoutDescriptor pipelineLayoutDesc = Default;
		pipelineLayoutDesc.label = StringView("quad pipeline layout");
		pipelineLayoutDesc.bindGroupLayoutCount = 2;
		WGPUBindGroupLayout layouts[2] = { g.textureBindGroupLayout, g.cameraBindGroupLayout };
		pipelineLayoutDesc.bindGroupLayouts = layouts;
		g.pipelineLayout = g.device.createPipelineLayout(pipelineLayoutDesc);
		if (!g.pipelineLayout)
		{
			std::cerr << "WebGPU: createPipelineLayout returned null\n";
			return false;
		}
		return true;
	}

	// The uniform buffer holds one CameraUniforms per slot, slots spaced by
	// the device's minimum dynamic-offset alignment (256 here). The bind
	// group binds one struct's worth at offset 0; setBindGroup's dynamic
	// offset moves that window to the slot for each draw. Growing the slot
	// count means a new buffer and, because the group references the buffer,
	// a new bind group.
	bool ensureCameraSlotCapacity(uint32_t slots)
	{
		if (g.uniformBuffer && slots <= g.cameraSlotCapacity) { return true; }

		ErrorScope scope("camera uniform buffer");

		g.cameraSlotStride = ceilToNextMultiple((uint32_t)sizeof(CameraUniforms), g.minUniformBufferOffsetAlignment);
		uint32_t capacity = g.cameraSlotCapacity ? g.cameraSlotCapacity : 16;
		while (capacity < slots) { capacity *= 2; }

		BufferDescriptor desc = Default;
		desc.label = StringView("camera uniforms");
		desc.usage = BufferUsage::Uniform | BufferUsage::CopyDst;
		desc.size = (uint64_t)g.cameraSlotStride * capacity;
		desc.mappedAtCreation = false;
		Buffer newBuffer = g.device.createBuffer(desc);
		if (!newBuffer)
		{
			std::cerr << "WebGPU: createBuffer (uniform, " << desc.size << " bytes) returned null\n";
			return false;
		}

		BindGroupEntry entry = Default;
		entry.binding = 0;
		entry.buffer = newBuffer;
		entry.offset = 0;
		entry.size = sizeof(CameraUniforms); // one struct, not the whole buffer
		entry.sampler = nullptr;
		entry.textureView = nullptr;

		BindGroupDescriptor groupDesc = Default;
		groupDesc.label = StringView("camera bind group");
		groupDesc.layout = g.cameraBindGroupLayout;
		groupDesc.entryCount = 1;
		groupDesc.entries = &entry;
		BindGroup newGroup = g.device.createBindGroup(groupDesc);
		if (!newGroup)
		{
			std::cerr << "WebGPU: createBindGroup (camera) returned null\n";
			newBuffer.release();
			return false;
		}

		if (g.cameraBindGroup) { g.cameraBindGroup.release(); }
		if (g.uniformBuffer) { g.uniformBuffer.release(); }
		g.cameraBindGroup = newGroup;
		g.uniformBuffer = newBuffer;
		g.cameraSlotCapacity = capacity;
		std::cout << "WebGPU camera slots: " << capacity << " x " << g.cameraSlotStride << " bytes\n";
		std::cout.flush();
		return true;
	}

	// The render pipeline: every configurable stage of the GPU's fixed
	// triangle pipeline, baked into one immutable object. Selected per pass
	// with setPipeline; never mutated.
	const char *blendName(wgpu2d::BlendMode mode)
	{
		switch (mode)
		{
			case wgpu2d::BlendMode::Additive: return "additive";
			case wgpu2d::BlendMode::Premultiplied: return "premultiplied";
			default: return "alpha";
		}
	}

	// One pipeline for one (format, blend) pair. Everything the descriptor
	// sets other than those two is identical across variants, which is what
	// makes the key as small as it is.
	RenderPipeline createQuadPipelineVariant(const PipelineKey &key, const char *label)
	{
		if (!g.quadShaderModule) { return nullptr; }
		ShaderModule module = g.quadShaderModule;

		RenderPipelineDescriptor desc = Default;
		desc.label = StringView(label);

		// Vertex layout: how the pipeline reads bytes out of the vertex buffer.
		// One buffer, interleaved, 32 bytes per vertex. Each attribute names
		// the @location it feeds in the shader.
		VertexAttribute attributes[3];
		attributes[0] = Default;
		attributes[0].shaderLocation = 0;                // @location(0) position
		attributes[0].format = VertexFormat::Float32x2;
		attributes[0].offset = offsetof(Vertex, x);
		attributes[1] = Default;
		attributes[1].shaderLocation = 1;                // @location(1) color
		attributes[1].format = VertexFormat::Float32x4;
		attributes[1].offset = offsetof(Vertex, r);
		attributes[2] = Default;
		attributes[2].shaderLocation = 2;                // @location(2) uv
		attributes[2].format = VertexFormat::Float32x2;
		attributes[2].offset = offsetof(Vertex, u);

		VertexBufferLayout vertexLayout = Default;
		vertexLayout.arrayStride = sizeof(Vertex);
		vertexLayout.stepMode = VertexStepMode::Vertex; // advance once per vertex, not per instance
		vertexLayout.attributeCount = 3;
		vertexLayout.attributes = attributes;

		desc.vertex.module = module;
		desc.vertex.entryPoint = StringView("vs_main");
		desc.vertex.constantCount = 0;
		desc.vertex.constants = nullptr;
		desc.vertex.bufferCount = 1;
		desc.vertex.buffers = &vertexLayout;

		// Primitive assembly. Default already gives TriangleList, CCW front
		// face, no culling; set explicitly so the choice is visible.
		desc.primitive.topology = PrimitiveTopology::TriangleList;
		desc.primitive.stripIndexFormat = IndexFormat::Undefined;
		desc.primitive.frontFace = FrontFace::CCW;
		desc.primitive.cullMode = CullMode::None; // gl2d never culled; quads may be flipped by negative sizes
		desc.primitive.unclippedDepth = false;

		// Alpha is gl2d's blend, matching enableNecessaryGLFeatures():
		//   glBlendEquation(GL_FUNC_ADD)
		//   glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
		//
		// The alpha channel is One / OneMinusSrcAlpha in every mode, which is
		// what makes a render target usable: drawing (C, a) into a cleared
		// target leaves rgb = C*a and a = a, so the target's contents are
		// premultiplied no matter which colour mode produced them. That is
		// why Premultiplied exists and why it is the right mode to draw a
		// target back with.
		BlendState blend = Default;
		blend.color.operation = BlendOperation::Add;
		switch (key.blend)
		{
			case wgpu2d::BlendMode::Additive:
				// src*a + dst: the destination survives whole, so light adds.
				blend.color.srcFactor = BlendFactor::SrcAlpha;
				blend.color.dstFactor = BlendFactor::One;
				break;
			case wgpu2d::BlendMode::Premultiplied:
				// src + dst*(1-a): the source is already scaled by coverage.
				blend.color.srcFactor = BlendFactor::One;
				blend.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
				break;
			default:
				// src*a + dst*(1-a): gl2d's "over".
				blend.color.srcFactor = BlendFactor::SrcAlpha;
				blend.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
				break;
		}
		blend.alpha.operation = BlendOperation::Add;
		blend.alpha.srcFactor = BlendFactor::One;
		blend.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;

		// A pipeline may only draw into an attachment of the format it was
		// built for. R1 established that this is not caught at creation: the
		// pipeline is built happily and the mismatch is only reported when it
		// meets the attachment inside a pass. Keying on the format is what
		// makes that structural instead of a rule in a comment.
		ColorTargetState colorTarget = Default;
		colorTarget.format = key.format;
		colorTarget.blend = &blend;
		colorTarget.writeMask = ColorWriteMask::All;

		FragmentState fragment = Default;
		fragment.module = module;
		fragment.entryPoint = StringView("fs_main");
		fragment.constantCount = 0;
		fragment.constants = nullptr;
		fragment.targetCount = 1;
		fragment.targets = &colorTarget;
		desc.fragment = &fragment;

		// No depth/stencil (gl2d disabled depth testing) and no multisampling.
		desc.depthStencil = nullptr;
		desc.multisample.count = 1;
		desc.multisample.mask = 0xFFFFFFFFu;
		desc.multisample.alphaToCoverageEnabled = false;

		// The explicit layout: group 0 texture + sampler, group 1 camera.
		desc.layout = g.pipelineLayout;

		RenderPipeline pipeline = nullptr;
		bool invalid = false;
		{
			// The densest descriptor in the file: a layout that disagrees
			// with the shader, or a bad blend factor, lands here as one
			// message naming this variant rather than as a null handle.
			ErrorScope scope(label);
			pipeline = g.device.createRenderPipeline(desc);
			// A rejected pipeline is not null -- createRenderPipeline hands
			// back a live handle in an invalid state, and every later
			// setPipeline reports it again. Only the scope can tell the
			// difference, and an invalid variant must never reach the cache
			// or it is re-served every frame.
			invalid = scope.failed();
		}

		// The module is not released here: it is g.quadShaderModule and every
		// other variant is built from it. wgpuShutdown owns its lifetime.

		if (!pipeline || invalid)
		{
			std::cerr << "WebGPU: pipeline variant '" << label << "' is not usable\n";
			if (pipeline) { pipeline.release(); }
			return nullptr;
		}
		return pipeline;
	}

	// The cache. A linear scan over a handful of entries beats writing a hash
	// for a two-field key, and lookups happen at run boundaries, not per quad.
	RenderPipeline getQuadPipeline(const PipelineKey &key)
	{
		for (const PipelineEntry &entry : g.quadPipelines)
		{
			if (entry.key == key) { return entry.pipeline; }
		}

		// Built on demand. The label is what a validation message and a GPU
		// capture show, so it names both halves of the key.
		PipelineEntry entry;
		entry.key = key;
		entry.label = std::string("quad pipeline (") + formatName(key.format) + ", "
			+ blendName(key.blend) + ")";

		// Counted because compiling a pipeline mid-frame is expensive. With
		// the failure cached below, this should read 1 for a variant's first
		// frame and 0 thereafter -- anything larger means something is asking
		// for keys that keep changing.
		++framePerf.pipelineBuilds;
		entry.pipeline = createQuadPipelineVariant(key, entry.label.c_str());
		const bool built = (entry.pipeline != nullptr);

		if (!built)
		{
			++framePerf.pipelineFailures;
			// Once, loudly, and then never again: the entry is cached either
			// way, so this cannot become a per-frame message any more than it
			// can become a per-frame compile.
			std::cerr << "WebGPU: " << entry.label << " could not be built."
				<< " Draws needing it are skipped for the rest of this run.\n";
			std::cerr.flush();
		}

		g.quadPipelines.push_back(std::move(entry));
		if (built)
		{
			int usable = 0;
			for (const PipelineEntry &e : g.quadPipelines) { if (e.pipeline) { ++usable; } }
			std::cout << "WebGPU " << g.quadPipelines.back().label << " created ("
				<< usable << " variant" << (usable == 1 ? "" : "s") << " cached)\n";
			std::cout.flush();
		}
		return g.quadPipelines.back().pipeline;
	}

	// ---------------------------------------------------------------------
	// Textures
	// ---------------------------------------------------------------------

	// One mip level down: each output texel is the average of a 2x2 block of
	// the input (clamped at odd edges). WebGPU has no glGenerateMipmap; this
	// is the CPU replacement for gl2d's mips.
	std::vector<unsigned char> downsampleRGBA8(const std::vector<unsigned char> &src, int w, int h, int &outW, int &outH)
	{
		outW = std::max(1, w / 2);
		outH = std::max(1, h / 2);
		std::vector<unsigned char> dst((size_t)outW * outH * 4);
		for (int y = 0; y < outH; y++)
		{
			const int y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
			for (int x = 0; x < outW; x++)
			{
				const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1);
				for (int c = 0; c < 4; c++)
				{
					const int sum = src[4 * (x0 + y0 * w) + c] + src[4 * (x1 + y0 * w) + c]
						+ src[4 * (x0 + y1 * w) + c] + src[4 * (x1 + y1 * w) + c];
					dst[4 * (x + y * outW) + c] = (unsigned char)((sum + 2) / 4);
				}
			}
		}
		return dst;
	}

	// Uploads RGBA8 pixels (rows top-first, tightly packed) as a texture,
	// with CPU-generated mip levels when requested, creates its view, and
	// builds the bind group that hands the view plus the right sampler to
	// the shader. Returns a handle (id 0 on failure) into the registry.
	wgpu2d::Texture createTextureFromPixels(const unsigned char *pixels, int width, int height,
		const char *label, bool pixelated, bool useMipMaps)
	{
		if (!g.device || width <= 0 || height <= 0 || !pixels)
		{
			std::cerr << "WebGPU: createTextureFromPixels called with no device or empty image (" << label << ")\n";
			return wgpu2d::Texture{};
		}

		// All levels, level 0 first.
		std::vector<std::vector<unsigned char>> levels;
		std::vector<int> levelW, levelH;
		levels.emplace_back(pixels, pixels + (size_t)4 * width * height);
		levelW.push_back(width);
		levelH.push_back(height);
		if (useMipMaps)
		{
			while (levelW.back() > 1 || levelH.back() > 1)
			{
				int w = 0, h = 0;
				levels.push_back(downsampleRGBA8(levels.back(), levelW.back(), levelH.back(), w, h));
				levelW.push_back(w);
				levelH.push_back(h);
			}
		}

		// One scope for texture, view and bind group: they are created
		// together and the label names the image either way.
		ErrorScope scope(label);

		TextureEntry entry;
		entry.width = width;
		entry.height = height;

		// The texture: GPU pixel storage with a fixed format and usage.
		// TextureBinding: a shader may sample it. CopyDst: the queue may write it.
		TextureDescriptor texDesc = Default;
		texDesc.label = StringView(label);
		texDesc.dimension = TextureDimension::_2D;
		texDesc.size.width = (uint32_t)width;
		texDesc.size.height = (uint32_t)height;
		texDesc.size.depthOrArrayLayers = 1;
		texDesc.format = TextureFormat::RGBA8Unorm;
		entry.format = texDesc.format;
		++framePerf.texturesCreated;
		texDesc.mipLevelCount = (uint32_t)levels.size();
		texDesc.sampleCount = 1;
		texDesc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
		texDesc.viewFormatCount = 0;
		texDesc.viewFormats = nullptr;
		entry.texture = g.device.createTexture(texDesc);
		if (!entry.texture)
		{
			std::cerr << "WebGPU: createTexture returned null (" << label << ")\n";
			return wgpu2d::Texture{};
		}

		// Upload every level. This is glTexImage2D + glGenerateMipmap.
		for (size_t i = 0; i < levels.size(); i++)
		{
			TexelCopyTextureInfo destination = Default;
			destination.texture = entry.texture;
			destination.mipLevel = (uint32_t)i;
			destination.origin.x = 0;
			destination.origin.y = 0;
			destination.origin.z = 0;
			destination.aspect = TextureAspect::All;

			TexelCopyBufferLayout sourceLayout = Default;
			sourceLayout.offset = 0;
			sourceLayout.bytesPerRow = 4 * (uint32_t)levelW[i];
			sourceLayout.rowsPerImage = (uint32_t)levelH[i];

			Extent3D extent = Default;
			extent.width = (uint32_t)levelW[i];
			extent.height = (uint32_t)levelH[i];
			extent.depthOrArrayLayers = 1;

			g.queue.writeTexture(destination, levels[i].data(), levels[i].size(), sourceLayout, extent);
		}

		// The view the shader samples through: all mip levels.
		TextureViewDescriptor viewDesc = Default;
		viewDesc.label = StringView(label);
		viewDesc.format = TextureFormat::RGBA8Unorm;
		viewDesc.dimension = TextureViewDimension::_2D;
		viewDesc.baseMipLevel = 0;
		viewDesc.mipLevelCount = (uint32_t)levels.size();
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect = TextureAspect::All;
		viewDesc.usage = TextureUsage::TextureBinding;
		entry.view = entry.texture.createView(viewDesc);
		if (!entry.view)
		{
			std::cerr << "WebGPU: createView returned null (" << label << ")\n";
			entry.texture.release();
			return wgpu2d::Texture{};
		}

		// The bind group: this view and the sampler for its filtering mode,
		// in the slots the layout declared.
		BindGroupEntry bindEntries[2];
		bindEntries[0] = Default;
		bindEntries[0].binding = 0;
		bindEntries[0].textureView = entry.view;
		bindEntries[0].buffer = nullptr;
		bindEntries[0].sampler = nullptr;
		bindEntries[1] = Default;
		bindEntries[1].binding = 1;
		bindEntries[1].sampler = pixelated ? g.samplerPixelated : g.samplerLinear;
		bindEntries[1].buffer = nullptr;
		bindEntries[1].textureView = nullptr;

		BindGroupDescriptor groupDesc = Default;
		groupDesc.label = StringView(label);
		groupDesc.layout = g.textureBindGroupLayout;
		groupDesc.entryCount = 2;
		groupDesc.entries = bindEntries;
		entry.bindGroup = g.device.createBindGroup(groupDesc);
		if (!entry.bindGroup)
		{
			std::cerr << "WebGPU: createBindGroup returned null (" << label << ")\n";
			entry.view.release();
			entry.texture.release();
			return wgpu2d::Texture{};
		}

		textures.push_back(entry);
		std::cout << "WebGPU texture " << textures.size() << ": " << width << "x" << height
			<< ", " << levels.size() << " mip level" << (levels.size() == 1 ? "" : "s")
			<< (pixelated ? ", nearest" : ", linear") << " (" << label << ")\n";
		wgpu2d::Texture handle;
		handle.id = (uint32_t)textures.size();
		return handle;
	}

	// 10: a texture with nothing in it that the renderer may draw into. Two
	// usages matter: RenderAttachment (a pass may target it) and
	// TextureBinding (the shader may sample it afterwards). Its format is the
	// surface's, because the sprite pipeline was built for that format and a
	// pipeline can only draw into a matching attachment.
	//
	// reuseId re-fills an existing registry slot so handles stay valid across
	// a resize; 0 appends a new one.
	wgpu2d::Texture createRenderTarget(int width, int height, bool pixelated, const char *label, uint32_t reuseId = 0)
	{
		if (!g.device || width <= 0 || height <= 0)
		{
			std::cerr << "WebGPU: createRenderTarget with no device or empty size (" << label << ")\n";
			return wgpu2d::Texture{};
		}

		ErrorScope scope(label);

		TextureEntry entry;
		entry.width = width;
		entry.height = height;
		entry.pixelated = pixelated;
		entry.renderTarget = true;
		entry.needsClear = true; // a fresh target's contents are undefined

		TextureDescriptor texDesc = Default;
		texDesc.label = StringView(label);
		texDesc.dimension = TextureDimension::_2D;
		texDesc.size.width = (uint32_t)width;
		texDesc.size.height = (uint32_t)height;
		texDesc.size.depthOrArrayLayers = 1;
		texDesc.format = g.surfaceFormat;
		entry.format = texDesc.format;
		++framePerf.texturesCreated;
		texDesc.mipLevelCount = 1;
		texDesc.sampleCount = 1;
		// CopySrc for the same reason the surface has it: a render target is a
		// frame too, and reading one back is how milestone 12's blend numbers
		// were measured.
		texDesc.usage = TextureUsage::RenderAttachment | TextureUsage::TextureBinding
			| TextureUsage::CopySrc;
		texDesc.viewFormatCount = 0;
		texDesc.viewFormats = nullptr;
		entry.texture = g.device.createTexture(texDesc);
		if (!entry.texture)
		{
			std::cerr << "WebGPU: createTexture (render target) returned null (" << label << ")\n";
			return wgpu2d::Texture{};
		}

		TextureViewDescriptor viewDesc = Default;
		viewDesc.label = StringView(label);
		viewDesc.format = g.surfaceFormat;
		viewDesc.dimension = TextureViewDimension::_2D;
		viewDesc.baseMipLevel = 0;
		viewDesc.mipLevelCount = 1;
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect = TextureAspect::All;
		viewDesc.usage = TextureUsage::RenderAttachment | TextureUsage::TextureBinding;
		entry.view = entry.texture.createView(viewDesc);
		if (!entry.view)
		{
			std::cerr << "WebGPU: createView (render target) returned null (" << label << ")\n";
			entry.texture.release();
			return wgpu2d::Texture{};
		}

		BindGroupEntry bindEntries[2];
		bindEntries[0] = Default;
		bindEntries[0].binding = 0;
		bindEntries[0].textureView = entry.view;
		bindEntries[0].buffer = nullptr;
		bindEntries[0].sampler = nullptr;
		bindEntries[1] = Default;
		bindEntries[1].binding = 1;
		bindEntries[1].sampler = pixelated ? g.samplerPixelated : g.samplerLinear;
		bindEntries[1].buffer = nullptr;
		bindEntries[1].textureView = nullptr;

		BindGroupDescriptor groupDesc = Default;
		groupDesc.label = StringView(label);
		groupDesc.layout = g.textureBindGroupLayout;
		groupDesc.entryCount = 2;
		groupDesc.entries = bindEntries;
		entry.bindGroup = g.device.createBindGroup(groupDesc);
		if (!entry.bindGroup)
		{
			std::cerr << "WebGPU: createBindGroup (render target) returned null (" << label << ")\n";
			entry.view.release();
			entry.texture.release();
			return wgpu2d::Texture{};
		}

		uint32_t id = reuseId;
		if (id != 0 && id <= textures.size())
		{
			TextureEntry &old = textures[id - 1];
			if (old.bindGroup) { old.bindGroup.release(); }
			if (old.view) { old.view.release(); }
			if (old.texture) { old.texture.release(); }
			old = entry;
		}
		else
		{
			textures.push_back(entry);
			id = (uint32_t)textures.size();
		}

		std::cout << "WebGPU render target " << id << ": " << width << "x" << height
			<< (pixelated ? ", nearest" : ", linear") << " (" << label << ")\n";
		std::cout.flush();
		wgpu2d::Texture handle;
		handle.id = id;
		return handle;
	}

	// 10: keeps the low-res stand-in matching the surface's current size.
	void ensureScaledTarget()
	{
		if (g.renderScale >= 1.f)
		{
			g.scaledTargetId = 0;
			return;
		}

		const int w = std::max(1, (int)std::lround(g.surfaceWidth * g.renderScale));
		const int h = std::max(1, (int)std::lround(g.surfaceHeight * g.renderScale));
		if (g.scaledTargetId && g.scaledTargetId <= textures.size()
			&& textures[g.scaledTargetId - 1].width == w
			&& textures[g.scaledTargetId - 1].height == h)
		{
			return;
		}

		// Nearest filtering on the way back up: this is the pixel-perfect
		// upscale, not a smooth one.
		wgpu2d::Texture target = createRenderTarget(w, h, true, "low-res target", g.scaledTargetId);
		g.scaledTargetId = target.id;
	}

	// Reads a whole file, like gl2d's loaders do before decoding.
	bool readBinaryFile(const char *path, std::vector<unsigned char> &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) { return false; }
		file.seekg(0, std::ios::end);
		const std::streamoff size = file.tellg();
		file.seekg(0, std::ios::beg);
		out.resize((size_t)size);
		if (size > 0) { file.read((char *)out.data(), size); }
		return true;
	}

	// gl2d's createFromFileDataWithPixelPadding, verbatim apart from the
	// missing load-time flip (see the milestone 4 orientation decision;
	// the padding is orientation-independent). Re-lays the image out with a
	// 2-pixel gutter around every blockSize x blockSize cell and duplicates
	// each cell's edge pixels into its gutter, so filtering never bleeds a
	// neighbor in.
	wgpu2d::Texture createPaddedTextureFromFileData(const unsigned char *image_file_data, size_t image_file_size,
		int blockSize, bool pixelated, bool useMipMaps, const char *label)
	{
		stbi_set_flip_vertically_on_load(0);

		int width = 0;
		int height = 0;
		int channels = 0;

		const unsigned char *decodedImage = stbi_load_from_memory(image_file_data, (int)image_file_size, &width, &height, &channels, 4);
		if (!decodedImage)
		{
			std::cerr << "WebGPU: cannot decode image " << label << ": " << stbi_failure_reason() << "\n";
			return wgpu2d::Texture{};
		}

		int newW = width + ((width * 2) / blockSize);
		int newH = height + ((height * 2) / blockSize);

		unsigned char *newData = new unsigned char[newW * newH * 4]{};

		auto getNew = [newData, newW](int x, int y, int c)
		{
			return &newData[4 * (x + (y * newW)) + c];
		};

		int newDataCursor = 0;
		int dataCursor = 0;

		//first copy data
		for (int y = 0; y < newH; y++)
		{
			int yNo = 0;
			if ((y == 0 || y == newH - 1
				|| ((y) % (blockSize + 2)) == 0 ||
				((y + 1) % (blockSize + 2)) == 0
				))
			{
				yNo = 1;
			}

			for (int x = 0; x < newW; x++)
			{
				if (
					yNo ||

					((
						x == 0 || x == newW - 1
						|| (x % (blockSize + 2)) == 0 ||
						((x + 1) % (blockSize + 2)) == 0
						)
						)

					)
				{
					newData[newDataCursor++] = 0;
					newData[newDataCursor++] = 0;
					newData[newDataCursor++] = 0;
					newData[newDataCursor++] = 0;
				}
				else
				{
					newData[newDataCursor++] = decodedImage[dataCursor++];
					newData[newDataCursor++] = decodedImage[dataCursor++];
					newData[newDataCursor++] = decodedImage[dataCursor++];
					newData[newDataCursor++] = decodedImage[dataCursor++];
				}

			}

		}

		//then add margins


		for (int x = 1; x < newW - 1; x++)
		{
			//copy on left
			if (x == 1 ||
				(x % (blockSize + 2)) == 1
				)
			{
				for (int y = 0; y < newH; y++)
				{
					*getNew(x - 1, y, 0) = *getNew(x, y, 0);
					*getNew(x - 1, y, 1) = *getNew(x, y, 1);
					*getNew(x - 1, y, 2) = *getNew(x, y, 2);
					*getNew(x - 1, y, 3) = *getNew(x, y, 3);
				}

			}
			else //copy on rigght
				if (x == newW - 2 ||
					(x % (blockSize + 2)) == blockSize
					)
				{
					for (int y = 0; y < newH; y++)
					{
						*getNew(x + 1, y, 0) = *getNew(x, y, 0);
						*getNew(x + 1, y, 1) = *getNew(x, y, 1);
						*getNew(x + 1, y, 2) = *getNew(x, y, 2);
						*getNew(x + 1, y, 3) = *getNew(x, y, 3);
					}
				}
		}

		for (int y = 1; y < newH - 1; y++)
		{
			if (y == 1 ||
				(y % (blockSize + 2)) == 1
				)
			{
				for (int x = 0; x < newW; x++)
				{
					*getNew(x, y - 1, 0) = *getNew(x, y, 0);
					*getNew(x, y - 1, 1) = *getNew(x, y, 1);
					*getNew(x, y - 1, 2) = *getNew(x, y, 2);
					*getNew(x, y - 1, 3) = *getNew(x, y, 3);
				}
			}
			else
				if (y == newH - 2 ||
					(y % (blockSize + 2)) == blockSize
					)
				{
					for (int x = 0; x < newW; x++)
					{
						*getNew(x, y + 1, 0) = *getNew(x, y, 0);
						*getNew(x, y + 1, 1) = *getNew(x, y, 1);
						*getNew(x, y + 1, 2) = *getNew(x, y, 2);
						*getNew(x, y + 1, 3) = *getNew(x, y, 3);
					}
				}

		}

		wgpu2d::Texture t = createTextureFromPixels(newData, newW, newH, label, pixelated, useMipMaps);

		stbi_image_free((void *)decodedImage);
		delete[] newData;
		return t;
	}

	// ---------------------------------------------------------------------
	// Batch: accumulate on the CPU, upload and draw at flush
	// ---------------------------------------------------------------------

	// gl2d's per-corner CPU transform (renderRectangleAbsRotation), collapsed
	// into one matrix:
	//   1. subtract the camera position          (v.x -= cam.x; v.y += cam.y on the flipped y)
	//   2. scale about the screen center by zoom  (scaleAroundPoint with center (w/2, -h/2))
	//   3. pixels to clip space                   (x: 2x/w - 1;  y: 2y/h + 1 on the flipped y)
	// For a world point (x, y) with y down that works out to
	//   ndc.x =  (2 zoom / w) x  - 2 zoom cam.x / w - zoom
	//   ndc.y = -(2 zoom / h) y  + 2 zoom cam.y / h + zoom
	// Checked against gl2d's own functions over 100k random inputs (milestone 5).
	glm::mat4 buildViewProj(const wgpu2d::Camera &cam, float w, float h)
	{
		const float z = cam.zoom;
		glm::mat4 m(1.0f);                     // glm is column-major: m[col][row]
		m[0][0] = 2.0f * z / w;
		m[1][1] = -2.0f * z / h;
		m[3][0] = -2.0f * z * cam.position.x / w - z;
		m[3][1] = 2.0f * z * cam.position.y / h + z;
		return m;
	}

	bool sameCamera(const wgpu2d::Camera &a, const wgpu2d::Camera &b)
	{
		return a.position == b.position && a.zoom == b.zoom;
	}

	// gl2d's rotateAroundPoint, verbatim. It works in gl2d's y-flipped space
	// (callers pass corners with y negated) and negates the pivot's y to
	// match. Ported as-is so positive degrees turn the same way they do today.
	glm::vec2 rotateAroundPoint(glm::vec2 vec, glm::vec2 point, const float degrees)
	{
		point.y = -point.y;
		float a = glm::radians(degrees);
		float s = sinf(a);
		float c = cosf(a);
		vec.x -= point.x;
		vec.y -= point.y;
		float newx = vec.x * c - vec.y * s;
		float newy = vec.x * s + vec.y * c;
		// translate point back:
		vec.x = newx + point.x;
		vec.y = newy + point.y;
		return vec;
	}

	// Makes sure the vertex buffer can hold `bytes`. A GPU buffer cannot be
	// resized, so growth means creating a bigger one (doubling) and releasing
	// the old one; WebGPU keeps the old buffer alive until submitted work
	// that reads it has finished. Never shrinks.
	bool ensureVertexBufferCapacity(uint64_t bytes)
	{
		if (g.vertexBuffer && bytes <= g.vertexBufferCapacityBytes) { return true; }

		uint64_t capacity = g.vertexBufferCapacityBytes ? g.vertexBufferCapacityBytes : 64 * sizeof(Vertex);
		while (capacity < bytes) { capacity *= 2; }

		// Past the early return above, so this is a growth step, not a frame
		// step: doubling means a handful of these over a run, and the scope's
		// stall never lands in the steady state.
		ErrorScope scope("batch vertex buffer");

		BufferDescriptor desc = Default;
		desc.label = StringView("batch vertices");
		desc.usage = BufferUsage::Vertex | BufferUsage::CopyDst;
		desc.size = capacity;
		desc.mappedAtCreation = false;
		Buffer newBuffer = g.device.createBuffer(desc);
		if (!newBuffer)
		{
			std::cerr << "WebGPU: createBuffer (" << capacity << " bytes) returned null\n";
			return false;
		}

		if (g.vertexBuffer) { g.vertexBuffer.release(); }
		g.vertexBuffer = newBuffer;
		g.vertexBufferCapacityBytes = capacity;
		std::cout << "WebGPU vertex buffer capacity: " << capacity / sizeof(Vertex) << " vertices ("
			<< capacity << " bytes)\n";
		std::cout.flush();
		return true;
	}

	// Makes sure the open render pass is the one for `target` (0 = surface,
	// otherwise a texture registry id), beginning it if needed. Passes cannot
	// nest and belong to one attachment, so a change of target ends the
	// current pass and starts another; StoreOp::Store plus LoadOp::Load means
	// nothing drawn earlier is lost.
	//
	// The load operation is where gl2d's glClear went: the surface and the
	// low-res stand-in clear on their first pass of the frame (to the color
	// clearScreen recorded), a user's FrameBuffer loads its previous contents
	// unless it asked to be cleared.
	bool ensurePassBegun(uint32_t target)
	{
		if (!g.frameOpen) { return false; }
		if (g.framePass && g.framePassTarget == target) { return true; }

		// Someone wants the surface while the frame is sitting in the low-res
		// target: upscale it first, so the UI lands on top of the world.
		if (target == 0 && g.scaledTargetDrawn && !g.compositing)
		{
			compositeScaledTarget();
			if (g.framePass && g.framePassTarget == 0) { return true; }
		}

		if (g.framePass)
		{
			g.framePass.end();
			g.framePass.release();
			g.framePass = nullptr;
		}

		TextureView view = nullptr;
		bool clear = false;
		glm::vec4 clearColor = {0, 0, 0, 0};
		if (target == 0)
		{
			view = g.frameView;
			clear = !g.surfaceCleared;
			clearColor = g.clearColor;
			g.surfaceCleared = true;
		}
		else if (target <= textures.size())
		{
			TextureEntry &entry = textures[target - 1];
			view = entry.view;
			if (isFrameTarget(target))
			{
				clear = !g.scaledCleared;
				clearColor = g.clearColor;
				g.scaledCleared = true;
			}
			else
			{
				clear = entry.needsClear;
			}
			entry.needsClear = false;
		}
		if (!view)
		{
			std::cerr << "WebGPU: no view for render target " << target << "\n";
			return false;
		}

		RenderPassColorAttachment colorAttachment = Default;
		colorAttachment.view = view;
		colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED; // required for a 2D target
		colorAttachment.resolveTarget = nullptr;
		colorAttachment.loadOp = clear ? LoadOp::Clear : LoadOp::Load;
		colorAttachment.storeOp = StoreOp::Store;
		colorAttachment.clearValue.r = clearColor.r;
		colorAttachment.clearValue.g = clearColor.g;
		colorAttachment.clearValue.b = clearColor.b;
		colorAttachment.clearValue.a = clearColor.a;

		RenderPassDescriptor passDesc = Default;
		passDesc.label = StringView(target == 0 ? "surface pass" : "render target pass");
		passDesc.colorAttachmentCount = 1;
		passDesc.colorAttachments = &colorAttachment;
		passDesc.depthStencilAttachment = nullptr;
		passDesc.occlusionQuerySet = nullptr;
		passDesc.timestampWrites = nullptr;

		g.framePass = g.frameEncoder.beginRenderPass(passDesc);
		g.framePassTarget = target;
		return (bool)g.framePass;
	}

	void clearBatch()
	{
		batchVertices.clear();
		batchQuadTextures.clear();
		batchQuadCameras.clear();
		batchQuadBlends.clear();
		frameCameras.clear();
	}

	// gl2d's internalFlush: upload the batch, then one draw per run of
	// consecutive quads that share a texture, extended to also break when
	// the camera changes. Order is preserved, so overlap and transparency
	// come out as they do today.
	//
	// `target` is where it lands: 0 for the screen (gl2d's flush), a texture
	// registry id for a render target (gl2d's flushFBO). The camera
	// projection uses the surface's dimensions for the screen -- including
	// when the low-res stand-in is standing in for it, which is what keeps
	// the framing identical at any render scale -- and a render target's own
	// dimensions otherwise.
	void flushBatch(uint32_t target)
	{
		if (batchVertices.empty()) { return; }

		float projectionWidth = (float)g.surfaceWidth;
		float projectionHeight = (float)g.surfaceHeight;
		if (target != 0 && !isFrameTarget(target) && target <= textures.size())
		{
			projectionWidth = (float)textures[target - 1].width;
			projectionHeight = (float)textures[target - 1].height;
		}

		if (!ensurePassBegun(target)) { return; }
		if (target != 0) { g.scaledTargetDrawn = g.scaledTargetDrawn || isFrameTarget(target); }

		const uint64_t bytes = batchVertices.size() * sizeof(Vertex);
		static_assert(sizeof(Vertex) % 4 == 0, "writeBuffer size must be a multiple of 4");
		const uint64_t vertexOffset = g.vertexBufferUsedBytes; // this flush's slice
		if (!ensureVertexBufferCapacity(vertexOffset + bytes)) { return; }

		// Queue-ordered: the copy lands after last frame's draw has finished
		// reading this buffer, so one buffer is enough without double
		// buffering -- as long as this frame's earlier flushes keep their
		// slices, which is what the offset is for.
		g.queue.writeBuffer(g.vertexBuffer, vertexOffset, batchVertices.data(), bytes);
		g.vertexBufferUsedBytes = vertexOffset + bytes;

		// One matrix per camera used in this flush, each in its own slot,
		// after the slots earlier flushes in this frame are still using.
		const uint32_t cameraSlotBase = g.cameraSlotsUsed;
		if (!ensureCameraSlotCapacity(cameraSlotBase + (uint32_t)frameCameras.size())) { return; }
		for (size_t i = 0; i < frameCameras.size(); i++)
		{
			CameraUniforms uniforms;
			uniforms.viewProj = buildViewProj(frameCameras[i], projectionWidth, projectionHeight);
			g.queue.writeBuffer(g.uniformBuffer, (uint64_t)(cameraSlotBase + i) * g.cameraSlotStride,
				&uniforms, sizeof(uniforms));
		}
		g.cameraSlotsUsed = cameraSlotBase + (uint32_t)frameCameras.size();

		RenderPassEncoder pass = g.framePass;

		// One group per flush, so a capture reads as "world", "scaled
		// composite", "HUD target" rather than a flat run of draws. The pass
		// cannot change inside this function -- ensurePassBegun already ran --
		// so the group is safe for the whole body.
		const char *groupName = g.compositing ? "composite scaled target"
			: target == 0 ? "batch -> surface"
			: isFrameTarget(target) ? "batch -> scaled target"
			: "batch -> render target";
		DebugGroup group(pass, groupName);

		// The attachment's format is fixed for the whole flush -- every quad
		// here lands on the same view -- so it is half of every pipeline key
		// below and is resolved once.
		//
		// Note this does *not* mirror the projection above, which deliberately
		// uses the surface's dimensions for the low-res stand-in so framing
		// survives a render scale. Format has no such reason: a pipeline must
		// match the attachment it actually draws into, stand-in or not.
		TextureFormat targetFormat = g.surfaceFormat;
		if (target != 0 && target <= textures.size())
		{
			targetFormat = textures[target - 1].format;
		}

		// The bound slice starts at this flush's offset, so the per-draw
		// firstVertex below stays relative to the batch.
		pass.setVertexBuffer(0, g.vertexBuffer, vertexOffset, bytes);

		const size_t quadCount = batchQuadTextures.size();
		size_t runStart = 0;
		uint32_t runs = 0;
		RenderPipeline boundPipeline = nullptr; // only re-set when it changes
		for (size_t i = 1; i <= quadCount; i++)
		{
			// R2 adds the third component. Texture and camera change which
			// resources are bound; blend changes which *pipeline* is bound,
			// because blending is baked into the pipeline and cannot be set
			// on a pass.
			const bool boundary = (i == quadCount)
				|| batchQuadTextures[i] != batchQuadTextures[runStart]
				|| batchQuadCameras[i] != batchQuadCameras[runStart]
				|| batchQuadBlends[i] != batchQuadBlends[runStart];
			if (!boundary) { continue; }

			RenderPipeline pipeline = getQuadPipeline(PipelineKey{ targetFormat, batchQuadBlends[runStart] });
			if (!pipeline)
			{
				// The variant could not be built. Skipping the run loses those
				// quads; drawing with the wrong pipeline would be a validation
				// error every frame.
				runStart = i;
				continue;
			}
			if (pipeline != boundPipeline)
			{
				pass.setPipeline(pipeline);
				boundPipeline = pipeline;
			}

			const uint32_t dynamicOffset = (cameraSlotBase + batchQuadCameras[runStart]) * g.cameraSlotStride;
			pass.setBindGroup(0, textures[batchQuadTextures[runStart] - 1].bindGroup, 0, nullptr); // texture + sampler
			pass.setBindGroup(1, g.cameraBindGroup, 1, &dynamicOffset);                          // camera slot
			pass.draw((uint32_t)((i - runStart) * 6), 1, (uint32_t)(runStart * 6), 0);
			runs++;
			runStart = i;
		}

		statsInProgress.quads += (int)quadCount;
		statsInProgress.drawRuns += (int)runs;
		statsInProgress.cameras += (int)frameCameras.size();
		statsInProgress.flushes += 1;

		if (!g.runStatsPrinted)
		{
			g.runStatsPrinted = true;
			std::cout << "WebGPU first flush: " << quadCount << " quads, " << frameCameras.size()
				<< " cameras, " << runs << " draw runs\n";
			std::cout.flush();
		}
	}

	// gl2d's renderRectangleAbsRotation up to (not including) its camera
	// steps, which are now the matrix. Corners are built and rotated in
	// gl2d's flipped space, then flipped back into world pixels for the
	// vertex buffer. Texture coordinates arrive in gl2d's convention
	// ({u0, v0, u1, v1} with v measured from the bottom, default {0,1,1,0})
	// and are converted to WebGPU's top-left origin here: v = 1 - v.
	void pushQuad(const wgpu2d::Camera &camera, wgpu2d::BlendMode blend,
		const glm::vec4 transforms, const wgpu2d::Texture texture,
		const glm::vec4 colors[4], const glm::vec2 origin, const float rotation, const glm::vec4 textureCoords)
	{
		wgpu2d::Texture textureCopy = texture;
		if (textureCopy.id == 0 || textureCopy.id > textures.size() || !textures[textureCopy.id - 1].bindGroup)
		{
			std::cerr << "wgpu2d: Invalid texture (id " << textureCopy.id << ")\n";
			textureCopy = white1pxSquareTexture;
			if (textureCopy.id == 0) { return; }
		}

		//We need to flip texture_transforms.y
		const float transformsY = transforms.y * -1;

		glm::vec2 v1 = { transforms.x,				  transformsY };
		glm::vec2 v2 = { transforms.x,				  transformsY - transforms.w };
		glm::vec2 v3 = { transforms.x + transforms.z, transformsY - transforms.w };
		glm::vec2 v4 = { transforms.x + transforms.z, transformsY };

		//Apply rotations
		if (rotation != 0)
		{
			v1 = rotateAroundPoint(v1, origin, rotation);
			v2 = rotateAroundPoint(v2, origin, rotation);
			v3 = rotateAroundPoint(v3, origin, rotation);
			v4 = rotateAroundPoint(v4, origin, rotation);
		}

		// Back to world pixels (y down). gl2d continued with camera offset,
		// zoom, and NDC here; the vertex shader does that now.
		v1.y = -v1.y; v2.y = -v2.y; v3.y = -v3.y; v4.y = -v4.y;

		const float u0 = textureCoords.x, v0 = 1.0f - textureCoords.y;
		const float u1 = textureCoords.z, v1t = 1.0f - textureCoords.w;

		auto push = [&](glm::vec2 p, const glm::vec4 &c, float u, float v)
		{
			batchVertices.push_back(Vertex{ p.x, p.y, c.r, c.g, c.b, c.a, u, v });
		};

		// gl2d's corner order: v1 v2 v4, v2 v3 v4, with gl2d's uv assignment.
		push(v1, colors[0], u0, v0);
		push(v2, colors[1], u0, v1t);
		push(v4, colors[3], u1, v0);
		push(v2, colors[1], u0, v1t);
		push(v3, colors[2], u1, v1t);
		push(v4, colors[3], u1, v0);

		batchQuadTextures.push_back(textureCopy.id);

		// Record which camera this quad was drawn under. A new slot only when
		// the camera changed since the last recorded one.
		if (frameCameras.empty() || !sameCamera(frameCameras.back(), camera))
		{
			frameCameras.push_back(camera);
		}
		batchQuadCameras.push_back((uint32_t)frameCameras.size() - 1);
		batchQuadBlends.push_back(blend);
	}

	// 10: the upscale. One quad covering the surface, sampling the low-res
	// target with nearest filtering, drawn through the ordinary sprite path
	// (the target is a normal texture handle, so no extra pipeline or shader
	// is needed). Any quads the game has recorded but not flushed are put
	// aside and restored, so this never eats the caller's batch.
	void compositeScaledTarget()
	{
		if (!g.scaledTargetId || !g.scaledTargetDrawn || g.compositing) { return; }
		g.compositing = true;

		std::vector<Vertex> keptVertices;
		std::vector<uint32_t> keptTextures;
		std::vector<uint32_t> keptCameras;
		std::vector<wgpu2d::Camera> keptFrameCameras;
		keptVertices.swap(batchVertices);
		keptTextures.swap(batchQuadTextures);
		keptCameras.swap(batchQuadCameras);
		keptFrameCameras.swap(frameCameras);

		wgpu2d::Texture handle;
		handle.id = g.scaledTargetId;
		const glm::vec4 white[4] = { {1,1,1,1}, {1,1,1,1}, {1,1,1,1}, {1,1,1,1} };
		// The target's pixels are premultiplied by their own coverage, so
		// this is Premultiplied regardless of what the quads inside it used.
		// The world target is cleared opaque, so today this is numerically
		// identical to Alpha -- it stops being identical the moment anything
		// clears it to anything translucent.
		pushQuad(wgpu2d::Camera{}, wgpu2d::BlendMode::Premultiplied,
			glm::vec4{0, 0, (float)g.surfaceWidth, (float)g.surfaceHeight},
			handle, white, {}, 0.f, WGPU2D_DefaultTextureCoords);
		g.scaledTargetDrawn = false; // the pass below is the surface's, not the target's
		flushBatch(0);

		batchVertices.swap(keptVertices);
		batchQuadTextures.swap(keptTextures);
		batchQuadCameras.swap(keptCameras);
		frameCameras.swap(keptFrameCameras);
		g.compositing = false;
	}

	// ---------------------------------------------------------------------
	// Frame capture
	// ---------------------------------------------------------------------
	//
	// Three steps, split across the frame because the GPU is not synchronous:
	// record a copy from the frame's texture into a buffer (before submit),
	// submit, then map the buffer and read it (after submit). The map is
	// asynchronous like every other WebGPU callback, and is drained with the
	// same bounded pump the error scopes use.
	//
	// Two rules the copy imposes, and both are the caller's problem if they
	// leak: bytesPerRow must be a multiple of 256, so the buffer's rows are
	// padded and unpadded here; and the surface is BGRA on this backend, so
	// the channels are swizzled here. What comes out is tightly packed RGBA.
	struct FrameCapture
	{
		bool requested = false;
		bool copied = false;
		bool ready = false;
		Buffer buffer = nullptr;
		uint64_t bufferBytes = 0;
		uint32_t rowBytes = 0;
		int width = 0;
		int height = 0;
		std::vector<unsigned char> pixels;
		bool mapDone = false;
	};
	FrameCapture capture;

	// Records the copy. Called from wgpuEndFrame after the pass has ended and
	// before the encoder is finished -- a copy cannot be recorded inside a
	// render pass.
	void captureRecordCopy()
	{
		if (!capture.requested || !g.frameTexture || g.surfaceWidth <= 0 || g.surfaceHeight <= 0)
		{
			return;
		}

		capture.width = g.surfaceWidth;
		capture.height = g.surfaceHeight;
		capture.rowBytes = ceilToNextMultiple((uint32_t)capture.width * 4, 256);
		const uint64_t needed = (uint64_t)capture.rowBytes * capture.height;

		if (!capture.buffer || capture.bufferBytes < needed)
		{
			if (capture.buffer) { capture.buffer.release(); }
			BufferDescriptor desc = Default;
			desc.label = StringView("frame capture");
			desc.usage = BufferUsage::CopyDst | BufferUsage::MapRead;
			desc.size = needed;
			desc.mappedAtCreation = false;

			ErrorScope scope("frame capture buffer");
			capture.buffer = g.device.createBuffer(desc);
			if (!capture.buffer || scope.failed())
			{
				std::cerr << "WebGPU: frame capture buffer could not be created\n";
				capture.requested = false;
				return;
			}
			capture.bufferBytes = needed;
		}

		TexelCopyTextureInfo source = Default;
		source.texture = g.frameTexture;
		source.mipLevel = 0;
		source.origin = { 0, 0, 0 };
		source.aspect = TextureAspect::All;

		TexelCopyBufferInfo destination = Default;
		destination.buffer = capture.buffer;
		destination.layout.offset = 0;
		destination.layout.bytesPerRow = capture.rowBytes;
		destination.layout.rowsPerImage = (uint32_t)capture.height;

		Extent3D size = { (uint32_t)capture.width, (uint32_t)capture.height, 1 };
		g.frameEncoder.copyTextureToBuffer(source, destination, size);
		capture.copied = true;
	}

	void onCaptureMapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *)
	{
		*static_cast<bool *>(userdata1) = true;
		if (status != MapAsyncStatus::Success)
		{
			std::cerr << "WebGPU: frame capture mapAsync failed: " << StringView(message) << "\n";
		}
	}

	// Maps the buffer and unpacks it. Called from wgpuEndFrame after submit.
	// This blocks, which is why a capture is a one-off the application asks
	// for rather than something that happens every frame.
	void captureResolve()
	{
		if (!capture.copied) { return; }
		capture.copied = false;
		capture.requested = false;

		const size_t total = (size_t)capture.bufferBytes;
		capture.mapDone = false;

		BufferMapCallbackInfo info = Default;
		info.mode = CallbackMode::AllowProcessEvents;
		info.callback = onCaptureMapped;
		info.userdata1 = &capture.mapDone;
		info.userdata2 = nullptr;
		capture.buffer.mapAsync(MapMode::Read, 0, total, info);

		for (int i = 0; !capture.mapDone && i < 3000; i++)
		{
			g.instance.processEvents();
			if (!capture.mapDone) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
		}
		if (!capture.mapDone)
		{
			std::cerr << "WebGPU: frame capture never mapped\n";
			return;
		}

		const unsigned char *src = static_cast<const unsigned char *>(
			capture.buffer.getConstMappedRange(0, total));
		if (!src)
		{
			std::cerr << "WebGPU: frame capture mapped range was null\n";
			capture.buffer.unmap();
			return;
		}

		capture.pixels.resize((size_t)capture.width * capture.height * 4);
		const bool bgra = (g.surfaceFormat == TextureFormat::BGRA8Unorm
			|| g.surfaceFormat == TextureFormat::BGRA8UnormSrgb);
		for (int y = 0; y < capture.height; y++)
		{
			const unsigned char *row = src + (size_t)y * capture.rowBytes;
			unsigned char *out = capture.pixels.data() + (size_t)y * capture.width * 4;
			for (int x = 0; x < capture.width; x++)
			{
				const unsigned char *p = row + (size_t)x * 4;
				unsigned char *q = out + (size_t)x * 4;
				q[0] = bgra ? p[2] : p[0];
				q[1] = p[1];
				q[2] = bgra ? p[0] : p[2];
				q[3] = 255; // the surface has no meaningful alpha to hand on
			}
		}

		capture.buffer.unmap();
		capture.ready = true;
	}

	// Adapter features are printed by name, not just counted: whether a
	// machine has TimestampQuery decides whether GPU timing is possible at
	// all, and that is worth knowing from a run's own output rather than from
	// a one-off probe. Unknown values print as numbers -- wgpu-native has its
	// own beyond the spec's list.
	const char *featureName(WGPUFeatureName f)
	{
		switch (f)
		{
			case WGPUFeatureName_DepthClipControl: return "DepthClipControl";
			case WGPUFeatureName_Depth32FloatStencil8: return "Depth32FloatStencil8";
			case WGPUFeatureName_TimestampQuery: return "TimestampQuery";
			case WGPUFeatureName_TextureCompressionBC: return "TextureCompressionBC";
			case WGPUFeatureName_TextureCompressionETC2: return "TextureCompressionETC2";
			case WGPUFeatureName_TextureCompressionASTC: return "TextureCompressionASTC";
			case WGPUFeatureName_IndirectFirstInstance: return "IndirectFirstInstance";
			case WGPUFeatureName_ShaderF16: return "ShaderF16";
			case WGPUFeatureName_RG11B10UfloatRenderable: return "RG11B10UfloatRenderable";
			case WGPUFeatureName_BGRA8UnormStorage: return "BGRA8UnormStorage";
			case WGPUFeatureName_Float32Filterable: return "Float32Filterable";
			default: return nullptr;
		}
	}

	void printAdapter(Adapter adapter)
	{
		AdapterInfo info = Default;
		if (adapter.getInfo(&info) == Status::Success)
		{
			std::cout << "WebGPU adapter\n"
				<< "  device:       " << StringView(info.device) << "\n"
				<< "  vendor:       " << StringView(info.vendor) << " (0x" << std::hex << info.vendorID << std::dec << ")\n"
				<< "  architecture: " << StringView(info.architecture) << "\n"
				<< "  description:  " << StringView(info.description) << "\n"
				<< "  backend:      " << backendName(info.backendType) << "\n"
				<< "  type:         " << adapterTypeName(info.adapterType) << "\n";
			info.freeMembers();
		}
		else
		{
			std::cerr << "WebGPU: adapter.getInfo failed\n";
		}

		// The limits that matter for a 2D sprite batcher. minUniformBufferOffsetAlignment
		// decides the stride of the per-camera uniform slots.
		Limits limits = Default;
		if (adapter.getLimits(&limits) == Status::Success)
		{
			g.minUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
			std::cout << "  limits:\n"
				<< "    maxTextureDimension2D:          " << limits.maxTextureDimension2D << "\n"
				<< "    maxBindGroups:                  " << limits.maxBindGroups << "\n"
				<< "    maxVertexBuffers:               " << limits.maxVertexBuffers << "\n"
				<< "    maxVertexAttributes:            " << limits.maxVertexAttributes << "\n"
				<< "    maxBufferSize:                  " << limits.maxBufferSize << "\n"
				<< "    maxUniformBufferBindingSize:    " << limits.maxUniformBufferBindingSize << "\n"
				<< "    minUniformBufferOffsetAlignment: " << limits.minUniformBufferOffsetAlignment << "\n";
		}
		else
		{
			std::cerr << "WebGPU: adapter.getLimits failed\n";
		}

		SupportedFeatures features = Default;
		adapter.getFeatures(&features);
		std::cout << "  features: " << features.featureCount << "\n";
		for (size_t i = 0; i < features.featureCount; i++)
		{
			const WGPUFeatureName f = features.features[i];
			const char *name = featureName(f);
			std::cout << "    ";
			if (name) { std::cout << name; }
			else { std::cout << "0x" << std::hex << (uint32_t)f << std::dec; }
			std::cout << "\n";
		}
		features.freeMembers();

		// Flush so the report survives even if the process is killed while the
		// window is open (stdout is fully buffered when redirected to a file).
		std::cout.flush();
	}
}

// -------------------------------------------------------------------------
// Context lifecycle, called from the platform loop
// -------------------------------------------------------------------------

WGPUInstance wgpuInitInstance()
{
	if (g.instance) { return g.instance; }

	wgpuSetLogCallback(logCallback, nullptr);
	// Warn and above by default. WGPU_LOG_LEVEL raises it without a rebuild;
	// "info" and "debug" say what the backend is doing between our calls,
	// which is the level worth reaching for when a scope reports something
	// that the descriptor alone does not explain.
	WGPULogLevel logLevel = WGPULogLevel_Warn;
	if (const char *level = getenv("WGPU_LOG_LEVEL"))
	{
		const std::string wanted = level;
		if (wanted == "off") { logLevel = WGPULogLevel_Off; }
		else if (wanted == "error") { logLevel = WGPULogLevel_Error; }
		else if (wanted == "warn") { logLevel = WGPULogLevel_Warn; }
		else if (wanted == "info") { logLevel = WGPULogLevel_Info; }
		else if (wanted == "debug") { logLevel = WGPULogLevel_Debug; }
		else if (wanted == "trace") { logLevel = WGPULogLevel_Trace; }
		else { std::cerr << "WGPU_LOG_LEVEL: unknown level '" << wanted << "', keeping warn\n"; }
	}
	wgpuSetLogLevel(logLevel);

	// The instance is the library itself: no GPU, no window. It exists before
	// wgpuInit because creating a surface needs it, and creating a surface is
	// the application's job -- only the application knows what a window is.
	InstanceDescriptor instanceDesc = Default;
	g.instance = createInstance(instanceDesc);
	if (!g.instance)
	{
		std::cerr << "WebGPU: createInstance returned null\n";
		return nullptr;
	}
	return g.instance;
}

bool wgpuInit(WGPUSurface surface, int width, int height)
{
	if (!g.instance)
	{
		std::cerr << "WebGPU: wgpuInit before wgpuInitInstance\n";
		return false;
	}
	if (!surface)
	{
		std::cerr << "WebGPU: wgpuInit given a null surface\n";
		return false;
	}
	g.surface = surface;

	// Any return false below must drop the borrowed surface without
	// releasing it. The flag is set only on the success path.
	struct DropSurfaceIfFailed
	{
		bool keep = false;
		~DropSurfaceIfFailed()
		{
			if (!keep) { forgetSurface(); }
		}
	} dropSurface;

	// The macOS colour-space pin used to happen here. It needs the window, so
	// it moved out with GLFW -- glfwMain calls it right after creating the
	// surface. See platform/wgpuMetalLayer.h.

	// Adapter: a description of one physical GPU that fits the options.
	//    The request is callback-based; on native backends it completes
	//    during processEvents, so the loop below runs at most a few times.
	RequestAdapterOptions options = Default;
	options.featureLevel = FeatureLevel::Core;
	options.powerPreference = PowerPreference::Undefined;
	options.forceFallbackAdapter = false;
	options.backendType = BackendType::Undefined;
	options.compatibleSurface = g.surface;

	AdapterRequest request;
	RequestAdapterCallbackInfo callbackInfo = Default;
	callbackInfo.mode = CallbackMode::AllowProcessEvents;
	callbackInfo.callback = onAdapterRequest;
	callbackInfo.userdata1 = &request;
	callbackInfo.userdata2 = nullptr;

	g.instance.requestAdapter(options, callbackInfo);

	for (int i = 0; !request.done && i < 1000; i++)
	{
		g.instance.processEvents();
		if (!request.done)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	if (!request.done)
	{
		std::cerr << "WebGPU: requestAdapter never completed\n";
		return false;
	}
	if (!request.adapter)
	{
		return false;
	}
	g.adapter = request.adapter;

	printAdapter(g.adapter);

	// Device: the working connection to the GPU. Every later object is
	//    created from it. No required features or limits: the defaults are
	//    far above what a 2D sprite batcher needs.
	DeviceDescriptor deviceDesc = Default;
	deviceDesc.label = StringView("space-game device");
	deviceDesc.requiredFeatureCount = 0;
	deviceDesc.requiredFeatures = nullptr;
	deviceDesc.requiredLimits = nullptr;
	deviceDesc.defaultQueue.label = StringView("space-game queue");
	deviceDesc.deviceLostCallbackInfo.mode = CallbackMode::AllowProcessEvents;
	deviceDesc.deviceLostCallbackInfo.callback = onDeviceLost;
	deviceDesc.deviceLostCallbackInfo.userdata1 = nullptr;
	deviceDesc.deviceLostCallbackInfo.userdata2 = nullptr;
	deviceDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
	deviceDesc.uncapturedErrorCallbackInfo.userdata1 = nullptr;
	deviceDesc.uncapturedErrorCallbackInfo.userdata2 = nullptr;

	DeviceRequest deviceRequest;
	RequestDeviceCallbackInfo deviceCallbackInfo = Default;
	deviceCallbackInfo.mode = CallbackMode::AllowProcessEvents;
	deviceCallbackInfo.callback = onDeviceRequest;
	deviceCallbackInfo.userdata1 = &deviceRequest;
	deviceCallbackInfo.userdata2 = nullptr;

	g.adapter.requestDevice(deviceDesc, deviceCallbackInfo);

	for (int i = 0; !deviceRequest.done && i < 1000; i++)
	{
		g.instance.processEvents();
		if (!deviceRequest.done)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	if (!deviceRequest.done)
	{
		std::cerr << "WebGPU: requestDevice never completed\n";
		return false;
	}
	if (!deviceRequest.device)
	{
		return false;
	}
	g.device = deviceRequest.device;

	// Queue: the device's single inbox for command buffers and uploads.
	g.queue = g.device.getQueue();
	if (!g.queue)
	{
		std::cerr << "WebGPU: device.getQueue returned null\n";
		return false;
	}

	// Surface format. The first listed format is the surface's preferred
	//    one and on Metal it is normally an sRGB variant. gl2d never gamma
	//    corrected, so prefer the plain (non-sRGB) 8-bit format when offered
	//    and only fall back to the preferred one otherwise.
	SurfaceCapabilities caps = Default;
	if (g.surface.getCapabilities(g.adapter, &caps) != Status::Success || caps.formatCount == 0)
	{
		std::cerr << "WebGPU: surface.getCapabilities failed or listed no formats\n";
		return false;
	}

	g.surfaceFormat = caps.formats[0];
	std::cout << "WebGPU surface formats offered:";
	for (size_t i = 0; i < caps.formatCount; i++)
	{
		std::cout << " " << formatName(caps.formats[i]);
	}
	std::cout << "\n";
	for (size_t i = 0; i < caps.formatCount; i++)
	{
		if (caps.formats[i] == TextureFormat::BGRA8Unorm || caps.formats[i] == TextureFormat::RGBA8Unorm)
		{
			g.surfaceFormat = caps.formats[i];
			break;
		}
	}
	std::cout << "WebGPU surface format chosen: " << formatName(g.surfaceFormat) << "\n";
	caps.freeMembers();

	// Configure the surface: this is the OpenGL default framebuffer plus
	//    swap interval, expressed as an explicit object.
	configureSurface(width, height);
	if (!g.surfaceConfigured)
	{
		std::cerr << "WebGPU: surface not configured (framebuffer size "
			<< g.surfaceWidth << "x" << g.surfaceHeight << ")\n";
		return false;
	}

	// Samplers and layouts. The pipeline needs the layout.
	if (!createSamplers() || !createLayouts())
	{
		return false;
	}

	// The sprite shader, compiled once and shared by every pipeline
	//    variant, then the one variant the common case needs: the surface's
	//    format with gl2d's alpha blend. The rest are built the first time a
	//    flush asks for them. Warming this one here keeps a validation
	//    failure in the shader or the layout at startup, where it can stop
	//    init, rather than in the first frame.
	g.quadShaderModule = createShaderModule(quadShaderWGSL, "quad.wgsl");
	if (!g.quadShaderModule)
	{
		return false;
	}
	if (!getQuadPipeline(PipelineKey{ g.surfaceFormat, wgpu2d::BlendMode::Alpha }))
	{
		return false;
	}
	// getQuadPipeline already reported the variant; no second line for it.

	// gl2d's 1px white texture: what every untextured draw samples.
	white1pxSquareTexture.create1PxSquare();
	if (white1pxSquareTexture.id == 0)
	{
		return false;
	}

	// The camera uniform slots. Grow on demand.
	if (!ensureCameraSlotCapacity(1))
	{
		return false;
	}

	// 12. Optional low-resolution rendering (milestone 10). Off unless
	//     WGPU_RENDER_SCALE is set to something below 1.
	if (const char *scale = getenv("WGPU_RENDER_SCALE"))
	{
		g.renderScale = (float)atof(scale);
		if (!(g.renderScale > 0.f) || g.renderScale > 1.f)
		{
			std::cerr << "WebGPU: ignoring WGPU_RENDER_SCALE=" << scale << " (expected 0 < scale <= 1)\n";
			g.renderScale = 1.f;
		}
		else if (g.renderScale < 1.f)
		{
			std::cout << "WebGPU render scale: " << g.renderScale
				<< " (the world is drawn into an offscreen target and upscaled)\n";
		}
	}
	ensureScaledTarget();

	std::cout.flush();
	dropSurface.keep = true;
	return true;
}

void wgpuRequestFrameCapture()
{
	capture.requested = true;
	capture.ready = false;
}

bool wgpuTakeFrameCapture(std::vector<unsigned char> &rgba, int &width, int &height)
{
	if (!capture.ready) { return false; }
	capture.ready = false;
	rgba = std::move(capture.pixels);
	capture.pixels.clear();
	width = capture.width;
	height = capture.height;
	return true;
}

void wgpuResize(int width, int height)
{
	if (!g.surface) { return; }
	if (g.surfaceConfigured && width == g.surfaceWidth && height == g.surfaceHeight) { return; }

	configureSurface(width, height);
	if (g.surfaceConfigured)
	{
		ensureScaledTarget(); // the low-res target follows the surface's size
	}
}

void wgpuBeginFrame()
{
	g.frameOpen = false;
	statsInProgress = wgpu2d::FrameStats{};
	framePerf = FramePerf{};

	// The surface must have been configured, which wgpuResize does. It is the
	// application's job to call that with the current framebuffer size --
	// every frame is fine and is what this game does, because on Metal
	// wgpu-native keeps presenting a drawable of the configured size and
	// stretches it to the window rather than ever reporting the surface
	// Outdated. The Outdated/Lost path below stays as a backstop for backends
	// that do report it.
	if (!g.surfaceConfigured) { return; } // minimized, or resize not pushed yet

	// Acquire: the texture that will next go on screen.
	SurfaceTexture surfaceTexture = Default;
	g.surface.getCurrentTexture(&surfaceTexture);
	wgpu::Texture texture = surfaceTexture.texture;

	switch (surfaceTexture.status)
	{
		case SurfaceGetCurrentTextureStatus::SuccessOptimal:
		case SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
			break;

		case SurfaceGetCurrentTextureStatus::Timeout:
		case SurfaceGetCurrentTextureStatus::Outdated:
		case SurfaceGetCurrentTextureStatus::Lost:
			// Reconfigure at the size we believe and try again next frame. If
			// the surface went Outdated *because* the window changed size,
			// the application's next wgpuResize corrects it -- this path is
			// the backstop for backends that report Outdated at all, which
			// Metal does not.
			if (texture) { texture.release(); }
			configureSurface(g.surfaceWidth, g.surfaceHeight);
			return;

		default:
			std::cerr << "WebGPU: surface.getCurrentTexture failed (status "
				<< surfaceTexture.status << ")\n";
			if (texture) { texture.release(); }
			return;
	}

	// View: render passes attach to a view of a texture, never the texture.
	TextureViewDescriptor viewDesc = Default;
	viewDesc.label = StringView("surface view");
	viewDesc.format = g.surfaceFormat;
	viewDesc.dimension = TextureViewDimension::_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = TextureAspect::All;
	viewDesc.usage = TextureUsage::RenderAttachment;

	// Record: a command encoder collects GPU work; nothing runs yet.
	CommandEncoderDescriptor encoderDesc = Default;
	encoderDesc.label = StringView("frame encoder");

	g.frameTexture = texture;
	g.frameView = texture.createView(viewDesc);
	g.frameEncoder = g.device.createCommandEncoder(encoderDesc);
	g.framePass = nullptr;
	g.framePassTarget = 0;
	g.surfaceCleared = false;
	g.scaledCleared = false;
	g.scaledTargetDrawn = false;
	g.vertexBufferUsedBytes = 0;
	g.cameraSlotsUsed = 0;
	g.clearColor = {0, 0, 0, 1}; // gl2d's glClear default until clearScreen says otherwise
	g.frameOpen = true;
}

void wgpuEndFrame()
{
	if (!g.frameOpen) { return; }

	// Nothing asked for the surface this frame (no UI, no screen-space draw):
	// upscale the low-res target now.
	compositeScaledTarget();

	// A frame with no draws still clears; begin the pass so the load op runs.
	ensurePassBegun(0);
	if (g.framePass)
	{
		g.framePass.end();
		g.framePass.release();
		g.framePass = nullptr;
		g.framePassTarget = 0;
	}

	captureRecordCopy(); // before finish(): a copy cannot go inside a pass

	// Seal the recording into a command buffer, submit, present.
	CommandBufferDescriptor cmdDesc = Default;
	cmdDesc.label = StringView("frame commands");
	CommandBuffer commands = g.frameEncoder.finish(cmdDesc);
	g.frameEncoder.release();
	g.frameEncoder = nullptr;

	g.queue.submit(1, &commands);
	commands.release();

	// N2b. Only one in flight: if the previous frame's work has not been
	// reported yet, this frame goes unmeasured rather than overwriting the
	// start time out from under it.
	if (!workDonePending)
	{
		QueueWorkDoneCallbackInfo workInfo = Default;
		workInfo.mode = CallbackMode::AllowProcessEvents;
		workInfo.callback = onQueueWorkDone;
		workInfo.userdata1 = nullptr;
		workInfo.userdata2 = nullptr;
		submitAt = std::chrono::steady_clock::now();
		workDonePending = true;
		g.queue.onSubmittedWorkDone(workInfo);
	}

	captureResolve(); // after submit: the copy has to have run

	g.surface.present(); // this is glfwSwapBuffers

	g.frameView.release();
	g.frameView = nullptr;
	g.frameTexture.release();
	g.frameTexture = nullptr;
	g.frameOpen = false;

	// Let pending callbacks (errors, device lost, queue work done) run once
	// per frame. The work-done callback for an earlier frame usually lands
	// here, which is why gpuMillis lags by a frame or two.
	g.instance.processEvents();

	statsInProgress.pipelineVariants = 0;
	for (const PipelineEntry &e : g.quadPipelines)
	{
		if (e.pipeline) { ++statsInProgress.pipelineVariants; } // known-bad entries do not count
	}
	statsInProgress.gpuMillis = lastGpuMillis;
	statsInProgress.pipelineBuilds = framePerf.pipelineBuilds;
	statsInProgress.pipelineFailures = framePerf.pipelineFailures;
	statsInProgress.validationErrors = framePerf.validationErrors;
	statsInProgress.texturesCreated = framePerf.texturesCreated;
	statsInProgress.blockedMs = framePerf.blockedMs;
	statsLastFrame = statsInProgress;

	if (!g.firstFramePresented)
	{
		g.firstFramePresented = true;
		std::cout << "WebGPU first frame presented\n";
		std::cout.flush();
	}
}

void wgpuShutdown()
{
	if (g.frameOpen)
	{
		// Shouldn't happen (the loop always ends the frame), but never leave
		// an encoder dangling.
		if (g.framePass) { g.framePass.end(); g.framePass.release(); g.framePass = nullptr; }
		if (g.frameEncoder) { g.frameEncoder.release(); g.frameEncoder = nullptr; }
		if (g.frameView) { g.frameView.release(); g.frameView = nullptr; }
		if (g.frameTexture) { g.frameTexture.release(); g.frameTexture = nullptr; }
		g.frameOpen = false;
	}
	clearBatch();
	if (g.cameraBindGroup) { g.cameraBindGroup.release(); g.cameraBindGroup = nullptr; }
	if (g.uniformBuffer) { g.uniformBuffer.release(); g.uniformBuffer = nullptr; g.cameraSlotCapacity = 0; }
	if (g.vertexBuffer) { g.vertexBuffer.release(); g.vertexBuffer = nullptr; g.vertexBufferCapacityBytes = 0; }
	for (TextureEntry &t : textures)
	{
		if (t.bindGroup) { t.bindGroup.release(); }
		if (t.view) { t.view.release(); }
		if (t.texture) { t.texture.release(); }
	}
	textures.clear();
	g.scaledTargetId = 0;
	g.scaledTargetDrawn = false;
	for (PipelineEntry &entry : g.quadPipelines)
	{
		if (entry.pipeline) { entry.pipeline.release(); }
	}
	g.quadPipelines.clear();
	if (g.quadShaderModule) { g.quadShaderModule.release(); g.quadShaderModule = nullptr; }
	if (g.pipelineLayout) { g.pipelineLayout.release(); g.pipelineLayout = nullptr; }
	if (g.cameraBindGroupLayout) { g.cameraBindGroupLayout.release(); g.cameraBindGroupLayout = nullptr; }
	if (g.textureBindGroupLayout) { g.textureBindGroupLayout.release(); g.textureBindGroupLayout = nullptr; }
	if (g.samplerPixelated) { g.samplerPixelated.release(); g.samplerPixelated = nullptr; }
	if (g.samplerLinear) { g.samplerLinear.release(); g.samplerLinear = nullptr; }
	if (capture.buffer) { capture.buffer.release(); capture.buffer = nullptr; capture.bufferBytes = 0; }
	forgetSurface();
	if (g.queue) { g.queue.release(); g.queue = nullptr; }
	if (g.device) { g.device.release(); g.device = nullptr; }
	if (g.adapter) { g.adapter.release(); g.adapter = nullptr; }
	if (g.instance) { g.instance.release(); g.instance = nullptr; }
}

// -------------------------------------------------------------------------
// Accessors for the rest of the render layer (render/wgpuFrame.h). The
// context stays private to this file; these hand out exactly what another
// render translation unit needs to record into the frame.
// -------------------------------------------------------------------------

Device wgpuDevice() { return g.device; }
Queue wgpuQueue() { return g.queue; }
TextureFormat wgpuSurfaceFormat() { return g.surfaceFormat; }
BindGroupLayout wgpuTextureBindGroupLayout() { return g.textureBindGroupLayout; }

BindGroup wgpuTextureBindGroup(uint32_t textureId)
{
	if (textureId == 0 || textureId > textures.size()) { return nullptr; }
	return textures[textureId - 1].bindGroup;
}

void wgpuSurfaceSize(int &width, int &height)
{
	width = g.surfaceWidth;
	height = g.surfaceHeight;
}

ShaderModule wgpuCreateShaderModuleFromFile(const char *path)
{
	return createShaderModuleFromFile(path);
}

RenderPassEncoder wgpuCurrentRenderPass()
{
	// The surface, always: the UI is drawn at native resolution on top of
	// the composited world, never inside a render target.
	if (!ensurePassBegun(0)) { return nullptr; }
	return g.framePass;
}

// The two halves of the ErrorScope guard declared in wgpuFrame.h. They live
// here because they need the device and the instance; the guard lives in the
// header so wgpuImgui.cpp gets the same one.
void wgpuBeginErrorScope(const char *what)
{
	if (!g.device) { return; }
	errorScopeNames.push_back(what ? what : "(unnamed)");
	g.device.pushErrorScope(ErrorFilter::Validation);
}

bool wgpuEndErrorScope()
{
	if (!g.device || errorScopeNames.empty()) { return false; }

	ErrorScopeResult result;
	result.what = errorScopeNames.back();
	errorScopeNames.pop_back();

	PopErrorScopeCallbackInfo info = Default;
	info.mode = CallbackMode::AllowProcessEvents;
	info.callback = onPopErrorScope;
	info.userdata1 = &result;
	info.userdata2 = nullptr;
	g.device.popErrorScope(info);

	// The same bounded pump the adapter and device requests use in wgpuInit.
	// Bounded because a scope that never resolves must not hang the game.
	//
	// This blocks, and the time is counted: a scope drained mid-frame is one
	// of the few things that can make a single frame cost tens of
	// milliseconds, so a slow frame should be able to say how much of itself
	// went here.
	const auto blockStart = std::chrono::steady_clock::now();
	for (int i = 0; !result.done && i < 1000; i++)
	{
		g.instance.processEvents();
		if (!result.done)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	framePerf.blockedMs += std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - blockStart).count();
	if (!result.done)
	{
		std::cerr << "WebGPU: error scope for " << result.what << " never completed\n";
	}
	return result.failed;
}

} // namespace render

// -------------------------------------------------------------------------
// wgpu2d: gl2d's API on top of the context above
// -------------------------------------------------------------------------
namespace wgpu2d
{
	using namespace render;

	FrameStats frameStats() { return render::statsLastFrame; }

	void init()
	{
		// The context is created by render::wgpuInit in the platform layer.
	}

	// ---- Texture ----
	glm::ivec2 Texture::GetSize()
	{
		if (id == 0 || id > textures.size()) { return {0, 0}; }
		return { textures[id - 1].width, textures[id - 1].height };
	}

	void Texture::createFromBuffer(const char *image_data, const int width, const int height, bool pixelated, bool useMipMaps)
	{
		*this = createTextureFromPixels((const unsigned char *)image_data, width, height, "buffer", pixelated, useMipMaps);
	}

	void Texture::create1PxSquare(const char *b)
	{
		if (b == nullptr)
		{
			const unsigned char buff[] = { 0xff, 0xff, 0xff, 0xff };
			*this = createTextureFromPixels(buff, 1, 1, "1px white", false, false);
		}
		else
		{
			*this = createTextureFromPixels((const unsigned char *)b, 1, 1, "1px", false, false);
		}
	}

	// Rows come back from stb_image top-first and are uploaded as-is: no
	// flip (see the milestone 4 orientation decision).
	void Texture::loadFromFile(const char *fileName, bool pixelated, bool useMipMaps)
	{
		int width = 0, height = 0, channels = 0;
		stbi_set_flip_vertically_on_load(0);
		unsigned char *pixels = stbi_load(fileName, &width, &height, &channels, 4);
		if (!pixels)
		{
			std::cerr << "wgpu2d: error openning: " << fileName << " (" << stbi_failure_reason() << ")\n";
			return;
		}
		*this = createTextureFromPixels(pixels, width, height, fileName, pixelated, useMipMaps);
		stbi_image_free(pixels);
	}

	void Texture::loadFromFileWithPixelPadding(const char *fileName, int blockSize, bool pixelated, bool useMipMaps)
	{
		std::vector<unsigned char> fileData;
		if (!readBinaryFile(fileName, fileData))
		{
			std::cerr << "wgpu2d: error openning: " << fileName << "\n";
			return;
		}
		*this = createPaddedTextureFromFileData(fileData.data(), fileData.size(), blockSize, pixelated, useMipMaps, fileName);
	}

	void Texture::cleanup()
	{
		if (id == 0 || id > textures.size()) { id = 0; return; }
		TextureEntry &t = textures[id - 1];
		if (t.bindGroup) { t.bindGroup.release(); t.bindGroup = nullptr; }
		if (t.view) { t.view.release(); t.view = nullptr; }
		if (t.texture) { t.texture.release(); t.texture = nullptr; }
		id = 0;
	}

	void Renderer2D::flushFBO(FrameBuffer frameBuffer, bool clearDrawData)
	{
		if (frameBuffer.fbo == 0)
		{
			std::cerr << "wgpu2d: Framebuffer not initialized\n";
			if (clearDrawData) { clearBatch(); }
			return;
		}
		flushBatch(frameBuffer.fbo);
		if (clearDrawData) { clearBatch(); }
	}

	// ---- FrameBuffer ----
	void FrameBuffer::create(unsigned int w, unsigned int h, bool pixelated)
	{
		cleanup();
		texture = createRenderTarget((int)w, (int)h, pixelated, "framebuffer");
		fbo = texture.id; // no framebuffer object exists; the texture is the target
	}

	void FrameBuffer::resize(unsigned int w, unsigned int h)
	{
		if (fbo == 0) { create(w, h); return; }
		if (fbo > textures.size()) { return; }
		if (textures[fbo - 1].width == (int)w && textures[fbo - 1].height == (int)h) { return; }

		// A texture cannot be resized: the slot is refilled with a new one so
		// the handles the caller is holding stay valid.
		texture = createRenderTarget((int)w, (int)h, textures[fbo - 1].pixelated, "framebuffer", fbo);
		fbo = texture.id;
	}

	void FrameBuffer::cleanup()
	{
		texture.cleanup();
		fbo = 0;
	}

	void FrameBuffer::clear()
	{
		if (fbo == 0 || fbo > textures.size()) { return; }
		textures[fbo - 1].needsClear = true;
		// gl2d clears immediately; do the same when there is a frame to
		// record into, otherwise the flag makes the next pass clear.
		if (g.frameOpen) { ensurePassBegun(fbo); }
	}

	// ---- Atlas math, verbatim from gl2d ----
	glm::vec4 computeTextureAtlas(int xCount, int yCount, int x, int y, bool flip)
	{
		float xSize = 1.f / xCount;
		float ySize = 1.f / yCount;

		if (flip)
		{
			return { (x + 1) * xSize, 1 - (y * ySize), (x)*xSize, 1.f - ((y + 1) * ySize) };
		}
		else
		{
			return { x * xSize, 1 - (y * ySize), (x + 1) * xSize, 1.f - ((y + 1) * ySize) };
		}
	}

	glm::vec4 computeTextureAtlasWithPadding(int mapXsize, int mapYsize,
		int xCount, int yCount, int x, int y, bool flip)
	{
		float xSize = 1.f / xCount;
		float ySize = 1.f / yCount;

		float Xpadding = 1.f / mapXsize;
		float Ypadding = 1.f / mapYsize;

		glm::vec4 noFlip = { x * xSize + Xpadding, 1 - (y * ySize) - Ypadding, (x + 1) * xSize - Xpadding, 1.f - ((y + 1) * ySize) + Ypadding };

		if (flip)
		{
			glm::vec4 flip = { noFlip.z, noFlip.y, noFlip.x, noFlip.w };

			return flip;
		}
		else
		{
			return noFlip;
		}
	}

	// ---- Renderer2D ----
	void Renderer2D::create(unsigned int fbo, size_t quadCount)
	{
		batchVertices.reserve(quadCount * 6);
		batchQuadTextures.reserve(quadCount);
		batchQuadCameras.reserve(quadCount);
		currentCamera = Camera{};
		defaultFBO = fbo; // gl2d's defaultFBO: 0 is the screen
	}

	void Renderer2D::cleanup()
	{
		clearDrawData();
	}

	void Renderer2D::pushCamera(Camera c)
	{
		cameraPushPop.push_back(currentCamera);
		currentCamera = c;
	}

	void Renderer2D::popCamera()
	{
		if (cameraPushPop.empty())
		{
			std::cerr << "wgpu2d: Pop on an empty stack on popCamera\n";
		}
		else
		{
			currentCamera = cameraPushPop.back();
			cameraPushPop.pop_back();
		}
	}

	// Verbatim from gl2d.
	glm::vec4 Renderer2D::getViewRect()
	{
		auto rect = glm::vec4{0, 0, windowW, windowH};

		glm::mat3 mat =
		{1.f, 0, currentCamera.position.x ,
		 0, 1.f, currentCamera.position.y,
		 0, 0, 1.f};
		mat = glm::transpose(mat);

		glm::vec3 pos1 = {rect.x, rect.y, 1.f};
		glm::vec3 pos2 = {rect.z + rect.x, rect.w + rect.y, 1.f};

		pos1 = mat * pos1;
		pos2 = mat * pos2;

		glm::vec2 point((pos1.x + pos2.x) / 2.f, (pos1.y + pos2.y) / 2.f);

		auto scaleAroundPoint = [](glm::vec2 vec, glm::vec2 point, float scale) { return (vec - point) * scale + point; };
		pos1 = glm::vec3(scaleAroundPoint(pos1, point, 1.f/currentCamera.zoom), 1.f);
		pos2 = glm::vec3(scaleAroundPoint(pos2, point, 1.f/currentCamera.zoom), 1.f);

		rect = {pos1.x, pos1.y, pos2.x - pos1.x, pos2.y - pos1.y};

		return rect;
	}

	void Renderer2D::clearDrawData()
	{
		clearBatch();
	}

	// gl2d's renderRectangle: the rotation origin is relative to the
	// rectangle's center.
	void Renderer2D::renderRectangle(const Rect transforms, const Texture texture, const Color4f colors[4],
		const glm::vec2 origin, const float rotationDegrees, const glm::vec4 textureCoords)
	{
		glm::vec2 newOrigin;
		newOrigin.x = origin.x + transforms.x + (transforms.z / 2);
		newOrigin.y = origin.y + transforms.y + (transforms.w / 2);
		renderRectangleAbsRotation(transforms, texture, colors, newOrigin, rotationDegrees, textureCoords);
	}

	void Renderer2D::renderRectangleAbsRotation(const Rect transforms, const Texture texture, const Color4f colors[4],
		const glm::vec2 origin, const float rotationDegrees, const glm::vec4 textureCoords)
	{
		pushQuad(currentCamera, currentBlendMode, transforms, texture, colors, origin, rotationDegrees, textureCoords);
	}

	void Renderer2D::renderRectangle(const Rect transforms, const Color4f colors[4], const glm::vec2 origin, const float rotationDegrees)
	{
		renderRectangle(transforms, white1pxSquareTexture, colors, origin, rotationDegrees);
	}

	// gl2d's renderLine, both forms, verbatim.
	void Renderer2D::renderLine(const glm::vec2 position, const float angleDegrees, const float length, const Color4f color, const float width)
	{
		renderRectangle({position - glm::vec2(0,width / 2.f), length, width},
			color, {-length/2, 0}, angleDegrees);
	}

	void Renderer2D::renderLine(const glm::vec2 start, const glm::vec2 end, const Color4f color, const float width)
	{
		glm::vec2 vector = end - start;
		float length = glm::length(vector);
		float angle = std::atan2(vector.y, vector.x);
		renderLine(start, -glm::degrees(angle), length, color, width);
	}

	// gl2d's renderCircleOutline, verbatim: a polygon of lines.
	void Renderer2D::renderCircleOutline(const glm::vec2 position, const Color4f color, const float size, const float width, const unsigned int segments)
	{
		auto calcPos = [&](int p)
		{
			glm::vec2 circle = {size,0};

			float a = 3.1415926 * 2 * ((float)p / segments);

			float c = std::cos(a);
			float s = std::sin(a);

			circle = {c * circle.x - s * circle.y, s * circle.x + c * circle.y};

			return circle + position;
		};

		glm::vec2 lastPos = calcPos(1);
		renderLine(calcPos(0), lastPos, color, width);
		for (int i = 1; i < segments; i++)
		{
			glm::vec2 pos1 = lastPos;
			glm::vec2 pos2 = calcPos(i + 1);

			renderLine(pos1, pos2, color, width);

			lastPos = pos2;
		}
	}

	void Renderer2D::clearScreen(const Color4f color)
	{
		g.clearColor = color;
	}

	void Renderer2D::flush(bool clearDrawData)
	{
		if (windowW <= 0 || windowH <= 0)
		{
			if (windowW < 0 || windowH < 0)
			{
				std::cerr << "wgpu2d: Negative windowW or windowH, have you forgotten to call updateWindowMetrics(w, h)?\n";
			}
			if (clearDrawData) { clearBatch(); }
			return;
		}
		flushBatch(resolveTarget(defaultFBO));
		if (clearDrawData) { clearBatch(); }
	}
}
