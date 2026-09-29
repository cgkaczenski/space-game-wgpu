// A lit rock (gameplay roadmap A3).
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
// **Per-rock data rides on the vertices**, so every rock is one draw:
//   color.rg  the direction to the light, in *this rock's* frame, as x * 0.5 + 0.5.
//             The texture turns with the rock and the light does not, so
//             the CPU turns the light into the rock's frame, per rock.
//   color.b   the rim: 1 at the fan's centre, less at its edge
//   color.a   heat from the beam, 0 .. 1, already faded with distance from
//             where it burns. Rocks are opaque, so alpha is free to carry it.

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
    a: vec4f, // rgb = tint (the rock's average colour over its brightness), w = ambient light
    b: vec4f, // x = the light's horizontal share (cos elevation), y = its height (sin elevation),
              // z = direct light's strength, w = coarsening 0 .. 1
    c: vec4f, // rgb = heat's colour, w = how far into the surface full heat reaches
};
@group(2) @binding(0) var<uniform> effect: EffectUniforms;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Coarsening: snap the texture coordinate to a coarser grid of texels, so
    // the photograph reads closer to the pixel-art ships. The snap is a step
    // function -- its derivative is zero inside a step -- which would make the
    // hardware pick mip level 0 everywhere and shimmer when zoomed out. So the
    // mip level comes from the unsnapped coordinate's derivatives
    // (textureSampleGrad), taken here, before any branch, as derivatives must be.
    let coarsen = clamp(effect.b.w, 0.0, 1.0);
    let ddx = dpdx(in.uv);
    let ddy = dpdy(in.uv);
    let texels = vec2f(textureDimensions(spriteTexture, 0)) * mix(1.0, 0.2, coarsen);
    let snapped = (floor(in.uv * texels) + 0.5) / texels;
    let uv = select(in.uv, snapped, coarsen > 0.001);
    let data = textureSampleGrad(spriteTexture, spriteSampler, uv, ddx, ddy);

    // The normal. Stored GL-style, +y up the image; the rock's frame is
    // y-down like the world (its texture coordinate v runs down), so y flips.
    // Mip levels average normals, which shortens them, so renormalize.
    var n = vec3f(data.g * 2.0 - 1.0, -(data.b * 2.0 - 1.0), 0.0);
    n.z = sqrt(max(1.0 - dot(n.xy, n.xy), 0.0));
    n = normalize(n);

    // The light, in the rock's frame: horizontal from the vertex colour, its
    // height the same for everything.
    let lightFlat = normalize(in.color.rg * 2.0 - 1.0 + vec2f(0.00001, 0.0));
    let light = normalize(vec3f(lightFlat * effect.b.x, effect.b.y));

    // Lambert: a surface is lit by how squarely it faces the light.
    var diffuse = max(dot(n, light), 0.0);
    // Coarsened, the light falls in bands too, as pixel art shades.
    let bands = mix(64.0, 5.0, coarsen);
    diffuse = floor(diffuse * bands + 0.5) / bands;

    let rim = in.color.b;
    var rgb = effect.a.rgb * data.r * (effect.a.w + effect.b.z * diffuse) * rim;

    // Heat: the beam's, faded with distance on the CPU. It glows in the
    // cracks first -- the shield dissolve's threshold, turned to fire.
    //
    // Cracks are *local* lows. This height map is mostly broad hills and
    // hollows, so "lower than some height" picked whole hollows and the burn
    // came out a flat white blob. A coarse mip level is the local average
    // height (each mip is an average of four below it), so the average minus
    // the height here is what stands out from the neighbourhood: a high-pass
    // filter from two texture reads. Positive in a crack, negative on a
    // ridge. As heat rises the bar falls, shallower cracks join in, and the
    // ridges never do.
    let heat = in.color.a;
    let neighbourhood = textureSampleLevel(spriteTexture, spriteSampler, uv, 4.0).a;
    let crack = (neighbourhood - data.a) * 6.0;
    let bar = 1.0 - heat * clamp(effect.c.w, 0.0, 1.0);
    let glow = clamp((crack - bar + 0.6) / 0.25, 0.0, 1.0) * heat;
    // Orange at the edges of a glow, yellow where it runs hottest; never white.
    let hot = mix(effect.c.rgb, vec3f(1.0, 0.8, 0.35), glow);
    rgb = mix(rgb, hot * 1.3, glow);

    // Opaque: the vertex alpha was heat, not coverage.
    return vec4f(rgb, 1.0);
}
