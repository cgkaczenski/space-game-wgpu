// Milestone 3: a colored quad from a vertex buffer.
//
// Each vertex arrives as the attributes the pipeline's vertex layout maps to
// these locations: location 0 is the position (Float32x2), location 1 the
// color (Float32x4). Positions are still clip space: x and y in [-1, 1],
// y up, origin at the center. Milestone 5 adds the camera matrix that maps
// gl2d's y-down pixel world into this space.

struct VertexInput {
    @location(0) position: vec2f,
    @location(1) color: vec4f,
};

// Anything tagged @location in the output is interpolated across the
// triangle before the fragment shader sees it, so three corner colors
// become a gradient with no extra code.
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = vec4f(in.position, 0.0, 1.0);
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return in.color;
}
