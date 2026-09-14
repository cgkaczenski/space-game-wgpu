// The shield breaking: the bubble burns away in patches, with a bright edge
// where it is still going.
//
// A fragment stage only; the vertex stage is the sprite shader's. See the
// contract in include/render/wgpu2d.h. It runs in place of the sprite shader on
// the shield's own quads -- the glass and both rim rings -- so it samples their
// textures and tints exactly as the sprite shader would, then removes what the
// threshold has passed.
//
// The noise is computed, not sampled. That is what made this "nothing new in
// the renderer": no noise texture to load, just a hash, so one threshold is the
// only parameter a dissolve needs.

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
    a: vec4f, // x = progress 0..1, y = edge width, z = noise scale, w = edge strength
    b: vec4f, // rgb = edge colour
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

fn hash(p: vec2f) -> f32 {
    let q = fract(p * vec2f(123.34, 456.21));
    let r = q + dot(q, q + 45.32);
    return fract(r.x * r.y);
}

// Value noise: smooth between hashed lattice points, so patches have soft,
// blobby outlines instead of the salt-and-pepper look of a raw hash.
fn valueNoise(p: vec2f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = hash(i);
    let b = hash(i + vec2f(1.0, 0.0));
    let c = hash(i + vec2f(0.0, 1.0));
    let d = hash(i + vec2f(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let progress = effect.a.x;
    let edgeWidth = max(effect.a.y, 0.001);
    let scale = effect.a.z;
    let edgeStrength = effect.a.w;

    // What the sprite shader would have drawn.
    let base = textureSample(spriteTexture, spriteSampler, in.uv) * in.color;

    // Two octaves: large patches, with smaller bites taken out of their edges.
    let n = 0.65 * valueNoise(in.uv * scale) + 0.35 * valueNoise(in.uv * scale * 2.7 + 17.0);

    // Pushed slightly past 1 so the last patch is gone exactly at progress 1.
    let threshold = progress * 1.08;
    let keep = step(threshold, n);

    // Glow in the band just above the threshold: the part about to go.
    let glow = (1.0 - smoothstep(0.0, edgeWidth, n - threshold)) * keep * step(0.001, progress);

    // Only inside the sphere, so the edge does not light the quad's corners.
    let fromCentre = (in.uv - vec2f(0.5)) * 2.0;
    let inside = step(length(fromCentre), 1.0);

    let alpha = max(base.a * keep, glow * edgeStrength * inside);
    let rgb = mix(base.rgb, effect.b.rgb, glow);
    return vec4f(rgb, alpha);
}
