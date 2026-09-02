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
	// Everything 1a creates. Grows into device/queue/surface config in 1b.
	struct Context
	{
		WGPUInstance instance = nullptr;
		WGPUSurface surface = nullptr;
		WGPUAdapter adapter = nullptr;
	};

	Context g;

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
	return true;
}

void wgpuShutdown()
{
	if (g.adapter) { wgpuAdapterRelease(g.adapter); g.adapter = nullptr; }
	if (g.surface) { wgpuSurfaceRelease(g.surface); g.surface = nullptr; }
	if (g.instance) { wgpuInstanceRelease(g.instance); g.instance = nullptr; }
}

}

#endif // RENDERER_WEBGPU
