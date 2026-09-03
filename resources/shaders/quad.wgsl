// Milestone 4: a textured quad.
//
// Each vertex arrives as the attributes the pipeline's vertex layout maps to
// these locations: location 0 position (Float32x2), location 1 color
// (Float32x4), location 2 texture coordinate (Float32x2). Positions are
// still clip space: x and y in [-1, 1], y up, origin at the center.
// Milestone 5 adds the camera matrix that maps gl2d's y-down pixel world
// into this space.
//
// Texture coordinates use WebGPU's convention: (0, 0) is the top-left
// texel, v grows downward. stb_image hands rows back top-first, so images
// are uploaded as-is with no flip. gl2d's flipped-at-load convention is
// converted at the API boundary in milestone 7.

struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};

// Anything tagged @location in the output is interpolated across the
// triangle before the fragment shader sees it.
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};

// Resources come in through bind groups. Group 0 is the per-texture group:
// the sampled texture at binding 0 and the sampler at binding 1. The
// pipeline's bind group layout 0 must declare exactly these.
@group(0) @binding(0) var spriteTexture: texture_2d<f32>;
@group(0) @binding(1) var spriteSampler: sampler;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4f(in.position, 0.0, 1.0);
    out.color = in.color;
    out.uv = in.uv;
    return out;
}

// gl2d's whole fragment shader: vertex color times the sampled texel.
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return in.color * textureSample(spriteTexture, spriteSampler, in.uv);
}
