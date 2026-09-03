#if RENDERER_WEBGPU

#include <render/wgpuContext.h>

#include <webgpu/webgpu.hpp> // WebGPU-Cpp wrapper over webgpu.h (+ wgpu.h extensions)
#include <glfw3webgpu.h>     // glfwCreateWindowWGPUSurface

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

// File scope only, never in a header: wgpu:: names would otherwise leak into
// every includer. Inside this file, Device, Texture, Default, StringView, and
// the scoped enums all come from the wrapper.
using namespace wgpu;

namespace render
{

namespace
{
	struct Context
	{
		GLFWwindow *window = nullptr;

		// 1a
		Instance instance = nullptr;
		Surface surface = nullptr;
		Adapter adapter = nullptr;

		// 1b
		Device device = nullptr;
		Queue queue = nullptr;
		TextureFormat surfaceFormat = TextureFormat::Undefined;
		int surfaceWidth = 0;
		int surfaceHeight = 0;
		bool surfaceConfigured = false;
		bool firstFramePresented = false;

		// 2: created once at init, selected every frame
		RenderPipeline trianglePipeline = nullptr;
	};

	Context g;

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

	const char *featureName(WGPUFeatureName f)
	{
		switch (f)
		{
			case FeatureName::DepthClipControl: return "DepthClipControl";
			case FeatureName::Depth32FloatStencil8: return "Depth32FloatStencil8";
			case FeatureName::TimestampQuery: return "TimestampQuery";
			case FeatureName::TextureCompressionBC: return "TextureCompressionBC";
			case FeatureName::TextureCompressionBCSliced3D: return "TextureCompressionBCSliced3D";
			case FeatureName::TextureCompressionETC2: return "TextureCompressionETC2";
			case FeatureName::TextureCompressionASTC: return "TextureCompressionASTC";
			case FeatureName::TextureCompressionASTCSliced3D: return "TextureCompressionASTCSliced3D";
			case FeatureName::IndirectFirstInstance: return "IndirectFirstInstance";
			case FeatureName::ShaderF16: return "ShaderF16";
			case FeatureName::RG11B10UfloatRenderable: return "RG11B10UfloatRenderable";
			case FeatureName::BGRA8UnormStorage: return "BGRA8UnormStorage";
			case FeatureName::Float32Filterable: return "Float32Filterable";
			case FeatureName::Float32Blendable: return "Float32Blendable";
			case FeatureName::ClipDistances: return "ClipDistances";
			case FeatureName::DualSourceBlending: return "DualSourceBlending";
			default: return "native extension"; // wgpu-native adds its own ids above the standard ones
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

	// wgpu-native's own diagnostics (wgpu.h extension, C API: the wrapper does
	// not cover it). Warn and above is enough for now.
	void logCallback(WGPULogLevel level, WGPUStringView message, void *)
	{
		const char *tag = level == WGPULogLevel_Error ? "error" : level == WGPULogLevel_Warn ? "warn" : "info";
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
	// debugging channel from here on: a bad descriptor field, a wrong usage
	// flag, a mismatched format all show up here as text.
	void onUncapturedError(WGPUDevice const *, WGPUErrorType type,
		WGPUStringView message, void *, void *)
	{
		const char *kind = type == ErrorType::Validation ? "validation"
			: type == ErrorType::OutOfMemory ? "out of memory"
			: type == ErrorType::Internal ? "internal" : "unknown";
		std::cerr << "WebGPU " << kind << " error: " << StringView(message) << "\n";
	}

	// Configures (or reconfigures) the surface at the window's current
	// framebuffer size. Framebuffer size, not window size: on a Retina
	// display they differ by the scale factor.
	void configureSurface()
	{
		int w = 0, h = 0;
		glfwGetFramebufferSize(g.window, &w, &h);
		if (w <= 0 || h <= 0)
		{
			return; // minimized; keep the old configuration
		}

		SurfaceConfiguration config = Default;
		config.device = g.device;
		config.format = g.surfaceFormat;
		config.usage = TextureUsage::RenderAttachment;
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
	}

	bool readTextFile(const char *path, std::string &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) { return false; }
		std::stringstream ss;
		ss << file.rdbuf();
		out = ss.str();
		return true;
	}

	// Compiles a WGSL file into a shader module. Compile errors arrive through
	// the uncaptured-error callback with line and column numbers.
	ShaderModule createShaderModuleFromFile(const char *path)
	{
		std::string source;
		if (!readTextFile(path, source))
		{
			std::cerr << "WebGPU: cannot read shader file " << path << "\n";
			return nullptr;
		}

		// The WGSL source is a chained struct hanging off the module descriptor.
		// Default sets chain.sType = ShaderSourceWGSL and chain.next = nullptr.
		ShaderSourceWGSL wgsl = Default;
		wgsl.code = StringView(source);

		ShaderModuleDescriptor desc = Default;
		desc.label = StringView(path);
		desc.nextInChain = &wgsl.chain;

		return g.device.createShaderModule(desc);
	}

	// The render pipeline: every configurable stage of the GPU's fixed
	// triangle pipeline, baked into one immutable object. Selected per pass
	// with setPipeline; never mutated.
	bool createTrianglePipeline()
	{
		ShaderModule module = createShaderModuleFromFile(RESOURCES_PATH "shaders/triangle.wgsl");
		if (!module) { return false; }

		RenderPipelineDescriptor desc = Default;
		desc.label = StringView("triangle pipeline");

		// Vertex stage: no vertex buffers yet, positions come from the shader.
		desc.vertex.module = module;
		desc.vertex.entryPoint = StringView("vs_main");
		desc.vertex.constantCount = 0;
		desc.vertex.constants = nullptr;
		desc.vertex.bufferCount = 0;
		desc.vertex.buffers = nullptr;

		// Primitive assembly. Default already gives TriangleList, CCW front
		// face, no culling; set explicitly so the choice is visible.
		desc.primitive.topology = PrimitiveTopology::TriangleList;
		desc.primitive.stripIndexFormat = IndexFormat::Undefined;
		desc.primitive.frontFace = FrontFace::CCW;
		desc.primitive.cullMode = CullMode::None; // gl2d never culled; quads may be flipped by negative sizes
		desc.primitive.unclippedDepth = false;

		// Blending, matching gl2d's enableNecessaryGLFeatures():
		//   glBlendEquation(GL_FUNC_ADD)
		//   glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
		BlendState blend = Default;
		blend.color.operation = BlendOperation::Add;
		blend.color.srcFactor = BlendFactor::SrcAlpha;
		blend.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
		blend.alpha.operation = BlendOperation::Add;
		blend.alpha.srcFactor = BlendFactor::One;
		blend.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;

		// The one color target: must match the surface's format exactly.
		ColorTargetState colorTarget = Default;
		colorTarget.format = g.surfaceFormat;
		colorTarget.blend = &blend;
		colorTarget.writeMask = ColorWriteMask::All;

		// Fragment stage: one output, to color attachment 0.
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

		// No bind groups yet, so no layout: the pipeline infers an empty one.
		// Milestone 4 replaces this with an explicit layout.
		desc.layout = nullptr;

		g.trianglePipeline = g.device.createRenderPipeline(desc);

		// The pipeline holds what it needs from the module; the module itself
		// can go now.
		module.release();

		if (!g.trianglePipeline)
		{
			std::cerr << "WebGPU: createRenderPipeline returned null\n";
			return false;
		}
		return true;
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
		// decides the stride of the per-camera uniform slots in milestone 6b.
		Limits limits = Default;
		if (adapter.getLimits(&limits) == Status::Success)
		{
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
		std::cout << "  features (" << features.featureCount << "):";
		for (size_t i = 0; i < features.featureCount; i++)
		{
			std::cout << " " << featureName(features.features[i]);
		}
		std::cout << "\n";
		features.freeMembers();

		// Flush so the report survives even if the process is killed while the
		// window is open (stdout is fully buffered when redirected to a file).
		std::cout.flush();
	}
}

bool wgpuInit(GLFWwindow *window)
{
	g.window = window;
	wgpuSetLogCallback(logCallback, nullptr);
	wgpuSetLogLevel(WGPULogLevel_Warn);

	// 1. Instance: the library itself. No GPU involved yet.
	InstanceDescriptor instanceDesc = Default;
	g.instance = createInstance(instanceDesc);
	if (!g.instance)
	{
		std::cerr << "WebGPU: createInstance returned null\n";
		return false;
	}

	// 2. Surface: the window's Metal layer, wrapped so WebGPU can present to it.
	//    Created now so the adapter request can ask for a GPU that can drive it.
	g.surface = glfwCreateWindowWGPUSurface(g.instance, window);
	if (!g.surface)
	{
		std::cerr << "WebGPU: glfwCreateWindowWGPUSurface returned null\n";
		return false;
	}

	// 3. Adapter: a description of one physical GPU that fits the options.
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

	// 4. Device: the working connection to the GPU. Every later object is
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

	// 5. Queue: the device's single inbox for command buffers and uploads.
	g.queue = g.device.getQueue();
	if (!g.queue)
	{
		std::cerr << "WebGPU: device.getQueue returned null\n";
		return false;
	}

	// 6. Surface format. The first listed format is the surface's preferred
	//    one and on Metal it is normally an sRGB variant. gl2d never gamma
	//    corrected, so prefer the plain (non-sRGB) 8-bit format when offered
	//    and only fall back to the preferred one otherwise. See the plan's
	//    "surface format" risk.
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

	// 7. Configure the surface: this is the OpenGL default framebuffer plus
	//    swap interval, expressed as an explicit object.
	configureSurface();
	if (!g.surfaceConfigured)
	{
		std::cerr << "WebGPU: surface not configured (framebuffer size "
			<< g.surfaceWidth << "x" << g.surfaceHeight << ")\n";
		return false;
	}
	std::cout << "WebGPU surface configured at " << g.surfaceWidth << "x" << g.surfaceHeight << "\n";

	// 8. The triangle pipeline (milestone 2). Depends on the surface format.
	if (!createTrianglePipeline())
	{
		return false;
	}
	std::cout << "WebGPU triangle pipeline created\n";
	std::cout.flush();
	return true;
}

void wgpuRenderFrame()
{
	if (!g.surfaceConfigured)
	{
		configureSurface();
		if (!g.surfaceConfigured) { return; }
	}

	// 1. Acquire: the texture that will next go on screen.
	SurfaceTexture surfaceTexture = Default;
	g.surface.getCurrentTexture(&surfaceTexture);
	Texture texture = surfaceTexture.texture;

	switch (surfaceTexture.status)
	{
		case SurfaceGetCurrentTextureStatus::SuccessOptimal:
		case SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
			break;

		case SurfaceGetCurrentTextureStatus::Timeout:
		case SurfaceGetCurrentTextureStatus::Outdated:
		case SurfaceGetCurrentTextureStatus::Lost:
			// The window was resized or the surface otherwise invalidated.
			// Reconfigure at the current size and try again next frame.
			if (texture) { texture.release(); }
			configureSurface();
			return;

		default:
			std::cerr << "WebGPU: surface.getCurrentTexture failed (status "
				<< surfaceTexture.status << ")\n";
			if (texture) { texture.release(); }
			return;
	}

	// 2. View: render passes attach to a view of a texture, never the texture.
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
	TextureView view = texture.createView(viewDesc);

	// 3. Record: a command encoder collects GPU work; nothing runs yet.
	CommandEncoderDescriptor encoderDesc = Default;
	encoderDesc.label = StringView("frame encoder");
	CommandEncoder encoder = g.device.createCommandEncoder(encoderDesc);

	// The render pass: one color attachment, cleared on load, kept on store.
	// This is gl2d's glClear, expressed as the pass's load operation.
	RenderPassColorAttachment colorAttachment = Default;
	colorAttachment.view = view;
	colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED; // required for a 2D target
	colorAttachment.resolveTarget = nullptr;
	colorAttachment.loadOp = LoadOp::Clear;
	colorAttachment.storeOp = StoreOp::Store;
	colorAttachment.clearValue.r = 0.25; // deliberately not black
	colorAttachment.clearValue.g = 0.45;
	colorAttachment.clearValue.b = 0.75;
	colorAttachment.clearValue.a = 1.0;

	RenderPassDescriptor passDesc = Default;
	passDesc.label = StringView("clear pass");
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colorAttachment;
	passDesc.depthStencilAttachment = nullptr;
	passDesc.occlusionQuerySet = nullptr;
	passDesc.timestampWrites = nullptr;

	RenderPassEncoder pass = encoder.beginRenderPass(passDesc);
	pass.setPipeline(g.trianglePipeline);
	pass.draw(3, 1, 0, 0); // 3 vertices, 1 instance, starting at vertex 0, instance 0
	pass.end();
	pass.release();

	// 4. Seal the recording into a command buffer and submit it.
	CommandBufferDescriptor cmdDesc = Default;
	cmdDesc.label = StringView("frame commands");
	CommandBuffer commands = encoder.finish(cmdDesc);
	encoder.release();

	g.queue.submit(1, &commands);
	commands.release();

	// 5. Present: hand the texture to the compositor. This is glfwSwapBuffers.
	g.surface.present();

	view.release();
	texture.release();

	// Let pending callbacks (errors, device lost) run once per frame.
	g.instance.processEvents();

	if (!g.firstFramePresented)
	{
		g.firstFramePresented = true;
		std::cout << "WebGPU first frame presented\n";
		std::cout.flush();
	}
}

void wgpuShutdown()
{
	if (g.trianglePipeline) { g.trianglePipeline.release(); g.trianglePipeline = nullptr; }
	if (g.surface && g.surfaceConfigured) { g.surface.unconfigure(); g.surfaceConfigured = false; }
	if (g.queue) { g.queue.release(); g.queue = nullptr; }
	if (g.device) { g.device.release(); g.device = nullptr; }
	if (g.adapter) { g.adapter.release(); g.adapter = nullptr; }
	if (g.surface) { g.surface.release(); g.surface = nullptr; }
	if (g.instance) { g.instance.release(); g.instance = nullptr; }
}

}

#endif // RENDERER_WEBGPU
