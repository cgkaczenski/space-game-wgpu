// A CRT filter over the finished frame: curvature, scanlines, an aperture
// mask, colour fringing and a vignette.
//
// A fragment stage only; the vertex stage is the sprite shader's. See the
// contract in include/render/wgpu2d.h.
//
// The thing worth noticing is how little of this needs neighbouring pixels.
// Five of the six parts below are arithmetic on *one* sample at a coordinate
// this shader chooses. Only the phosphor glow -- the bloom where bright things
// bleed into what is next to them, not written here -- is a convolution, and
// it is the last part to add rather than the first.

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
    a: vec4f,          // x = master, y = curvature, z = scanlines, w = mask
    b: vec4f,          // x = scanline period in pixels, y = vignette, z = fringing, w = warmth
    c: vec4f,          // x = switch-off 0..1, y = white-out 0..1 (gameplay roadmap L1)
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

// Where warm highlights head. Kept in step with `warmColour` in crt.cpp, which
// tints the glow toward the same colour; only the amount is a parameter.
const warmTint = vec3f(1.0, 0.80, 0.58);
const lumaWeights = vec3f(0.2126, 0.7152, 0.0722);

// Where the highlight shoulder begins. Below it colour is untouched; above it
// values ease toward 1 instead of passing it, so the very brightest art keeps
// some detail after the brightness restore rather than clipping into a flat
// sheet.
//
// High on purpose. At 0.75 it also flattened the glow: a halo is added
// brightness, and a curve that compresses everything above 0.75 compresses
// exactly what the halo added. It is a limit on burn-out, not a tone curve.
const shoulderStart = 0.9;

// A soft ceiling. Continuous with the identity at `shoulderStart` in value and
// slope, so there is no visible band where it takes over, and it approaches 1
// without reaching it.
fn shoulder(c: vec3f) -> vec3f
{
    let k = shoulderStart;
    let over = max(c - vec3f(k), vec3f(0.0));
    return min(c, vec3f(k)) + (1.0 - k) * (vec3f(1.0) - exp(-over / (1.0 - k)));
}

// The glass. Pushes the coordinate outward by the square of its distance from
// the centre, which is a barrel distortion: nothing moves in the middle and
// the corners move most. `amount` 0 is a flat panel.
fn bend(uv: vec2f, amount: f32) -> vec2f
{
    let centred = uv * 2.0 - 1.0;
    let r2 = dot(centred, centred);
    return (centred * (1.0 + amount * r2)) * 0.5 + 0.5;
}

// The set switching off, as a coordinate: where on the unsquashed picture
// this screen point comes from. First static comes up over the picture (the
// caller does that), then the picture collapses to a horizontal line, the
// line to a dot, and the dot goes out. Squashing is sampling from further
// out, so it is a divide around the centre; a point that would sample past
// the picture's edge falls outside [0, 1] and the tube test blacks it out.
fn squashY(off: f32) -> f32 { return mix(1.0, 0.004, smoothstep(0.25, 0.65, off)); }
fn squashX(off: f32) -> f32 { return mix(1.0, 0.003, smoothstep(0.6, 0.88, off)); }

fn squash(uv: vec2f, off: f32) -> vec2f
{
    if (off <= 0.0) { return uv; }
    let scale = vec2f(squashX(off), squashY(off));
    return (uv - 0.5) / scale + 0.5;
}

