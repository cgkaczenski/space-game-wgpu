#if RENDERER_WEBGPU

#include <render/wgpuContext.h>
#ifdef __APPLE__
#include <render/wgpuMetalLayer.h>
#endif

#include <webgpu/webgpu.hpp> // WebGPU-Cpp wrapper over webgpu.h (+ wgpu.h extensions)
#include <glfw3webgpu.h>     // glfwCreateWindowWGPUSurface
#include <stb_image/stb_image.h>
#include <glm/glm.hpp>       // column-major like WGSL's mat4x4f

#include <chrono>
#include <cmath>
#include <cstddef>   // offsetof
#include <cstdlib>   // rand, for the demo
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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
		RenderPipeline quadPipeline = nullptr;

		// 6a: the vertex buffer is rewritten every frame from the CPU batch
		// and grows (never shrinks) when a frame needs more than it holds.
		Buffer vertexBuffer = nullptr;
		uint64_t vertexBufferCapacityBytes = 0;

		// 4: created once at init, shared by every texture
		Sampler sampler = nullptr;
		BindGroupLayout textureBindGroupLayout = nullptr; // group 0: texture + sampler
		PipelineLayout pipelineLayout = nullptr;

		// 5: the camera uniform, one 64-byte matrix rewritten every frame
		BindGroupLayout cameraBindGroupLayout = nullptr; // group 1: uniform buffer
		Buffer uniformBuffer = nullptr;
		BindGroup cameraBindGroup = nullptr;
		std::chrono::steady_clock::time_point lastFrameTime = {};
		float demoTime = 0.0f;
	};

	// gl2d's camera, fields and follow() copied verbatim so milestone 7 can
	// call it under the same name with the same numbers. Rotation is omitted:
	// the game never sets it.
	struct Camera
	{
		glm::vec2 position = {};
		float zoom = 1.0f;

		void follow(glm::vec2 pos, float speed, float min, float max, float w, float h)
		{
			pos.x -= w / 2.f;
			pos.y -= h / 2.f;

			glm::vec2 delta = pos - position;
			bool signX = delta.x >= 0;
			bool signY = delta.y >= 0;

			float len = glm::length(delta);

			delta = glm::normalize(delta);

			if (len < min * 2)
			{
				speed /= 4.f;
			}
			else if (len < min * 4)
			{
				speed /= 2.f;
			}

			if (len > min)
			{
				if (len > max)
				{
					len = max;
					position = pos - (max * delta);
					//fix jittering
					//position += delta * speed;
				}
				else
				{
					position += delta * speed;
				}

				glm::vec2 delta2 = pos - position;
				bool signX2 = delta.x >= 0;
				bool signY2 = delta.y >= 0;
				if (signX2 != signX || signY2 != signY || glm::length(delta2) > len)
				{
					//fix jittering
					//position = pos;
				}
			}
		}
	};

	Camera demoCamera;

	// What the shader's Camera struct reads: one column-major mat4x4f.
	struct CameraUniforms
	{
		glm::mat4 viewProj;
	};
	static_assert(sizeof(CameraUniforms) == 64, "uniform layout must match the WGSL Camera struct");

	// gl2d's per-corner CPU transform (renderRectangleAbsRotation), collapsed
	// into one matrix:
	//   1. subtract the camera position          (v.x -= cam.x; v.y += cam.y on the flipped y)
	//   2. scale about the screen center by zoom  (scaleAroundPoint with center (w/2, -h/2))
	//   3. pixels to clip space                   (x: 2x/w - 1;  y: 2y/h + 1 on the flipped y)
	// For a world point (x, y) with y down that works out to
	//   ndc.x =  (2 zoom / w) x  - 2 zoom cam.x / w - zoom
	//   ndc.y = -(2 zoom / h) y  + 2 zoom cam.y / h + zoom
	// Checks: the camera's top-left corner maps to (-1, 1) at zoom 1, and
	// the screen center stays fixed under zoom.
	glm::mat4 buildViewProj(const Camera &cam, float w, float h)
	{
		const float z = cam.zoom;
		glm::mat4 m(1.0f);                     // glm is column-major: m[col][row]
		m[0][0] = 2.0f * z / w;
		m[1][1] = -2.0f * z / h;
		m[3][0] = -2.0f * z * cam.position.x / w - z;
		m[3][1] = 2.0f * z * cam.position.y / h + z;
		return m;
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

	// ---- gl2d-shaped public types (milestone 6a) ----
	// Mirrors gl2d's signatures so the game files change only by namespace
	// in milestone 7. The implementation underneath is not gl2d's.
	using Rect = glm::vec4;    // x, y, width, height in world pixels, y down
	using Color4f = glm::vec4;
	const glm::vec4 DefaultTextureCoords = {0, 1, 1, 0}; // gl2d's convention, see renderRectangleAbsRotation

	// A copyable handle, like gl2d's Texture { GLuint id }. 0 means invalid.
	// The GPU objects live in the registry below, indexed by id - 1.
	struct Texture
	{
		uint32_t id = 0;
	};

	struct TextureEntry
	{
		wgpu::Texture texture = nullptr;
		TextureView view = nullptr;
		BindGroup bindGroup = nullptr; // group 0 for this texture + the shared sampler
		int width = 0;
		int height = 0;
	};

	std::vector<TextureEntry> textures;

	// The CPU batch: gl2d's spritePositions / spriteColors / texturePositions
	// / spriteTextures, collapsed into one interleaved vertex vector plus the
	// texture of each quad (6b uses that for draw ranges).
	std::vector<Vertex> batchVertices;
	std::vector<Texture> batchQuadTextures;

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

	// gl2d's renderRectangleAbsRotation up to (not including) its camera
	// steps, which are now the matrix. Corners are built and rotated in
	// gl2d's flipped space, then flipped back into world pixels for the
	// vertex buffer. Texture coordinates arrive in gl2d's convention
	// ({u0, v0, u1, v1} with v measured from the bottom, default {0,1,1,0})
	// and are converted to WebGPU's top-left origin here: v = 1 - v.
	void renderRectangleAbsRotation(const Rect transforms, const Texture texture,
		const Color4f colors[4], const glm::vec2 origin, const float rotation, const glm::vec4 textureCoords)
	{
		if (texture.id == 0 || texture.id > textures.size())
		{
			std::cerr << "render: renderRectangle with an invalid texture (id " << texture.id << ")\n";
			return;
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

		auto push = [&](glm::vec2 p, const Color4f &c, float u, float v)
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

		batchQuadTextures.push_back(texture);
	}

	// gl2d's renderRectangle: the rotation origin is relative to the
	// rectangle's center.
	void renderRectangle(const Rect transforms, const Texture texture, const Color4f colors[4],
		const glm::vec2 origin = {}, const float rotationDegrees = 0.f, const glm::vec4 textureCoords = DefaultTextureCoords)
	{
		glm::vec2 newOrigin;
		newOrigin.x = origin.x + transforms.x + (transforms.z / 2);
		newOrigin.y = origin.y + transforms.y + (transforms.w / 2);
		renderRectangleAbsRotation(transforms, texture, colors, newOrigin, rotationDegrees, textureCoords);
	}

	void renderRectangle(const Rect transforms, const Texture texture, const Color4f colors = {1, 1, 1, 1},
		const glm::vec2 origin = {}, const float rotationDegrees = 0.f, const glm::vec4 textureCoords = DefaultTextureCoords)
	{
		Color4f c[4] = { colors, colors, colors, colors };
		renderRectangle(transforms, texture, c, origin, rotationDegrees, textureCoords);
	}

	// ---- demo content (milestone 6a) ----
	struct DemoSprite
	{
		glm::vec2 position;
		float size;
		float rotation;
		float spin;
		Color4f color;
		int cellX, cellY; // cell in the 5x2 ship sheet
	};
	std::vector<DemoSprite> demoSprites;
	Texture demoSheet;

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
		std::cout << "WebGPU surface configured at " << w << "x" << h << "\n";
		std::cout.flush();
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

	// The sampler: how a shader reads a texture. Separate from the texture
	// (unlike OpenGL), so one serves every sprite. Clamp to edge and nearest
	// filtering match what the game passes to gl2d (pixelated = true). No
	// mipmaps yet; milestone 7 adds them and switches mipmapFilter.
	bool createSampler()
	{
		SamplerDescriptor desc = Default;
		desc.label = StringView("sprite sampler");
		desc.addressModeU = AddressMode::ClampToEdge;
		desc.addressModeV = AddressMode::ClampToEdge;
		desc.addressModeW = AddressMode::ClampToEdge;
		desc.magFilter = FilterMode::Nearest;
		desc.minFilter = FilterMode::Nearest;
		desc.mipmapFilter = MipmapFilterMode::Nearest;
		desc.lodMinClamp = 0.0f;
		desc.lodMaxClamp = 32.0f;
		desc.compare = CompareFunction::Undefined;
		desc.maxAnisotropy = 1;
		g.sampler = g.device.createSampler(desc);
		if (!g.sampler)
		{
			std::cerr << "WebGPU: createSampler returned null\n";
			return false;
		}
		return true;
	}

	// Bind group layout 0: the contract between the shader's
	// @group(0) @binding(n) declarations and the resources a bind group
	// supplies. Binding 0 is a sampled 2D float texture, binding 1 a
	// filtering sampler, both visible to the fragment stage only.
	// The pipeline layout lists the bind group layouts by group index.
	bool createLayouts()
	{
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

		// Bind group layout 1: the camera uniform, read by the vertex stage.
		BindGroupLayoutEntry cameraEntry = Default;
		cameraEntry.binding = 0;
		cameraEntry.visibility = ShaderStage::Vertex;
		cameraEntry.buffer.type = BufferBindingType::Uniform;
		cameraEntry.buffer.hasDynamicOffset = false;   // milestone 6b turns this on
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

		// The pipeline layout lists the group layouts by group index.
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

	// The uniform buffer and the bind group that hands it to the shader.
	// Created once; the contents are rewritten every frame.
	bool createCameraUniform()
	{
		BufferDescriptor desc = Default;
		desc.label = StringView("camera uniforms");
		desc.usage = BufferUsage::Uniform | BufferUsage::CopyDst;
		desc.size = sizeof(CameraUniforms);
		desc.mappedAtCreation = false;
		g.uniformBuffer = g.device.createBuffer(desc);
		if (!g.uniformBuffer)
		{
			std::cerr << "WebGPU: createBuffer (uniform) returned null\n";
			return false;
		}

		BindGroupEntry entry = Default;
		entry.binding = 0;
		entry.buffer = g.uniformBuffer;
		entry.offset = 0;
		entry.size = sizeof(CameraUniforms);
		entry.sampler = nullptr;
		entry.textureView = nullptr;

		BindGroupDescriptor groupDesc = Default;
		groupDesc.label = StringView("camera bind group");
		groupDesc.layout = g.cameraBindGroupLayout;
		groupDesc.entryCount = 1;
		groupDesc.entries = &entry;
		g.cameraBindGroup = g.device.createBindGroup(groupDesc);
		if (!g.cameraBindGroup)
		{
			std::cerr << "WebGPU: createBindGroup (camera) returned null\n";
			return false;
		}
		return true;
	}

	// Loads a PNG with stb_image, uploads it as an RGBA8 texture with one
	// mip level, creates its view, and builds the bind group that hands the
	// view plus the shared sampler to the shader. Rows are uploaded top-first,
	// exactly as stb_image returns them: no flip. Returns a handle (id 0 on
	// failure) into the registry.
	Texture createTextureFromFile(const char *path)
	{
		int width = 0, height = 0, channels = 0;
		stbi_set_flip_vertically_on_load(0);
		unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
		if (!pixels)
		{
			std::cerr << "WebGPU: cannot load image " << path << ": " << stbi_failure_reason() << "\n";
			return Texture{};
		}
		TextureEntry entry;
		entry.width = width;
		entry.height = height;

		// The texture: GPU pixel storage with a fixed format and usage.
		// TextureBinding: a shader may sample it. CopyDst: the queue may write it.
		TextureDescriptor texDesc = Default;
		texDesc.label = StringView(path);
		texDesc.dimension = TextureDimension::_2D;
		texDesc.size.width = (uint32_t)width;
		texDesc.size.height = (uint32_t)height;
		texDesc.size.depthOrArrayLayers = 1;
		texDesc.format = TextureFormat::RGBA8Unorm;
		texDesc.mipLevelCount = 1;
		texDesc.sampleCount = 1;
		texDesc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
		texDesc.viewFormatCount = 0;
		texDesc.viewFormats = nullptr;
		entry.texture = g.device.createTexture(texDesc);
		if (!entry.texture)
		{
			std::cerr << "WebGPU: createTexture returned null\n";
			stbi_image_free(pixels);
			return Texture{};
		}

		// Upload: destination is mip 0 at origin; the source layout says how
		// the CPU rows are laid out. This is glTexImage2D.
		TexelCopyTextureInfo destination = Default;
		destination.texture = entry.texture;
		destination.mipLevel = 0;
		destination.origin.x = 0;
		destination.origin.y = 0;
		destination.origin.z = 0;
		destination.aspect = TextureAspect::All;

		TexelCopyBufferLayout sourceLayout = Default;
		sourceLayout.offset = 0;
		sourceLayout.bytesPerRow = 4 * (uint32_t)width;
		sourceLayout.rowsPerImage = (uint32_t)height;

		const size_t byteCount = (size_t)4 * width * height;
		g.queue.writeTexture(destination, pixels, byteCount, sourceLayout, texDesc.size);
		stbi_image_free(pixels);

		// The view the shader samples through.
		TextureViewDescriptor viewDesc = Default;
		viewDesc.label = StringView("sprite view");
		viewDesc.format = TextureFormat::RGBA8Unorm;
		viewDesc.dimension = TextureViewDimension::_2D;
		viewDesc.baseMipLevel = 0;
		viewDesc.mipLevelCount = 1;
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect = TextureAspect::All;
		viewDesc.usage = TextureUsage::TextureBinding;
		entry.view = entry.texture.createView(viewDesc);
		if (!entry.view)
		{
			std::cerr << "WebGPU: createView returned null\n";
			entry.texture.release();
			return Texture{};
		}

		// The bind group: this view and the shared sampler, in the slots the
		// layout declared. Milestone 7 builds one of these per loaded texture.
		BindGroupEntry bindEntries[2];
		bindEntries[0] = Default;
		bindEntries[0].binding = 0;
		bindEntries[0].textureView = entry.view;
		bindEntries[0].buffer = nullptr;
		bindEntries[0].sampler = nullptr;
		bindEntries[1] = Default;
		bindEntries[1].binding = 1;
		bindEntries[1].sampler = g.sampler;
		bindEntries[1].buffer = nullptr;
		bindEntries[1].textureView = nullptr;

		BindGroupDescriptor groupDesc = Default;
		groupDesc.label = StringView("sprite bind group");
		groupDesc.layout = g.textureBindGroupLayout;
		groupDesc.entryCount = 2;
		groupDesc.entries = bindEntries;
		entry.bindGroup = g.device.createBindGroup(groupDesc);
		if (!entry.bindGroup)
		{
			std::cerr << "WebGPU: createBindGroup returned null\n";
			entry.view.release();
			entry.texture.release();
			return Texture{};
		}

		textures.push_back(entry);
		return Texture{ (uint32_t)textures.size() };
	}

	// The render pipeline: every configurable stage of the GPU's fixed
	// triangle pipeline, baked into one immutable object. Selected per pass
	// with setPipeline; never mutated.
	bool createQuadPipeline()
	{
		ShaderModule module = createShaderModuleFromFile(RESOURCES_PATH "shaders/quad.wgsl");
		if (!module) { return false; }

		RenderPipelineDescriptor desc = Default;
		desc.label = StringView("quad pipeline");

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

		// Vertex stage: one vertex buffer in slot 0.
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

		// The explicit layout: group 0 is the texture + sampler group.
		desc.layout = g.pipelineLayout;

		g.quadPipeline = g.device.createRenderPipeline(desc);

		// The pipeline holds what it needs from the module; the module itself
		// can go now.
		module.release();

		if (!g.quadPipeline)
		{
			std::cerr << "WebGPU: createRenderPipeline returned null\n";
			return false;
		}
		return true;
	}

	// Makes sure the vertex buffer can hold `bytes`. A GPU buffer cannot be
	// resized, so growth means creating a bigger one (doubling) and releasing
	// the old one; WebGPU keeps the old buffer alive until submitted work
	// that reads it has finished. Never shrinks. Starts small on purpose so
	// the growth path runs early and visibly.
	bool ensureVertexBufferCapacity(uint64_t bytes)
	{
		if (g.vertexBuffer && bytes <= g.vertexBufferCapacityBytes) { return true; }

		uint64_t capacity = g.vertexBufferCapacityBytes ? g.vertexBufferCapacityBytes : 64 * sizeof(Vertex);
		while (capacity < bytes) { capacity *= 2; }

		// A buffer is GPU memory with a fixed purpose. Vertex: a pipeline may
		// read it as vertex input. CopyDst: the queue may write into it.
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

	// gl2d's flush: upload the accumulated batch and draw it. One texture per
	// frame in 6a, so one draw; 6b splits the batch into runs per texture.
	void flushBatch(RenderPassEncoder pass)
	{
		if (batchVertices.empty()) { return; }

		const uint64_t bytes = batchVertices.size() * sizeof(Vertex);
		static_assert(sizeof(Vertex) % 4 == 0, "writeBuffer size must be a multiple of 4");
		if (!ensureVertexBufferCapacity(bytes)) { return; }

		// Queue-ordered: the copy lands after last frame's draw has finished
		// reading this buffer, so one buffer is enough without double buffering.
		g.queue.writeBuffer(g.vertexBuffer, 0, batchVertices.data(), bytes);

		const Texture first = batchQuadTextures.front();
		for (const Texture &t : batchQuadTextures)
		{
			if (t.id != first.id)
			{
				std::cerr << "render: mixed textures in one batch are not supported until milestone 6b\n";
				break;
			}
		}

		pass.setPipeline(g.quadPipeline);
		pass.setBindGroup(0, textures[first.id - 1].bindGroup, 0, nullptr); // group 0 = texture + sampler
		pass.setBindGroup(1, g.cameraBindGroup, 0, nullptr);               // group 1 = camera uniform
		pass.setVertexBuffer(0, g.vertexBuffer, 0, bytes);
		pass.draw((uint32_t)batchVertices.size(), 1, 0, 0);
	}

	void clearBatch()
	{
		batchVertices.clear();
		batchQuadTextures.clear();
	}

	// Milestone 6a demo: the whole sheet at native size as an orientation
	// reference, plus a few hundred spinning tinted ships picked from its
	// 5x2 cells. Cell coordinates are given in gl2d's bottom-origin
	// convention so the boundary conversion in renderRectangle is exercised.
	void buildDemoSprites()
	{
		srand(7);
		auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); };
		demoSprites.clear();
		for (int i = 0; i < 400; i++)
		{
			DemoSprite d;
			d.position = { rnd(-1200, 1800), rnd(-800, 1300) };
			d.size = rnd(40, 140);
			d.rotation = rnd(0, 360);
			d.spin = rnd(-90, 90);
			d.color = { rnd(0.5f, 1), rnd(0.5f, 1), rnd(0.5f, 1), rnd(0.6f, 1) };
			d.cellX = rand() % 5;
			d.cellY = rand() % 2;
			demoSprites.push_back(d);
		}
	}

	glm::vec4 demoCellUV(int cellX, int cellY)
	{
		// Top-left-origin cell rect, then expressed in gl2d's convention
		// (v measured from the bottom) since that is what renderRectangle takes.
		const float u0 = cellX / 5.0f, u1 = (cellX + 1) / 5.0f;
		const float vTop = cellY / 2.0f, vBottom = (cellY + 1) / 2.0f;
		return { u0, 1.0f - vTop, u1, 1.0f - vBottom };
	}

	void drawDemo(float deltaTime)
	{
		const TextureEntry &sheet = textures[demoSheet.id - 1];
		renderRectangle({100, 100, (float)sheet.width, (float)sheet.height}, demoSheet);

		for (DemoSprite &d : demoSprites)
		{
			d.rotation += d.spin * deltaTime;
			renderRectangle({ d.position.x, d.position.y, d.size, d.size }, demoSheet,
				d.color, {}, d.rotation, demoCellUV(d.cellX, d.cellY));
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

#ifdef __APPLE__
	// Pin the layer's color space so windowed and fullscreen presentation
	// agree (otherwise fullscreen shows slightly lighter colors on macOS).
	if (pinMetalLayerColorSpaceToSRGB(window))
	{
		std::cout << "WebGPU Metal layer color space pinned to sRGB\n";
	}
	else
	{
		std::cerr << "WebGPU: window layer is not a CAMetalLayer; color space not pinned\n";
	}
#endif

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
	// 8. Sampler and layouts (milestone 4). The pipeline needs the layout.
	if (!createSampler() || !createLayouts())
	{
		return false;
	}
	std::cout << "WebGPU sampler and bind group layout created\n";

	// 9. The quad pipeline (milestones 2, 3, 4). Depends on the surface
	//    format and the pipeline layout.
	if (!createQuadPipeline())
	{
		return false;
	}
	std::cout << "WebGPU quad pipeline created\n";

	// 10. The test texture and its bind group (milestone 4), now a registry
	//     entry behind a handle (milestone 6a).
	demoSheet = createTextureFromFile(RESOURCES_PATH "spaceShip/stitchedFiles/spaceships.png");
	if (demoSheet.id == 0)
	{
		return false;
	}
	std::cout << "WebGPU texture uploaded: " << textures[demoSheet.id - 1].width << "x"
		<< textures[demoSheet.id - 1].height << ", bind group created, handle id " << demoSheet.id << "\n";

	// 11. The vertex buffer is created on first flush (milestone 6a) and
	//     grows as needed. Nothing to do here.
	buildDemoSprites();

	// 12. The camera uniform (milestone 5).
	if (!createCameraUniform())
	{
		return false;
	}
	std::cout << "WebGPU camera uniform buffer created: " << sizeof(CameraUniforms) << " bytes\n";
	g.lastFrameTime = std::chrono::steady_clock::now();
	std::cout.flush();
	return true;
}

void wgpuRenderFrame()
{
	// Resize detection. On Metal, wgpu-native keeps presenting a drawable of
	// the configured size and the layer stretches it to the window; the
	// surface never reports itself Outdated. So compare the framebuffer size
	// every frame and reconfigure when it changed. The Outdated/Lost path
	// below stays as a backstop for backends that do report it.
	{
		int w = 0, h = 0;
		glfwGetFramebufferSize(g.window, &w, &h);
		if (!g.surfaceConfigured || w != g.surfaceWidth || h != g.surfaceHeight)
		{
			configureSurface();
			if (!g.surfaceConfigured) { return; } // minimized: nothing to draw
		}
	}

	// 1. Acquire: the texture that will next go on screen.
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

	// Demo camera (milestone 5): follow a point circling the sprite with the
	// game's own follow parameters, and breathe the zoom, so both the follow
	// math and zoom-about-center are exercised. Milestone 7 replaces this
	// with the game's camera calls.
	{
		auto now = std::chrono::steady_clock::now();
		float deltaTime = std::chrono::duration<float>(now - g.lastFrameTime).count();
		g.lastFrameTime = now;
		if (deltaTime > 0.1f) { deltaTime = 0.1f; }
		g.demoTime += deltaTime;

		const TextureEntry &sheet = textures[demoSheet.id - 1];
		const glm::vec2 spriteCenter(100.0f + sheet.width / 2.0f, 100.0f + sheet.height / 2.0f);
		const glm::vec2 target = spriteCenter + 150.0f * glm::vec2(std::cos(g.demoTime * 0.7f), std::sin(g.demoTime * 0.7f));
		demoCamera.follow(target, deltaTime * 550.0f, 1.0f, 150.0f, (float)g.surfaceWidth, (float)g.surfaceHeight);
		demoCamera.zoom = 0.75f + 0.25f * std::sin(g.demoTime * 0.5f);

		// Accumulate this frame's quads (milestone 6a). Nothing touches the
		// GPU until flushBatch below.
		clearBatch();
		drawDemo(deltaTime);
	}

	// Camera matrix from the current framebuffer size. This and the surface
	// configuration must agree on the size, or sprites stretch on resize.
	CameraUniforms uniforms;
	uniforms.viewProj = buildViewProj(demoCamera, (float)g.surfaceWidth, (float)g.surfaceHeight);
	g.queue.writeBuffer(g.uniformBuffer, 0, &uniforms, sizeof(uniforms));

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
	flushBatch(pass);
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
	if (g.cameraBindGroup) { g.cameraBindGroup.release(); g.cameraBindGroup = nullptr; }
	if (g.uniformBuffer) { g.uniformBuffer.release(); g.uniformBuffer = nullptr; }
	if (g.vertexBuffer) { g.vertexBuffer.release(); g.vertexBuffer = nullptr; g.vertexBufferCapacityBytes = 0; }
	for (TextureEntry &t : textures)
	{
		if (t.bindGroup) { t.bindGroup.release(); }
		if (t.view) { t.view.release(); }
		if (t.texture) { t.texture.release(); }
	}
	textures.clear();
	if (g.quadPipeline) { g.quadPipeline.release(); g.quadPipeline = nullptr; }
	if (g.pipelineLayout) { g.pipelineLayout.release(); g.pipelineLayout = nullptr; }
	if (g.cameraBindGroupLayout) { g.cameraBindGroupLayout.release(); g.cameraBindGroupLayout = nullptr; }
	if (g.textureBindGroupLayout) { g.textureBindGroupLayout.release(); g.textureBindGroupLayout = nullptr; }
	if (g.sampler) { g.sampler.release(); g.sampler = nullptr; }
	if (g.surface && g.surfaceConfigured) { g.surface.unconfigure(); g.surfaceConfigured = false; }
	if (g.queue) { g.queue.release(); g.queue = nullptr; }
	if (g.device) { g.device.release(); g.device = nullptr; }
	if (g.adapter) { g.adapter.release(); g.adapter = nullptr; }
	if (g.surface) { g.surface.release(); g.surface = nullptr; }
	if (g.instance) { g.instance.release(); g.instance = nullptr; }
}

}

#endif // RENDERER_WEBGPU
