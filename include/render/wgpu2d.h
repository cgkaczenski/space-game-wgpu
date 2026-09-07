#pragma once

// wgpu2d: the 2D drawing library. The game includes this header and nothing
// else from render/. The GPU context (instance, device, surface, frame) lives
// in render/wgpuContext.h and is driven by the platform loop.
//
// It started as gl2d's signatures so the game could switch by include and
// namespace. New drawing capabilities belong here too — BlendMode, LayerEffect
// — rather than in sibling headers. Matching gl2d is how the port landed, not
// a ceiling on the API.
//
// One Renderer2D instance is supported (the game has one).

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

	struct Renderer2D; // LayerEffect::flush takes one; the full type is below

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

	// How a composed layer is drawn back. Screen-space pixels, and degrees
	// about the layer's centre.
	//
	// Deliberately just these two. A tint, a scale and a per-effect shader all
	// have plausible futures, and adding a field when one arrives is a few
	// lines; a transform designed around three imagined callers is how the
	// wrong abstraction gets built.
	struct LayerTransform
	{
		glm::vec2 offsetPixels = {};
		float rotationDegrees = 0.f;

		// Exact comparison on purpose: a caller that settles its animation to
		// exactly zero at rest gets LayerEffect's identity bypass, and one
		// that decays asymptotically never does. That is a real property to
		// design to, not a float-equality bug.
		bool isIdentity() const
		{
			return offsetPixels.x == 0.f && offsetPixels.y == 0.f && rotationDegrees == 0.f;
		}
	};

	// Flush everything recorded so far through an offscreen target, then draw
	// that target back under a transform. The caller owns the transform
	// (offset, rotation, when it is identity). This is the mechanism; what
	// the layer is, and why it moves, is the caller's.
	//
	// Why the round trip is worth it: a composed layer can be moved and
	// rotated as one image. Nudging each quad separately cannot rotate a
	// group about a shared centre without shearing the layout apart.
	struct LayerEffect
	{
		// Flushes everything the renderer has recorded since its last flush
		// through the target, then records one quad drawing that target back
		// under `transform`. The caller's next flush is what puts it on screen,
		// on top of whatever was flushed before it.
		//
		// `width` and `height` are the framebuffer size -- the same values the
		// game passes to updateWindowMetrics -- and the target follows them.
		//
		// An identity transform skips the target entirely and flushes straight
		// to the screen. That is not an optimisation the caller has to
		// remember: it keeps the untransformed case pixel-exact, since a round
		// trip through a same-size target is only byte-identical for opaque
		// pixels.
		void flush(Renderer2D &renderer, int width, int height, const LayerTransform &transform);

		// Releases the target. Safe to call twice, and safe never to call.
		void cleanup();

		// The composed layer. Screen-sized, resized with the window so the
		// composite is 1:1.
		FrameBuffer target;
	};

	// A camera is a transform: where the view sits and how far it is zoomed.
	// buildViewProj turns those into the matrix the vertex shader applies.
	//
	// It used to carry two things it should not have. `follow` chased a point
	// and drew nothing, so it is a behaviour and now lives in the game (see
	// gameLayer/cameraFollow.h, and roadmap R7 for where it goes next).
	// `rotation` was accepted and applied by nothing at all -- and `sameCamera`
	// did not compare it either, so implementing it without also fixing the
	// batch's run key would have let two cameras differing only in roll share
	// a uniform slot and silently render with the wrong matrix. A roll can be
	// added when something wants one, in buildViewProj and sameCamera
	// together, checked numerically the way milestone 5 checked this matrix.
	struct Camera
	{
		glm::vec2 position = {};
		float zoom = 1.0;

		void setDefault() { *this = Camera{}; }
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
