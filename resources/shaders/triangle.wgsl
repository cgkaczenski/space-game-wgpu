// Milestone 2: one triangle, no vertex buffer.
//
// The vertex shader runs once per vertex. It receives the vertex's index
// (0, 1, 2 for a single triangle) and must return a clip-space position:
// x and y in [-1, 1] across the whole render target, y up, origin at the
// center. Milestone 5 replaces the hardcoded positions with real vertex
// data and a camera matrix.

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> @builtin(position) vec4f {
    var p = vec2f(0.0, 0.0);
    if (vertex_index == 0u) {
        p = vec2f(-0.5, -0.5);
    } else if (vertex_index == 1u) {
        p = vec2f(0.5, -0.5);
    } else {
        p = vec2f(0.0, 0.5);
    }
    return vec4f(p, 0.0, 1.0);
}

// The fragment shader runs once per pixel the triangle covers and writes to
// color attachment 0, which the render pass bound to the surface view.
@fragment
fn fs_main() -> @location(0) vec4f {
    return vec4f(0.95, 0.6, 0.1, 1.0); // warm orange: unmistakable against the blue clear
}
