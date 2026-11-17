// Present pass: upscale the raymarched image to the window and draw the
// slice-orientation indicator at full resolution. Host prepends #version.

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uScene;
uniform vec2 uUVScale;  // part of the target the scene filled
uniform mat4 uCameraBasis;
uniform float uDim;    // darken behind menus (indicator hidden too)
uniform vec4 uGizmo;   // centre x, y (pixels, origin bottom-left), size, slice offset / max

// ---------------------------------------------------------------- overlay

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0));
}

// Slice-orientation indicator: each world axis drawn as seen in the slice
// (x red, y green, z blue, w gold). An axis lying in the slice is a full
// line; one tilted out of it shrinks and its tip swells into a ring, so the
// gold w axis is a ring at the centre in the default slice.
vec4 gizmo(vec2 px) {
    vec2 q = px - uGizmo.xy;
    float size = uGizmo.z;
    if (max(abs(q.x), abs(q.y)) > size * 1.6) return vec4(0.0);
    vec4 acc = vec4(0.0);
    float disc = length(q) - size * 1.25;
    acc = vec4(0.08, 0.06, 0.10, 0.45 * clamp(0.5 - disc, 0.0, 1.0));
    vec3 cols[4];
    cols[0] = vec3(0.95, 0.35, 0.30);
    cols[1] = vec3(0.45, 0.90, 0.40);
    cols[2] = vec3(0.40, 0.60, 1.00);
    cols[3] = vec3(1.00, 0.80, 0.25);
    for (int k = 0; k < 4; ++k) {
        // Row k of the basis: world axis k expressed in camera coordinates.
        vec4 a = vec4(uCameraBasis[0][k], uCameraBasis[1][k], uCameraBasis[2][k], uCameraBasis[3][k]);
        vec2 tip = a.xy * size;
        float out4 = abs(a.w);
        float line = segDist(q, vec2(0.0), tip) - 1.5;
        float ring = abs(length(q - tip) - (2.0 + 6.0 * out4)) - 1.2;
        float d = min(line, out4 > 0.05 ? ring : 1e3);
        float cov = clamp(0.5 - d, 0.0, 1.0);
        acc.rgb = mix(acc.rgb, cols[k], cov);
        acc.a = max(acc.a, cov);
    }
    // Offset along the slice normal: a short gold bar to the right.
    vec2 bq = q - vec2(size * 1.55, 0.0);
    float bar = max(abs(bq.x) - 1.0, abs(bq.y) - size);
    float mark = length(bq - vec2(0.0, uGizmo.w * size)) - 3.5;
    float cb = clamp(0.5 - bar, 0.0, 1.0) * 0.5;
    float cm = clamp(0.5 - mark, 0.0, 1.0);
    acc.rgb = mix(acc.rgb, vec3(0.8), cb);
    acc.rgb = mix(acc.rgb, cols[3], cm);
    acc.a = max(acc.a, max(cb, cm));
    return acc;
}

void main() {
    vec3 col = texture(uScene, vUV * uUVScale).rgb * (1.0 - uDim);
    vec4 g = uDim > 0.0 ? vec4(0.0) : gizmo(gl_FragCoord.xy);
    fragColor = vec4(mix(col, g.rgb, g.a), 1.0);
}
