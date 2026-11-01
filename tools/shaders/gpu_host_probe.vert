#version 450
layout(set=1,binding=0) uniform Position { vec4 parameters; };
layout(location=0) out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, parameters.x, 1.0);
}
