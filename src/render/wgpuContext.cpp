#if RENDERER_WEBGPU

#include <render/wgpuContext.h>

#include <webgpu/webgpu.h>   // the standard WebGPU C API (webgpu-headers)
#include <webgpu/wgpu.h>     // wgpu-native extensions: log callback
#include <glfw3webgpu.h>     // glfwCreateWindowWGPUSurface

#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

namespace render
{

namespace
{
	struct Context
	{
		GLFWwindow *window = nullptr;

		// 1a
		WGPUInstance instance = nullptr;
		WGPUSurface surface = nullptr;
		WGPUAdapter adapter = nullptr;

		// 1b
		WGPUDevice device = nullptr;
		WGPUQueue queue = nullptr;
		WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
		int surfaceWidth = 0;
		int surfaceHeight = 0;
		bool surfaceConfigured = false;
		bool firstFramePresented = false;
	};

	Context g;

	// Labels and other input strings are passed as string views. WGPU_STRLEN
	// as the length means "null-terminated, measure it".
	WGPUStringView str(const char *s)
	{
		return WGPUStringView{s, WGPU_STRLEN};
	}

	// wgpu-native hands strings back as pointer + length, not null-terminated.
	// A length of WGPU_STRLEN means "null-terminated, measure it yourself".
	std::string_view toStringView(WGPUStringView s)
	{
		if (s.data == nullptr) { return {}; }
		if (s.length == WGPU_STRLEN) { return std::string_view(s.data); }
		return std::string_view(s.data, s.length);
	}

	const char *backendName(WGPUBackendType t)
	{
		switch (t)
		{
			case WGPUBackendType_Null: return "Null";
			case WGPUBackendType_WebGPU: return "WebGPU";
			case WGPUBackendType_D3D11: return "D3D11";
			case WGPUBackendType_D3D12: return "D3D12";
			case WGPUBackendType_Metal: return "Metal";
			case WGPUBackendType_Vulkan: return "Vulkan";
			case WGPUBackendType_OpenGL: return "OpenGL";
			case WGPUBackendType_OpenGLES: return "OpenGLES";
			default: return "Undefined";
		}
	}

	const char *adapterTypeName(WGPUAdapterType t)
	{
		switch (t)
		{
			case WGPUAdapterType_DiscreteGPU: return "discrete GPU";
			case WGPUAdapterType_IntegratedGPU: return "integrated GPU";
			case WGPUAdapterType_CPU: return "CPU";
			default: return "unknown";
		}
	}

	const char *featureName(WGPUFeatureName f)
	{
		switch (f)
		{
			case WGPUFeatureName_DepthClipControl: return "DepthClipControl";
			case WGPUFeatureName_Depth32FloatStencil8: return "Depth32FloatStencil8";
			case WGPUFeatureName_TimestampQuery: return "TimestampQuery";
			case WGPUFeatureName_TextureCompressionBC: return "TextureCompressionBC";
			case WGPUFeatureName_TextureCompressionBCSliced3D: return "TextureCompressionBCSliced3D";
			case WGPUFeatureName_TextureCompressionETC2: return "TextureCompressionETC2";
			case WGPUFeatureName_TextureCompressionASTC: return "TextureCompressionASTC";
			case WGPUFeatureName_TextureCompressionASTCSliced3D: return "TextureCompressionASTCSliced3D";
			case WGPUFeatureName_IndirectFirstInstance: return "IndirectFirstInstance";
			case WGPUFeatureName_ShaderF16: return "ShaderF16";
			case WGPUFeatureName_RG11B10UfloatRenderable: return "RG11B10UfloatRenderable";
			case WGPUFeatureName_BGRA8UnormStorage: return "BGRA8UnormStorage";
			case WGPUFeatureName_Float32Filterable: return "Float32Filterable";
			case WGPUFeatureName_Float32Blendable: return "Float32Blendable";
			case WGPUFeatureName_ClipDistances: return "ClipDistances";
			case WGPUFeatureName_DualSourceBlending: return "DualSourceBlending";
			default: return "native extension"; // wgpu-native adds its own ids above the standard ones
		}
	}

	// wgpu-native's own diagnostics. Warn and above is enough for now.
	void logCallback(WGPULogLevel level, WGPUStringView message, void *)
	{
		const char *tag = level == WGPULogLevel_Error ? "error" : level == WGPULogLevel_Warn ? "warn" : "info";
		std::cerr << "[wgpu " << tag << "] " << toStringView(message) << "\n";
	}

	// Filled in by the request-adapter callback. Unsynchronized: the callback
	// only runs on this thread, inside wgpuInstanceProcessEvents (see mode below).
	struct AdapterRequest
	{
		WGPUAdapter adapter = nullptr;
		bool done = false;
	};

