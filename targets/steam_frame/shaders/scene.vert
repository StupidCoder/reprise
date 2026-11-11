#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 colour;
layout(location=2) in vec2 uv;
layout(location=3) in float material;
layout(location=0) out vec3 outColour;
layout(location=1) out vec2 outUV;
layout(location=2) flat out float outMaterial;
layout(push_constant) uniform View {
    vec4 eye; vec4 right; vec4 up; vec4 forward; vec4 tangent; vec4 planes;
} view;
void main() {
    vec3 offset=position-view.eye.xyz;
    float x=dot(offset,view.right.xyz), y=dot(offset,view.up.xyz), z=dot(offset,view.forward.xyz);
    float l=view.tangent.x, r=view.tangent.y, b=view.tangent.z, t=view.tangent.w;
    float n=view.planes.x, f=view.planes.y;
    gl_Position=vec4((2*x-(r+l)*z)/(r-l),(-2*y+(t+b)*z)/(t-b),(f*z-f*n)/(f-n),z);
    outColour=colour;outUV=uv;outMaterial=material;
}
