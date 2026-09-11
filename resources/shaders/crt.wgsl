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
    b: vec4f,          // x = scanline period in pixels, y = vignette, z = fringing
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

// The glass. Pushes the coordinate outward by the square of its distance from
// the centre, which is a barrel distortion: nothing moves in the middle and
// the corners move most. `amount` 0 is a flat panel.
fn bend(uv: vec2f, amount: f32) -> vec2f
{
    let centred = uv * 2.0 - 1.0;
    let r2 = dot(centred, centred);
    return (centred * (1.0 + amount * r2)) * 0.5 + 0.5;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // One master knob scales the lot, so the slider runs from a flat screen to
    // a strong effect without the parts drifting out of proportion.
    let master = clamp(effect.a.x, 0.0, 2.0);

    let curvature = effect.a.y * master;
    let scanlines = effect.a.z * master;
    let mask = effect.a.w * master;
    let period = effect.b.x;
    let vignette = effect.b.y * master;
    let fringing = effect.b.z * master;

    // TEMPORARY: b.w = 1 replaces the bend with a flat 2-pixel shift, to tell
    // "the bend is wrong" apart from "any off-centre sample is wrong". Remove.
    let uv = bend(in.uv, curvature);

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
            bend(in.uv, curvature + scale), 0.0).r;
        let blue = textureSampleLevel(spriteTexture, spriteSampler,
            bend(in.uv, curvature - scale), 0.0).b;
        colour = vec3f(red, colour.g, blue);
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

    // The corners of the tube are further from the gun and dimmer for it.
    if (vignette > 0.0) {
        let centred = uv * 2.0 - 1.0;
        colour = colour * (1.0 - vignette * dot(centred, centred) * 0.5);
    }

    // TEMPORARY: b.w = 1 skips the outside-the-tube mask, so a black region
    // can be attributed to the sample or to the mask. Remove.
    // TEMPORARY: identical control flow, so the two runs differ only in what
    // is shown, not in what is computed. Remove.
    return vec4f(colour * inside, 1.0);
}