	void onAdapterRequest(WGPURequestAdapterStatus status, WGPUAdapter adapter,
		WGPUStringView message, void *userdata1, void *)
	{
		auto *req = static_cast<AdapterRequest *>(userdata1);
		if (status == WGPURequestAdapterStatus_Success)
		{
			req->adapter = adapter;
		}
		else
		{
			std::cerr << "WebGPU: requestAdapter failed (status " << status << "): "
				<< toStringView(message) << "\n";
		}
		req->done = true;
	}

	const char *formatName(WGPUTextureFormat f)
	{
		switch (f)
		{
			case WGPUTextureFormat_BGRA8Unorm: return "BGRA8Unorm";
			case WGPUTextureFormat_BGRA8UnormSrgb: return "BGRA8UnormSrgb";
			case WGPUTextureFormat_RGBA8Unorm: return "RGBA8Unorm";
			case WGPUTextureFormat_RGBA8UnormSrgb: return "RGBA8UnormSrgb";
			default: return "other";
		}
	}

	// Filled in by the request-device callback. Unsynchronized for the same
	// reason as AdapterRequest: it only runs inside wgpuInstanceProcessEvents.
	struct DeviceRequest
	{
		WGPUDevice device = nullptr;
		bool done = false;
	};

	void onDeviceRequest(WGPURequestDeviceStatus status, WGPUDevice device,
		WGPUStringView message, void *userdata1, void *)
	{
		auto *req = static_cast<DeviceRequest *>(userdata1);
		if (status == WGPURequestDeviceStatus_Success)
		{
			req->device = device;
		}
		else
		{
			std::cerr << "WebGPU: requestDevice failed (status " << status << "): "
				<< toStringView(message) << "\n";
		}
		req->done = true;
	}

	// Fires if the GPU connection dies (driver reset, device destroyed).
	void onDeviceLost(WGPUDevice const *, WGPUDeviceLostReason reason,
		WGPUStringView message, void *, void *)
	{
		std::cerr << "WebGPU: device lost (reason " << reason << "): "
			<< toStringView(message) << "\n";
	}

	// Fires on every validation error the API catches. This is the main
	// debugging channel from here on: a bad descriptor field, a wrong usage
	// flag, a mismatched format all show up here as text.
	void onUncapturedError(WGPUDevice const *, WGPUErrorType type,
		WGPUStringView message, void *, void *)
	{
		const char *kind = type == WGPUErrorType_Validation ? "validation"
			: type == WGPUErrorType_OutOfMemory ? "out of memory"
			: type == WGPUErrorType_Internal ? "internal" : "unknown";
		std::cerr << "WebGPU " << kind << " error: " << toStringView(message) << "\n";
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

		WGPUSurfaceConfiguration config = {};
		config.nextInChain = nullptr;
		config.device = g.device;
		config.format = g.surfaceFormat;
		config.usage = WGPUTextureUsage_RenderAttachment;
		config.width = (uint32_t)w;
		config.height = (uint32_t)h;
		config.viewFormatCount = 0;
		config.viewFormats = nullptr;
		config.alphaMode = WGPUCompositeAlphaMode_Auto;
		config.presentMode = WGPUPresentMode_Fifo; // vsync, always supported

		wgpuSurfaceConfigure(g.surface, &config);
		g.surfaceWidth = w;
		g.surfaceHeight = h;
		g.surfaceConfigured = true;
	}

	void printAdapter(WGPUAdapter adapter)
	{
		WGPUAdapterInfo info = {};
		if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success)
		{
			std::cout << "WebGPU adapter\n"
				<< "  device:       " << toStringView(info.device) << "\n"
				<< "  vendor:       " << toStringView(info.vendor) << " (0x" << std::hex << info.vendorID << std::dec << ")\n"
				<< "  architecture: " << toStringView(info.architecture) << "\n"
				<< "  description:  " << toStringView(info.description) << "\n"
				<< "  backend:      " << backendName(info.backendType) << "\n"
				<< "  type:         " << adapterTypeName(info.adapterType) << "\n";
			wgpuAdapterInfoFreeMembers(info);
		}
		else
		{
			std::cerr << "WebGPU: wgpuAdapterGetInfo failed\n";
		}

		// The limits that matter for a 2D sprite batcher. minUniformBufferOffsetAlignment
		// decides the stride of the per-camera uniform slots in milestone 6b.
		WGPULimits limits = {};
		if (wgpuAdapterGetLimits(adapter, &limits) == WGPUStatus_Success)
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
			std::cerr << "WebGPU: wgpuAdapterGetLimits failed\n";
		}

