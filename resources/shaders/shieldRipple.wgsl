// One impact ripple travelling across the shield.
//
// A fragment stage only; the vertex stage is the sprite shader's. See the
// contract in include/render/wgpu2d.h.
//
// One of these runs per impact, each as its own quad with its own parameters.
// That is what the per-quad channel bought: several overlapping ripples need
// no array in a uniform, because "several" is just several quads.

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
    a: vec4f, // xy = impact point in quad uv, z = seconds since impact, w = intensity
    b: vec4f, // x = wave speed (uv/s), y = band half-width (uv)
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let impact = effect.a.xy;
    let elapsed = effect.a.z;
    let intensity = effect.a.w;
    let speed = effect.b.x;
    let band = max(effect.b.y, 0.001);

    // Outside the sphere there is no shell to carry a wave. -1..1 across the
    // quad, so r is the same radius term the shell texture is built on.
    let fromCentre = (in.uv - vec2f(0.5)) * 2.0;
    let r = length(fromCentre);
    if (r > 1.0 || intensity <= 0.0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    // The wave front, expanding from where the hit landed.
    let travelled = elapsed * speed;
    let ring = max(0.0, 1.0 - abs(distance(in.uv, impact) - travelled) / band);

    // Brighter where the shell turns away from the viewer, so the wave looks
    // like it is running over a surface rather than across a flat disc. Same
    // fresnel term the shell texture uses, recomputed here because this shader
    // does not sample that texture.
    let nz = sqrt(max(0.0, 1.0 - r * r));
    let surface = 0.35 + 0.65 * (1.0 - nz);

    let value = ring * ring * intensity * surface;
    return vec4f(vec3f(0.70, 0.88, 1.0) * value, 1.0);
}
