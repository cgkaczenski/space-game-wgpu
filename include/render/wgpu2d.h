#pragma once

// wgpu2d: the WebGPU renderer behind gl2d's public shape. Only the subset
// the game calls is here. Names, parameter order, and defaults match
// gl2d so the game files could switch by include and namespace.
//
// One Renderer2D instance is supported (the game has one). The GPU
// context itself (instance, device, surface, frame) lives in
// render/wgpuContext.h and is driven by the platform loop.

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wgpu2d
{
	// gl2d::init() equivalent. The GPU context is created by
	// render::wgpuInit in the platform layer; this is a no-op kept for the
	// call sites.
	void init();

	using Color4f = glm::vec4;
	using Rect = glm::vec4; // x, y, width, height in world pixels, y down

	// An addition to gl2d's shape, not a mirror of it: gl2d had one blend
	// state for the whole program. Blending is baked into a render pipeline,
	// so a second mode means a second pipeline, and the batch has to break a
	// draw run when the mode changes -- the same way it already breaks on
	// texture and camera.
	enum class BlendMode
	{
		// gl2d's blend, and the default: src * srcAlpha + dst * (1 - srcAlpha).
		Alpha,
		// Light adds: src * srcAlpha + dst. Nothing gets darker, so overlapping
		// sprites build up. For muzzle flashes, explosions, thrusters.
		Additive,
		// src + dst * (1 - srcAlpha): "over", for a source whose colour is
		// already multiplied by its own coverage. That is exactly what a
		// render target holds -- drawing (C, a) into a transparent target with
		// Alpha leaves (C*a, a) -- so this is the mode for drawing a
		// FrameBuffer back. Using Alpha there multiplies by coverage a second
		// time and the result comes out dark.
		Premultiplied,
	};

	// gl2d's texture-coordinate convention: {u0, v0, u1, v1} with v measured
	// from the bottom. Converted to WebGPU's top-left origin inside the renderer.
	#define WGPU2D_DefaultTextureCoords (glm::vec4{ 0, 1, 1, 0 })

	#define Colors_Red (wgpu2d::Color4f{ 1, 0, 0, 1 })
	#define Colors_Green (wgpu2d::Color4f{ 0, 1, 0, 1 })
	#define Colors_Blue (wgpu2d::Color4f{ 0, 0, 1, 1 })
	#define Colors_Black (wgpu2d::Color4f{ 0, 0, 0, 1 })
	#define Colors_White (wgpu2d::Color4f{ 1, 1, 1, 1 })
	#define Colors_Yellow (wgpu2d::Color4f{ 1, 1, 0, 1 })
	#define Colors_Magenta (wgpu2d::Color4f{ 1, 0, 1, 1 })
	#define Colors_Turqoise (wgpu2d::Color4f{ 0, 1, 1, 1 })
	#define Colors_Orange (wgpu2d::Color4f{ 1, (float)0x7F / 255.0f, 0, 1 })
	#define Colors_Purple (wgpu2d::Color4f{ 101.0f / 255.0f, 29.0f / 255.0f, 173.0f / 255.0f, 1 })
	#define Colors_Gray (wgpu2d::Color4f{ (float)0x7F / 255.0f, (float)0x7F / 255.0f, (float)0x7F / 255.0f, 1 })
	#define Colors_Transparent (wgpu2d::Color4f{ 0,0,0,0 })

	// A copyable handle like gl2d's Texture { GLuint id }; 0 means invalid.
	struct Texture
	{
		uint32_t id = 0;

		Texture() {};

		glm::ivec2 GetSize();

		// RGBA8 pixels, rows top-first. Mipmaps are generated on the CPU.
		void createFromBuffer(const char *image_data, const int width, const int height,
			bool pixelated = false, bool useMipMaps = true);

		void create1PxSquare(const char *b = 0);

		void loadFromFile(const char *fileName, bool pixelated = false, bool useMipMaps = true);

		// Like gl2d: re-lays the image out with a 2-pixel gutter around every
		// blockSize x blockSize cell (edge pixels duplicated) so filtering
		// never bleeds neighbors. Use TextureAtlasPadding for the coordinates.
		void loadFromFileWithPixelPadding(const char *fileName, int blockSize,
			bool pixelated = false, bool useMipMaps = true);

		void cleanup();
	};

	glm::vec4 computeTextureAtlas(int xCount, int yCount, int x, int y, bool flip = 0);
	glm::vec4 computeTextureAtlasWithPadding(int mapXsize, int mapYsize, int xCount, int yCount, int x, int y, bool flip = 0);

	struct TextureAtlas
	{
		TextureAtlas() {};
		TextureAtlas(int x, int y) :xCount(x), yCount(y) {};

		int xCount = 0;
		int yCount = 0;

		glm::vec4 get(int x, int y, bool flip = 0)
		{
			return computeTextureAtlas(xCount, yCount, x, y, flip);
		}
	};

	struct TextureAtlasPadding
	{
		TextureAtlasPadding() {};

		TextureAtlasPadding(int x, int y, int xSize, int ySize) :xCount(x), yCount(y)
			, xSize(xSize), ySize(ySize)
		{
		};

		int xCount = 0;
		int yCount = 0;
		int xSize = 0;
		int ySize = 0;

		glm::vec4 get(int x, int y, bool flip = 0)
		{
			return computeTextureAtlasWithPadding(xSize, ySize, xCount, yCount, x, y, flip);
		}
	};

	// gl2d's FrameBuffer: a texture the renderer draws into instead of the
	// screen. `texture` is an ordinary handle, so the result is drawn back
	// with renderRectangle like any sprite. In WebGPU there is no framebuffer
	// object at all -- a render pass's color attachment is just a texture
	// view -- so `fbo` is simply the texture's id, non-zero once created.
	//
	// The target's format is the surface's, because a pipeline may only draw
	// into an attachment matching the format it was built for.
	struct FrameBuffer
	{
		FrameBuffer() {};
		explicit FrameBuffer(unsigned int w, unsigned int h) { create(w, h); };

		unsigned int fbo = 0;
		Texture texture = {};

		// `pixelated` is an addition to gl2d's signature (nearest instead of
		// linear filtering when the result is drawn back), for the
		// pixel-perfect upscale case. Defaulted, so gl2d call sites compile.
		void create(unsigned int w, unsigned int h, bool pixelated = false);
		void resize(unsigned int w, unsigned int h);

		// Releases the texture. Does not touch anything drawn into it.
		void cleanup();

		// Clears to transparent black. Immediate if a frame is open,
		// otherwise applied when the target is next drawn into.
		void clear();

		// Note on translucency: a draw of (C, a) into a transparent target
		// leaves (C*a, a) -- the target's contents are premultiplied by
		// coverage, whatever the source was. Draw the result back with
		// BlendMode::Premultiplied, which is what compositing expects; with
		// BlendMode::Alpha the coverage is applied a second time and a
		// translucent target comes out darker than the same draw made
		// directly. Opaque targets round-trip identically either way.
	};

	// gl2d's Camera. Rotation is accepted but not applied (the game never sets it).
	struct Camera
	{
		glm::vec2 position = {};
		float rotation = 0.f;
		float zoom = 1.0;

		void setDefault() { *this = Camera{}; }

		void follow(glm::vec2 pos, float speed, float min, float max, float w, float h);
	};

	struct Renderer2D
	{
		Renderer2D() {};
		Renderer2D(Renderer2D &other) = delete;
		Renderer2D(Renderer2D &&other) = delete;
		Renderer2D operator=(Renderer2D other) = delete;
		Renderer2D operator=(Renderer2D &other) = delete;
		Renderer2D operator=(Renderer2D &&other) = delete;

		// The GPU objects are owned by the context; this only reserves batch
		// memory and records the default target (gl2d's defaultFBO): 0 draws
		// to the screen, a FrameBuffer's fbo draws into that texture.
		void create(unsigned int fbo = 0, size_t quadCount = 1000);
		unsigned int defaultFBO = 0;
		void cleanup();

		Camera currentCamera = {};
		std::vector<Camera> cameraPushPop;

		// Applies to every quad recorded after it, until it is set again.
		// Not a stack like the camera: nothing nests blend modes, and a
		// missing pop would be silent where a missing popCamera is obvious.
		BlendMode currentBlendMode = BlendMode::Alpha;
		void setBlendMode(BlendMode mode) { currentBlendMode = mode; }
		void pushCamera(Camera c = {});
		void popCamera();

		// The camera's visible world rectangle. Doesn't take rotation into account.
		glm::vec4 getViewRect();

		// Window metrics, should be up to date at all times. The game passes
		// the framebuffer size, which is what the camera matrix uses.
		int windowW = -1;
		int windowH = -1;
		void updateWindowMetrics(int w, int h) { windowW = w; windowH = h; }

		void clearDrawData();

		void renderRectangle(const Rect transforms, const Texture texture, const Color4f colors[4], const glm::vec2 origin = {}, const float rotationDegrees = 0.f, const glm::vec4 textureCoords = WGPU2D_DefaultTextureCoords);
		inline void renderRectangle(const Rect transforms, const Texture texture, const Color4f colors = {1,1,1,1}, const glm::vec2 origin = {}, const float rotationDegrees = 0, const glm::vec4 textureCoords = WGPU2D_DefaultTextureCoords)
		{
			Color4f c[4] = { colors,colors,colors,colors };
			renderRectangle(transforms, texture, c, origin, rotationDegrees, textureCoords);
		}

		void renderRectangleAbsRotation(const Rect transforms, const Texture texture, const Color4f colors[4], const glm::vec2 origin = {}, const float rotationDegrees = 0.f, const glm::vec4 textureCoords = WGPU2D_DefaultTextureCoords);
		inline void renderRectangleAbsRotation(const Rect transforms, const Texture texture, const Color4f colors = {1,1,1,1}, const glm::vec2 origin = {}, const float rotationDegrees = 0.f, const glm::vec4 textureCoords = WGPU2D_DefaultTextureCoords)
		{
			Color4f c[4] = { colors,colors,colors,colors };
			renderRectangleAbsRotation(transforms, texture, c, origin, rotationDegrees, textureCoords);
		}

		void renderRectangle(const Rect transforms, const Color4f colors[4], const glm::vec2 origin = { 0,0 }, const float rotationDegrees = 0);
		inline void renderRectangle(const Rect transforms, const Color4f colors, const glm::vec2 origin = { 0,0 }, const float rotationDegrees = 0)
		{
			Color4f c[4] = { colors,colors,colors,colors };
			renderRectangle(transforms, c, origin, rotationDegrees);
		}

		void renderLine(const glm::vec2 position, const float angleDegrees, const float length, const Color4f color, const float width = 2.f);
		void renderLine(const glm::vec2 start, const glm::vec2 end, const Color4f color, const float width = 2.f);
		void renderCircleOutline(const glm::vec2 position, const Color4f color, const float size, const float width = 2.f, const unsigned int segments = 16);

		// Records the color the frame's render pass clears to. The clear
		// itself happens when the pass begins (first flush, or end of frame).
		void clearScreen(const Color4f color = Color4f{0,0,0,0});

		void setCamera(const Camera camera) { currentCamera = camera; }

		// Uploads the accumulated quads and draws them into the current frame,
		// into defaultFBO.
		void flush(bool clearDrawData = true);

		// Same, but into the given render target instead. The camera
		// projection uses the target's own size.
		void flushFBO(FrameBuffer frameBuffer, bool clearDrawData = true);
	};
}