		WGPUSupportedFeatures features = {};
		wgpuAdapterGetFeatures(adapter, &features);
		std::cout << "  features (" << features.featureCount << "):";
		for (size_t i = 0; i < features.featureCount; i++)
		{
			std::cout << " " << featureName(features.features[i]);
		}
		std::cout << "\n";
		wgpuSupportedFeaturesFreeMembers(features);

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
	WGPUInstanceDescriptor instanceDesc = {};
	instanceDesc.nextInChain = nullptr;
	g.instance = wgpuCreateInstance(&instanceDesc);
	if (!g.instance)
	{
		std::cerr << "WebGPU: wgpuCreateInstance returned null\n";
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
	//    during ProcessEvents, so the loop below runs at most a few times.
	WGPURequestAdapterOptions options = {};
	options.nextInChain = nullptr;
	options.featureLevel = WGPUFeatureLevel_Core;
	options.powerPreference = WGPUPowerPreference_Undefined;
	options.forceFallbackAdapter = false;
	options.backendType = WGPUBackendType_Undefined;
	options.compatibleSurface = g.surface;

	AdapterRequest request;
	WGPURequestAdapterCallbackInfo callbackInfo = {};
	callbackInfo.nextInChain = nullptr;
	// The mode is a contract about when the implementation may invoke the callback:
	//   WaitAnyOnly        — only from wgpuInstanceWaitAny that includes this future
	//   AllowProcessEvents — from WaitAny or wgpuInstanceProcessEvents (this one)
	//   AllowSpontaneous   — those, plus whenever the impl feels like it, possibly
	//                        on a thread we do not control
	// AllowProcessEvents is why the polling loop below is necessary, and why
	// AdapterRequest needs no mutex or atomic: the callback can only run on this
	// thread, inside wgpuInstanceProcessEvents. Switching to AllowSpontaneous
	// inherits a data race on request.done and request.adapter.
	callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
	callbackInfo.callback = onAdapterRequest;
	callbackInfo.userdata1 = &request;
	callbackInfo.userdata2 = nullptr;

	wgpuInstanceRequestAdapter(g.instance, &options, callbackInfo);

	for (int i = 0; !request.done && i < 1000; i++)
	{
		wgpuInstanceProcessEvents(g.instance);
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
	WGPUDeviceDescriptor deviceDesc = {};
	deviceDesc.nextInChain = nullptr;
	deviceDesc.label = str("space-game device");
	deviceDesc.requiredFeatureCount = 0;
	deviceDesc.requiredFeatures = nullptr;
	deviceDesc.requiredLimits = nullptr;
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.defaultQueue.label = str("space-game queue");
	deviceDesc.deviceLostCallbackInfo.nextInChain = nullptr;
	deviceDesc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
	deviceDesc.deviceLostCallbackInfo.callback = onDeviceLost;
	deviceDesc.deviceLostCallbackInfo.userdata1 = nullptr;
	deviceDesc.deviceLostCallbackInfo.userdata2 = nullptr;
	deviceDesc.uncapturedErrorCallbackInfo.nextInChain = nullptr;
	deviceDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
	deviceDesc.uncapturedErrorCallbackInfo.userdata1 = nullptr;
	deviceDesc.uncapturedErrorCallbackInfo.userdata2 = nullptr;

	DeviceRequest deviceRequest;
	WGPURequestDeviceCallbackInfo deviceCallbackInfo = {};
	deviceCallbackInfo.nextInChain = nullptr;
	deviceCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
	deviceCallbackInfo.callback = onDeviceRequest;
	deviceCallbackInfo.userdata1 = &deviceRequest;
	deviceCallbackInfo.userdata2 = nullptr;

	wgpuAdapterRequestDevice(g.adapter, &deviceDesc, deviceCallbackInfo);

	for (int i = 0; !deviceRequest.done && i < 1000; i++)
	{
		wgpuInstanceProcessEvents(g.instance);
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
	g.queue = wgpuDeviceGetQueue(g.device);
	if (!g.queue)
	{
		std::cerr << "WebGPU: wgpuDeviceGetQueue returned null\n";
		return false;
	}

	// 6. Surface format. The first listed format is the surface's preferred
	//    one and on Metal it is normally an sRGB variant. gl2d never gamma
	//    corrected, so prefer the plain (non-sRGB) 8-bit format when offered
	//    and only fall back to the preferred one otherwise. See the plan's
	//    "surface format" risk.
	WGPUSurfaceCapabilities caps = {};
	if (wgpuSurfaceGetCapabilities(g.surface, g.adapter, &caps) != WGPUStatus_Success || caps.formatCount == 0)
	{
		std::cerr << "WebGPU: wgpuSurfaceGetCapabilities failed or listed no formats\n";
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
		if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm)
		{
			g.surfaceFormat = caps.formats[i];
			break;
		}
	}
	std::cout << "WebGPU surface format chosen: " << formatName(g.surfaceFormat) << "\n";
	wgpuSurfaceCapabilitiesFreeMembers(caps);

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
	WGPUSurfaceTexture surfaceTexture = {};
	wgpuSurfaceGetCurrentTexture(g.surface, &surfaceTexture);

	switch (surfaceTexture.status)
	{
		case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
		case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
			break;

		case WGPUSurfaceGetCurrentTextureStatus_Timeout:
		case WGPUSurfaceGetCurrentTextureStatus_Outdated:
		case WGPUSurfaceGetCurrentTextureStatus_Lost:
			// The window was resized or the surface otherwise invalidated.
			// Reconfigure at the current size and try again next frame.
			if (surfaceTexture.texture) { wgpuTextureRelease(surfaceTexture.texture); }
			configureSurface();
			return;

		default:
			std::cerr << "WebGPU: wgpuSurfaceGetCurrentTexture failed (status "
				<< surfaceTexture.status << ")\n";
			if (surfaceTexture.texture) { wgpuTextureRelease(surfaceTexture.texture); }
			return;
	}

	// 2. View: render passes attach to a view of a texture, never the texture.
	WGPUTextureViewDescriptor viewDesc = {};
	viewDesc.nextInChain = nullptr;
	viewDesc.label = str("surface view");
	viewDesc.format = g.surfaceFormat;
	viewDesc.dimension = WGPUTextureViewDimension_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = WGPUTextureAspect_All;
	viewDesc.usage = WGPUTextureUsage_RenderAttachment;
	WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

	// 3. Record: a command encoder collects GPU work; nothing runs yet.
	WGPUCommandEncoderDescriptor encoderDesc = {};
	encoderDesc.nextInChain = nullptr;
	encoderDesc.label = str("frame encoder");
	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g.device, &encoderDesc);

	// The render pass: one color attachment, cleared on load, kept on store.
	// This is gl2d's glClear, expressed as the pass's load operation.
	WGPURenderPassColorAttachment colorAttachment = {};
	colorAttachment.nextInChain = nullptr;
	colorAttachment.view = view;
	colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED; // required for a 2D target
	colorAttachment.resolveTarget = nullptr;
	colorAttachment.loadOp = WGPULoadOp_Clear;
	colorAttachment.storeOp = WGPUStoreOp_Store;
	colorAttachment.clearValue = WGPUColor{0.25, 0.45, 0.75, 1.0}; // deliberately not black

	WGPURenderPassDescriptor passDesc = {};
	passDesc.nextInChain = nullptr;
	passDesc.label = str("clear pass");
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colorAttachment;
	passDesc.depthStencilAttachment = nullptr;
	passDesc.occlusionQuerySet = nullptr;
	passDesc.timestampWrites = nullptr;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
	// Draw calls go here from milestone 2 on.
	wgpuRenderPassEncoderEnd(pass);
	wgpuRenderPassEncoderRelease(pass);

	// 4. Seal the recording into a command buffer and submit it.
	WGPUCommandBufferDescriptor cmdDesc = {};
	cmdDesc.nextInChain = nullptr;
	cmdDesc.label = str("frame commands");
	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdDesc);
	wgpuCommandEncoderRelease(encoder);

