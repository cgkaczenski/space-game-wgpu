// The paused world: grey and dim.
//
// A fragment stage only; the vertex stage is the sprite shader's. See the
// contract in include/render/wgpu2d.h. It runs on one quad covering the view,
// textured with the world as it was drawn this frame, so it is the sprite
// shader's sample with two lines after it.

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};

@group(0) @binding(0) var spriteTexture: texture_2d<f32>;
@group(0) @binding(1) var spriteSampler: sampler;

struct EffectUniforms {
    resolution: vec4f,
    time: vec4f,
    a: vec4f, // x = desaturation 0..1, y = brightness multiplier
    b: vec4f,
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

const lumaWeights = vec3f(0.2126, 0.7152, 0.0722);

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let texel = textureSampleLevel(spriteTexture, spriteSampler, in.uv, 0.0);
    // Toward its own luminance, not toward an average grey, so the bright
    // things stay bright relative to the dark ones and the scene still reads.
    let grey = vec3f(dot(texel.rgb, lumaWeights));
    let graded = mix(texel.rgb, grey, clamp(effect.a.x, 0.0, 1.0)) * effect.a.y;
    // Premultiplied in, premultiplied out: alpha scales nothing extra here.
    return vec4f(graded, texel.a) * in.color;
}