fn hash(p: vec2f) -> f32
{
    let q = fract(p * vec2f(123.34, 456.21));
    let r = q + dot(q, q + 45.32);
    return fract(r.x * r.y);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let off = clamp(effect.c.x, 0.0, 1.0);
    let white = clamp(effect.c.y, 0.0, 1.0);
    let screenUv = squash(in.uv, off);

    // One master knob scales the lot, so the slider runs from a flat screen to
    // a strong effect without the parts drifting out of proportion.
    let master = clamp(effect.a.x, 0.0, 2.0);

    let curvature = effect.a.y * master;
    let scanlines = effect.a.z * master;
    let mask = effect.a.w * master;
    let period = effect.b.x;
    let vignette = effect.b.y * master;
    let fringing = effect.b.z * master;
    let warmth = clamp(effect.b.w, 0.0, 1.0);

    let uv = bend(screenUv, curvature);

    // Sampling is textureSampleLevel rather than textureSample because a
    // post-process wants level 0 always, and because textureSample picks its
    // mip level from screen-space derivatives and so has to sit in uniform
    // control flow.
    //
    // This shader spent a long time being blamed for an artifact that was not
    // in it. A quadrant of the screen came out black, and every measurement of
    // this file said it should not: the interpolated coordinate was right, the
    // bent coordinate was right, the sampled colour was right, the mask read 1.
    // The parameters were arriving from another quad's uniform slot, because
    // the composite that draws this shader borrowed the batch without setting
    // all of it aside. Where a measurement contradicts the code in front of
    // you, the wrong thing is usually not in front of you.

    // Colour fringing: the three guns do not land in quite the same place, so
    // red and blue are sampled through slightly different curvatures. Three
    // samples at coordinates chosen here, which is not a convolution -- a
    // kernel would weight a fixed neighbourhood whatever the picture is doing.
    var colour = textureSampleLevel(spriteTexture, spriteSampler, uv, 0.0).rgb;
    if (fringing > 0.0) {
        let scale = fringing * 0.004;
        let red = textureSampleLevel(spriteTexture, spriteSampler,
            bend(screenUv, curvature + scale), 0.0).r;
        let blue = textureSampleLevel(spriteTexture, spriteSampler,
            bend(screenUv, curvature - scale), 0.0).b;
        colour = vec3f(red, colour.g, blue);
    }

    // Warm highlights. Only the bright end moves -- chosen by luminance, so
    // dark colours are left alone -- and it moves toward amber *at the same
    // luminance*: blue comes down, red comes up, and the pixel is as bright as
    // it was. That is what keeps the glow. The glow is already in `colour`
    // here (it was added to the frame before this pass), and the first version
    // multiplied by amber instead, which dimmed exactly the pixels the glow had
    // brightened -- about a third of the halo at a bright edge was lost.
    if (warmth > 0.0) {
        let luma = dot(colour, lumaWeights);
        let highlight = smoothstep(0.45, 0.95, luma);
        let warmed = luma * warmTint / dot(warmTint, lumaWeights);
        colour = mix(colour, warmed, warmth * highlight);
    }

    // Outside the tube, black rather than the clamped edge texel: the sampler
    // would smear the outermost row around the corners, which reads as a
    // stretched image rather than as the end of the glass. A multiply at the
    // end instead of an early return, so nothing above it is conditional.
    let inside = f32(uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0);

    // Scanlines, measured in whole output pixels rather than as a count of
    // lines across the picture.
    //
    // A count was the obvious way to write it and it is why the hull looked
    // translucent. 240 lines across a 500 pixel window is a period of 2.083
    // pixels, so the dark band drifts a twelfth of a pixel every row and beats
    // against the sprite's own pixel grid. What that produces is not scanlines,
    // it is a moving screen door over the artwork, and a screen door over a
    // sprite on a dark background reads as the sprite being see-through.
    //
    // A period in pixels cannot drift. `period` 3 means two bright rows and one
    // dark one, in the same place every row and every frame.
    //
    // Every darkening term below also records what it took, so `gain` can give
    // it back. A mask that removes light on average is a mask that dims the
    // picture, and stacking three of them is why the screen fell away at high
    // settings. A real tube does not get dimmer as its mask gets finer: the
    // bright parts get brighter and the average holds. Restoring the mean is
    // one multiply and it is the difference between a filter and a dimmer.
    var gain = 1.0;

    if (scanlines > 0.0) {
        let row = in.uv.y * effect.resolution.y;
        let phase = fract(row / max(period, 2.0));
        let line = 0.5 - 0.5 * cos(phase * 6.2831853);
        colour = colour * (1.0 - scanlines * (1.0 - line));
        // `line` averages 0.5 over a period, so the mean factor is known
        // exactly rather than estimated.
        gain = gain / (1.0 - scanlines * 0.5);
    }

    // The aperture grille: vertical stripes of red, green and blue phosphor.
    // This one is per output *pixel*, not per uv, because it is a property of
    // the physical screen rather than of the picture on it.
    if (mask > 0.0) {
        let column = i32(floor(in.uv.x * effect.resolution.x)) % 3;
        var tint = vec3f(1.0, 1.0, 1.0);
        if (column == 0) { tint = vec3f(1.0, 1.0 - mask, 1.0 - mask); }
        else if (column == 1) { tint = vec3f(1.0 - mask, 1.0, 1.0 - mask); }
        else { tint = vec3f(1.0 - mask, 1.0 - mask, 1.0); }
        colour = colour * tint;
        // Each channel is full on one column in three and dimmed on the other
        // two, so its mean is 1 - 2*mask/3.
        gain = gain / (1.0 - mask * 2.0 / 3.0);
    }

    // Capped, so a slider at its limit brightens rather than blows out.
    colour = colour * min(gain, 2.0);

    // The restore just lifted the bright rows past 1 wherever the art was
    // already near it -- on the planet, all of its highlights. Ease them in.
    colour = shoulder(colour);

    // The corners of the tube are further from the gun and dimmer for it.
    if (vignette > 0.0) {
        let centred = uv * 2.0 - 1.0;
        colour = colour * (1.0 - vignette * dot(centred, centred) * 0.5);
    }

    // The switch-off's static and glare. Static first, over the whole picture
    // while it is still full size; then, as it squashes, what is left runs
    // white-hot, the way the last line on a tube is the brightest thing on it.
    // Snow is in blocks of two output pixels and changes every 1/30 s, so it
    // reads as noise rather than as a shimmer.
    if (off > 0.0) {
        let cell = floor(in.uv * effect.resolution.xy * 0.5);
        let frame = floor(effect.time.x * 30.0);
        let snow = vec3f(hash(cell + vec2f(frame * 7.13, frame * 3.71)));
        colour = mix(colour, snow, 0.75 * smoothstep(0.0, 0.3, off));
        let glare = smoothstep(0.35, 0.7, off);
        colour = mix(colour, vec3f(1.0), glare);
        // The dot, going out.
        colour = colour * (1.0 - smoothstep(0.88, 1.0, off));
    }

    // White-out: after everything, so it is white, not warm or scanlined white.
    return vec4f(mix(colour * inside, vec3f(1.0), white), 1.0);
}
