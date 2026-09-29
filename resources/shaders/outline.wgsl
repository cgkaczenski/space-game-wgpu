// A sprite drawn as its own outline: a bright line round its shape and a faint
// fill inside. The player hidden in an asteroid field (gameplay roadmap A1b),
// drawn over the rocks so you can still see where you are -- and seeing it is
// how you know no enemy can.
//
// A fragment stage only; the vertex stage is the sprite shader's. See the
// contract in include/render/wgpu2d.h.
//
// How it finds the edge: a texel of the sprite is on its edge if it is solid
// and some texel `width` away is not. So each fragment samples its own alpha
// and eight neighbours round it, in the sprite's own texels -- the texture's
// size from textureDimensions, not the screen's -- and keeps the smallest.
// Solid here and clear nearby is the line. Everything is inside the sprite's
// own rectangle, so a cell of an atlas cannot bleed into its neighbour past
// the gutter the atlas loader leaves.

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
    a: vec4f, // rgb = line colour, w = the fill's strength inside the line
    b: vec4f, // x = line width in the sprite's texels, y = at least this many screen pixels
    c: vec4f, // rgb = the fill's colour: a flat silhouette, never the sprite's own colours
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

fn alphaAt(uv: vec2f) -> f32 {
    return textureSampleLevel(spriteTexture, spriteSampler, uv, 0.0).a;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // How far to look for clear space. In the sprite's own texels -- but a
    // ship shrunk on screen packs several texels into one pixel, and a line
    // a texel wide would then be thinner than a pixel and come out in dashes.
    // fwidth(uv) is how much uv one screen pixel spans (a screen-space
    // derivative, taken from the neighbouring fragments), so the step is
    // never less than `b.y` pixels whatever the zoom. Derivatives need
    // uniform control flow, which is why this comes first.
    let texel = 1.0 / vec2f(textureDimensions(spriteTexture, 0));
    let perPixel = fwidth(in.uv);
    let step = max(texel * max(effect.b.x, 0.5), perPixel * max(effect.b.y, 0.5));
    let here = textureSampleLevel(spriteTexture, spriteSampler, in.uv, 0.0);

    var lowest = 1.0;
    for (var y = -1; y <= 1; y = y + 1) {
        for (var x = -1; x <= 1; x = x + 1) {
            if (x == 0 && y == 0) { continue; }
            lowest = min(lowest, alphaAt(in.uv + vec2f(f32(x), f32(y)) * step));
        }
    }

    // Solid, with clear space within reach: the edge. The smoothsteps keep the
    // line from stair-stepping where the sprite's alpha is soft.
    let solid = smoothstep(0.3, 0.6, here.a);
    let edge = solid * (1.0 - smoothstep(0.3, 0.6, lowest));
    let fill = solid * effect.a.w;

    // The fill is a flat colour, not the sprite. Drawn over whatever covers
    // the ship, the sprite's own colours would tint that cover and the ship
    // would read as blended into it rather than behind it.
    let alpha = max(edge, fill) * in.color.a;
    let colour = mix(effect.c.rgb, effect.a.rgb, edge / max(max(edge, fill), 0.0001));
    // Alpha blending, straight (not premultiplied) like every sprite.
    return vec4f(colour, alpha);
}
