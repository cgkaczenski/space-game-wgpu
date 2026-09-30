// A lit rock (gameplay roadmap A3, A5).
//
// A fragment stage only; the vertex stage is the sprite shader's. See the
// contract in include/render/wgpu2d.h.
//
// **One texture, four kinds of data** (resources/asteroid/rock_packed.png):
//   r  brightness -- the rock's colour, less its hue; `a.rgb` puts the hue back
//   g  the surface normal's x, stored as n * 0.5 + 0.5
//   b  its y, the same way, in OpenGL's convention: +y is *up* the image
//   a  height, 0 the deepest cracks, 1 the highest ridges
// The normal's z is not stored: a normal is a unit vector, so z is
// sqrt(1 - x² - y²).
//
// **The rock is a height field** (A5): a dome for its overall shape, with the
// texture's bumps on top. The dome is what gives a rock a lit side and a dark
// side; the bumps are the detail; and marching across the height map toward
// the light is what lets the ridges shadow the cracks.
//
// **Per-rock data rides on the vertices**, so every rock is one draw:
//   color.rg  where this point is on the rock, in the rock's own frame, as a
//             direction scaled so the outline's corners are length 1 -- which
//             way is "outward" here
//   color.b   how far from the centre to the edge, 0 .. 1: exact across a fan,
//             since each fan triangle runs from the centre to one edge. -1 for
//             a shard cut into triangles with no centre: there `rg`'s length
//             stands in, and the edge is not smoothed.
//   color.a   heat from the beam, 0 .. 1, already faded with distance from
//             where it burns -- or, when negative, no heat and a shade: a
//             shard sunk into the background, or foreground debris, is drawn
//             at -a of a rock's light. Nothing is both hot and shaded.
//
// **The light is turned into each rock's frame here**, not on the CPU. The
// texture is laid on each rock at one fixed scale and turned with it, so the
// texture coordinate's rate of change across the screen (dpdx, dpdy) is the
// rock's rotation, scaled: a world direction pushed through it comes out in
// the rock's frame. That is what freed `rg` for the position.

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
    a: vec4f, // rgb = tint (the rock's average colour over its brightness, times
              //       brightness and the light's strength), w = ambient, as a
              //       share of the light's strength
    b: vec4f, // xy = toward the light across the screen (the world's frame),
              // z = the light's height (sin of its elevation),
              // w = the large-scale layer's strength
    c: vec4f, // rgb = heat's colour, w = how far into the surface full heat reaches
    d: vec4f, // x = coarsening 0 .. 1, y = how round the edges are,
              // z = self-shadow strength, w = shine (specular)
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

// The height map's full range, in texture units: 0.03 of a repeat, about 54
// world units at the default texture scale.
const bumpDepth = 0.03;
// How far toward the light the shadow march looks, in texture units, and in
// how many steps.
const shadowReach = 0.05;
const shadowSteps = 12;
// The large-scale layer: the same texture again at five times the size, so a
// big rock has big features and does not read as wallpaper.
const macroScale = 0.2;

fn unpackNormal(t: vec4f) -> vec3f {
    // Stored GL-style, +y up the image; the rock's frame is y-down like the
    // world (its texture coordinate v runs down), so y flips. Mip levels
    // average normals, which shortens them, so renormalize.
    var n = vec3f(t.g * 2.0 - 1.0, -(t.b * 2.0 - 1.0), 0.0);
    n.z = sqrt(max(1.0 - dot(n.xy, n.xy), 0.0));
    return normalize(n);
}

