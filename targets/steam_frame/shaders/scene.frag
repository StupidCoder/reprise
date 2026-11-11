#version 450
layout(location=0) in vec3 colour;
layout(location=1) in vec2 uv;
layout(location=2) flat in float material;
layout(set=0,binding=0) uniform sampler2DArray surfaces;
layout(location=0) out vec4 outColour;
void main() { outColour=vec4(colour*texture(surfaces,vec3(uv,material)).rgb,1); }
