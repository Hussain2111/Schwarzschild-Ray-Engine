#version 330 core

// Fullscreen triangle generated from gl_VertexID -- no vertex buffer needed.
// One triangle rather than two avoids the diagonal seam where the quad's two
// triangles meet, which shows up as a visible artifact in derivative-based
// effects.
out vec2 vUV;

void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