	wgpuQueueSubmit(g.queue, 1, &commands);
	wgpuCommandBufferRelease(commands);

	// 5. Present: hand the texture to the compositor. This is glfwSwapBuffers.
	wgpuSurfacePresent(g.surface);

	wgpuTextureViewRelease(view);
	wgpuTextureRelease(surfaceTexture.texture);

	// Let pending callbacks (errors, device lost) run once per frame.
	wgpuInstanceProcessEvents(g.instance);

	if (!g.firstFramePresented)
	{
		g.firstFramePresented = true;
		std::cout << "WebGPU first frame presented\n";
		std::cout.flush();
	}
}

void wgpuShutdown()
{
	if (g.surface && g.surfaceConfigured) { wgpuSurfaceUnconfigure(g.surface); g.surfaceConfigured = false; }
	if (g.queue) { wgpuQueueRelease(g.queue); g.queue = nullptr; }
	if (g.device) { wgpuDeviceRelease(g.device); g.device = nullptr; }
	if (g.adapter) { wgpuAdapterRelease(g.adapter); g.adapter = nullptr; }
	if (g.surface) { wgpuSurfaceRelease(g.surface); g.surface = nullptr; }
	if (g.instance) { wgpuInstanceRelease(g.instance); g.instance = nullptr; }
}

}

#endif // RENDERER_WEBGPU
