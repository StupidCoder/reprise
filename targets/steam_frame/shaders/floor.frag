#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(push_constant) uniform View {
    vec4 rotation;
    vec4 eye; // w is floor height in the reference space
    vec4 tangent; // left, right, down, up
} view;
vec3 rotate(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w*v);
}
void main() {
    vec3 ray = rotate(view.rotation, vec3(mix(view.tangent.x, view.tangent.y, uv.x),
                                          mix(view.tangent.w, view.tangent.z, uv.y), -1.0));
    color = vec4(0,0,0,1);
    if (ray.y >= -0.001) return;
    float t = (view.eye.w-view.eye.y)/ray.y;
    if (t <= 0) return;
    vec2 point = (view.eye.xyz + t*ray).xz;
    float fade = 1.0-smoothstep(2.0, 8.0, length(point));
    vec2 tile = point / 0.55;
    vec2 edge = abs(fract(tile-0.5)-0.5)*0.55;
    float d = min(edge.x, edge.y);
    float aa = max(length(fwidth(point)), 0.001);
    float seam = 1.0-smoothstep(0.003, 0.003+aa, d);
    float glow = exp(-d*65.0);
    // Stationary variation keeps the floor quiet under head movement.
    vec2 cell = floor(tile);
    float variation = fract(sin(dot(cell, vec2(12.9898,78.233)))*43758.5453);
    float light = 0.25+0.75*variation;
    vec3 charcoal = vec3(0.012,0.014,0.017);
    vec3 amber = vec3(0.48,0.17,0.025)*(seam*0.42 + glow*0.10)*light;
    // Suppress subpixel seams near the horizon instead of sparkling.
    amber *= 1.0-smoothstep(0.03,0.18,aa);
    color = vec4((charcoal+amber)*fade, 1);
}
