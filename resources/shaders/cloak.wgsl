// A refraction cloak: the scene behind the ship bends rather than fading.
//
// This is a fragment stage only. The vertex stage is the sprite shader's, so
// the declarations below have to match what it outputs and what the pipeline
// layout binds -- see the contract in include/render/wgpu2d.h.
//
// Why this cannot be a blend mode: blending combines a fragment with the
// destination, it cannot *read* the destination and move it, and core WebGPU
// has no framebuffer fetch (outline 14). Displacing what is behind something
// means sampling a texture that already holds the scene, which is why the
// world is rendered into a target first and this shader runs over it.

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};

@group(0) @binding(0) var spriteTexture: texture_2d<f32>;
@group(0) @binding(1) var spriteSampler: sampler;

struct EffectUniforms {
    resolution: vec4f, // xy = pixels, zw = 1 / pixels
    time: vec4f,       // x = seconds
    a: vec4f,          // xy = ship centre in pixels, z = radius, w = strength
    b: vec4f,          // x = the shimmer's clock, in game seconds
    c: vec4f,          // the gate's swirl: xy = centre in pixels, z = radius, w = turn in radians at the centre
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let centre = effect.a.xy;
    let radius = max(effect.a.z, 1.0);
    let strength = effect.a.w;

    let pixel = in.uv * effect.resolution.xy;
    let toPixel = pixel - centre;
    let distance = length(toPixel);

    var uv = in.uv;

    if (strength > 0.0 && distance < radius) {
        // 1 at the centre, 0 at the rim, squared so the edge of the field is
        // soft and the distortion does not end on a visible circle.
        let t = 1.0 - distance / radius;
        let falloff = t * t;

        // Away from the centre, like light bending through a lens. The guard
        // matters: at distance 0 the direction is undefined, and normalize of
        // a zero vector is NaN -- the same trap camera::follow had.
        var direction = vec2f(1.0, 0.0);
        if (distance > 0.0001) {
            direction = toPixel / distance;
        }

        // A travelling ripple, so the field shimmers instead of sitting still.
        // On the game's clock, not `time.x` (wall time), so a pause or a
        // hit-stop holds it still with everything else.
        let ripple = sin(distance * 0.09 - effect.b.x * 5.0);

        let offsetPixels = direction * falloff * strength * (10.0 + 8.0 * ripple);
        uv = uv + offsetPixels * effect.resolution.zw;
    }

    // The gate's swirl (gameplay roadmap L5). Not a push but a turn: each
    // pixel samples from the same distance round the centre, rotated by an
    // angle that is largest at the centre and nothing at the rim -- so the
    // stars behind the gate wind into it and the outside is left alone. A
    // slow wave through the angle keeps it turning even when nothing moves
    // behind it. Added onto the cloak's offset, so the two fields compose if
    // the cloaked ship sits over the gate.
    let swirlTurn = effect.c.w;
    if (swirlTurn != 0.0) {
        let toSwirl = pixel - effect.c.xy;
        let swirlDistance = length(toSwirl);
        let swirlRadius = max(effect.c.z, 1.0);
        if (swirlDistance < swirlRadius) {
            let t = 1.0 - swirlDistance / swirlRadius;
            let wave = 1.0 + 0.15 * sin(effect.b.x * 2.0 - swirlDistance * 0.02);
            let angle = swirlTurn * t * t * wave;
            let c = cos(angle);
            let s = sin(angle);
            let turned = vec2f(toSwirl.x * c - toSwirl.y * s, toSwirl.x * s + toSwirl.y * c);
            uv = uv + (turned - toSwirl) * effect.resolution.zw;
        }
    }

    // Clamped, because an offset near the screen edge would otherwise sample
    // outside the target. The sampler clamps too, but doing it here keeps the
    // intent visible.
    let sampled = textureSample(spriteTexture, spriteSampler,
        clamp(uv, vec2f(0.0), vec2f(1.0)));

    // The world target is opaque, so the alpha it carries is not interesting;
    // this composites over the surface as a finished picture.
    return vec4f(sampled.rgb, 1.0);
}
