#version 330 core
// Antialiased UI shapes as signed distances (in points): rounded rects
// (filled or a ring), capsules, arc rings and rounded triangles, with a
// vertical gradient. A wide feather turns any of them into a soft shadow.
in vec2 vPos;
out vec4 color;

uniform int uMode;        // 0 rect, 1 capsule, 2 arc, 3 triangle
uniform vec2 uA, uB, uC;
uniform float uRadius, uBorder, uFeather, uAlpha;
uniform vec2 uArc;        // start angle, sweep
uniform vec3 uTop, uBottom;
uniform vec2 uSpan;       // gradient: top y, height

float sdRoundRect(vec2 p, vec2 c, vec2 hs, float r) {
    vec2 q = abs(p - c) - hs + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float sdCapsule(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float sdTriangle(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                     vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

void main()
{
    vec2 p = vPos;
    float d;
    if (uMode == 0) {
        d = sdRoundRect(p, uA, uB, uRadius);
        if (uBorder > 0.0) d = abs(d + uBorder * 0.5) - uBorder * 0.5;
    } else if (uMode == 1) {
        d = sdCapsule(p, uA, uB, uRadius);
    } else if (uMode == 2) {
        vec2 q = p - uA;
        d = abs(length(q) - uRadius) - uBorder * 0.5;
        // Angular mask: distance to the arc's end caps outside the sweep.
        float ang = atan(q.y, q.x) - uArc.x;
        ang = mod(ang, 6.2831853);
        if (uArc.y < 6.28 && ang > uArc.y) {
            vec2 e0 = uA + uRadius * vec2(cos(uArc.x), sin(uArc.x));
            vec2 e1 = uA + uRadius * vec2(cos(uArc.x + uArc.y), sin(uArc.x + uArc.y));
            d = min(length(p - e0), length(p - e1)) - uBorder * 0.5;
        }
    } else {
        d = sdTriangle(p, uA, uB, uC) - uRadius;
    }
    float a = 1.0 - smoothstep(-uFeather * 0.5, uFeather * 0.5, d);
    float t = clamp((p.y - uSpan.x) / uSpan.y, 0.0, 1.0);
    color = vec4(mix(uTop, uBottom, t), a * uAlpha);
}
