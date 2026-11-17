// The 4D world. Owned by the world student; this is the stub the renderer
// ships with. Plain GLSL ES 3.00: no #version, no uniforms of its own.
//
// Contract (see docs/4D.md):
//   vec2 worldSDF(vec4 p)                    x = signed distance, y = material id
//   vec3 skyColor(vec4 dir)                  radiance seen along a 4D direction
//   vec3 materialColor(float id, vec4 p, vec4 n)  albedo of a world material
// Available from the renderer: uTime, uSunDir (unit vec4, towards the sun).
// Material ids: 1 ground, 2 bowl, 10+ free for world props.

float sdGlome(vec4 p, float r) { return length(p) - r; }

// Lower half of a 4D spherical shell: the bowl the fruit sit in.
float sdBowl(vec4 p, float r, float thickness) {
    float shell = abs(length(p) - r) - thickness;
    return max(shell, p.y); // keep y <= 0
}

vec2 worldSDF(vec4 p) {
    vec2 res = vec2(p.y + 3.2, 1.0);                       // 4D ground hyperplane
    float bowl = sdBowl(p, 3.0, 0.1);
    if (bowl < res.x) res = vec2(bowl, 2.0);
    float g = sdGlome(p - vec4(-5.5, -1.2, -2.0, 1.0), 2.0); // a 4D boulder
    if (g < res.x) res = vec2(g, 10.0);
    return res;
}

vec3 skyColor(vec4 dir) {
    float h = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 col = mix(vec3(0.95, 0.45, 0.25), vec3(0.32, 0.22, 0.42), smoothstep(0.45, 0.9, h));
    col = mix(vec3(0.20, 0.16, 0.28), col, smoothstep(0.2, 0.5, h));
    float s = max(dot(dir, uSunDir), 0.0);
    col += vec3(1.0, 0.65, 0.3) * (pow(s, 8.0) * 0.35 + pow(s, 200.0) * 2.0);
    return col;
}

vec3 materialColor(float id, vec4 p, vec4 n) {
    if (id < 1.5) {
        float checker = mod(floor(p.x) + floor(p.z) + floor(p.w), 2.0);
        return mix(vec3(0.30, 0.45, 0.20), vec3(0.36, 0.52, 0.24), checker);
    }
    if (id < 2.5) return vec3(0.90, 0.85, 0.75);
    return vec3(0.45, 0.42, 0.44);
}