// Laying one normal map over another ("whiteout" blending): add the tilts,
// multiply the ups. A bump on a slope tilts the slope further, which simply
// adding normals and renormalizing flattens.
fn blendNormals(base: vec3f, detail: vec3f) -> vec3f {
    return normalize(vec3f(base.xy + detail.xy, base.z * detail.z));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Everything that takes a derivative, first: derivatives are only defined
    // in uniform control flow, before any branch.
    let ddx = dpdx(in.uv);
    let ddy = dpdy(in.uv);
    let edgeWidth = fwidth(in.color.b);

    // Coarsening: snap the texture coordinate to a coarser grid of texels, so
    // the photograph reads closer to the pixel-art ships. The snap is a step
    // function -- its derivative is zero inside a step -- which would make the
    // hardware pick mip level 0 everywhere and shimmer when zoomed out. So the
    // mip level comes from the unsnapped coordinate's derivatives
    // (textureSampleGrad).
    let coarsen = clamp(effect.d.x, 0.0, 1.0);
    let texels = vec2f(textureDimensions(spriteTexture, 0)) * mix(1.0, 0.2, coarsen);
    let snapped = (floor(in.uv * texels) + 0.5) / texels;
    let uv = select(in.uv, snapped, coarsen > 0.001);
    let data = textureSampleGrad(spriteTexture, spriteSampler, uv, ddx, ddy);
    let macroData = textureSampleGrad(spriteTexture, spriteSampler, uv * macroScale,
        ddx * macroScale, ddy * macroScale);

    // What the vertices carry.
    let q = in.color.rg;
    let clipped = in.color.b < 0.0;
    let t = clamp(select(in.color.b, length(q), clipped), 0.0, 1.0);
    let heat = max(in.color.a, 0.0);
    let shade = select(1.0, -in.color.a, in.color.a < 0.0);

    // The light, in the rock's frame (see the top of the file), and in 3D.
    let lightFlat = normalize(ddx * effect.b.x + ddy * effect.b.y + vec2f(1e-9, 0.0));
    let lightHeight = clamp(effect.b.z, 0.05, 1.0);
    let lightSide = sqrt(1.0 - lightHeight * lightHeight);
    let light = vec3f(lightFlat * lightSide, lightHeight);

    // The shape. The dome: flat in the middle, turning away toward the edge
    // (its slope grows with t²), outward along `q`. Then the large-scale
    // layer, then the fine detail, each laid over the last.
    let outward = q / max(length(q), 1e-4);
    let dome = normalize(vec3f(outward * (effect.d.y * t * t), 1.0));
    let macroNormal = unpackNormal(macroData);
    let macroAmount = clamp(effect.b.w, 0.0, 1.0);
    let broad = normalize(vec3f(macroNormal.xy * macroAmount, 1.0));
    let n = blendNormals(blendNormals(dome, broad), unpackNormal(data));

    // Lambert: a surface is lit by how squarely it faces the light.
    var diffuse = max(dot(n, light), 0.0);

    // Shadows within the rock: from here, step across the height map toward
    // the light, the ray climbing at the light's elevation. If the surface
    // anywhere along it stands above the ray, something is between this
    // point and the light. How far above decides how dark, so the shadow's
    // edge is soft.
    let rise = lightHeight / max(lightSide, 0.05); // height gained per unit across
    let here = data.a * bumpDepth;
    var blocked = 0.0;
    for (var i = 1; i <= shadowSteps; i++) {
        let s = shadowReach * f32(i) / f32(shadowSteps);
        let ground = textureSampleLevel(spriteTexture, spriteSampler, uv + lightFlat * s, 1.0).a * bumpDepth;
        blocked = max(blocked, (ground - (here + s * rise)) / bumpDepth);
    }
    let lit = 1.0 - clamp(blocked * 4.0, 0.0, 1.0) * clamp(effect.d.z, 0.0, 1.0);
    diffuse *= lit;

    // Coarsened, the light falls in bands too, as pixel art shades.
    let bands = mix(64.0, 5.0, coarsen);
    diffuse = floor(diffuse * bands + 0.5) / bands;

    // The stone's own light and dark, with the large layer's broad patches.
    let tone = data.r * mix(1.0, 0.45 + macroData.r, macroAmount);
    var rgb = effect.a.rgb * tone * (effect.a.w + diffuse) * shade;

    // Shine: Blinn-Phong, the viewer straight above. The highlight is where
    // the surface faces halfway between the light and the eye.
    let halfway = normalize(light + vec3f(0.0, 0.0, 1.0));
    let shine = pow(max(dot(n, halfway), 0.0), 24.0) * effect.d.w * lit * shade;
    rgb += vec3f(0.9, 0.95, 1.0) * shine;

    // Heat: the beam's, faded with distance on the CPU. It glows in the
    // cracks first -- the shield dissolve's threshold, turned to fire.
    //
    // Cracks are *local* lows. A coarse mip level is the local average height
    // (each mip is an average of four below it), so the average minus the
    // height here is what stands out from the neighbourhood: a high-pass
    // filter from two texture reads. As heat rises the bar falls, shallower
    // cracks join in, and the ridges never do.
    let neighbourhood = textureSampleLevel(spriteTexture, spriteSampler, uv, 4.0).a;
    let crack = (neighbourhood - data.a) * 6.0;
    let bar = 1.0 - heat * clamp(effect.c.w, 0.0, 1.0);
    let glow = clamp((crack - bar + 0.6) / 0.25, 0.0, 1.0) * heat;
    // Orange at the edges of a glow, yellow where it runs hottest; never white.
    let hot = mix(effect.c.rgb, vec3f(1.0, 0.8, 0.35), glow);
    rgb = mix(rgb, hot * 1.3, glow);

    // The silhouette, smoothed: the last pixel inside the edge fades out, by
    // how far t still has to go to reach 1, measured in pixels (fwidth is t's
    // change across one pixel). The polygon's own edge is a hard stair-step.
    let coverage = select(clamp((1.0 - in.color.b) / max(edgeWidth, 1e-6), 0.0, 1.0), 1.0,
        clipped || edgeWidth < 1e-6);
    return vec4f(rgb, coverage);
}
