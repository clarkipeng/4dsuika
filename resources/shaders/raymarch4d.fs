// 4D raymarcher. The host prepends the #version line, precision, and
// #define MAX_FRUIT, then pastes world4d.glsl in place of its #include.
//
// Each pixel's ray starts at the camera and runs along
// uCameraBasis * (slice-space direction, 0), so it stays inside the 3D
// slice; shading (normals, sun, shadows) uses the full 4D scene.

in vec2 vUV;
out vec4 fragColor;

uniform vec2  uResolution;          // render target size in pixels
uniform vec4  uCameraPos;
uniform mat4  uCameraBasis;         // columns: right, up, back, slice normal
uniform float uTanHalfFov;
uniform float uTime;
uniform vec4  uSunDir;

uniform int   uFruitCount;
uniform vec4  uFruitCenters[MAX_FRUIT];
uniform vec4  uFruitData[MAX_FRUIT]; // radius, type, flash, facet seed
uniform int   uHintCount;            // fruit just off the slice
uniform vec4  uHintCenters[MAX_HINTS]; // centre projected into the slice
uniform vec4  uHintData[MAX_HINTS];  // radius, type, signed gap to the slice, strength 0..1
uniform vec4  uPreviewCenter;        // ghost of the next drop
uniform vec4  uPreviewData;          // radius (0 = none), type
uniform float uFogStart;             // distance where fog begins (just past the orbit target)
uniform float uFacet;                // low-poly facet strength, 0 = smooth

#include "world4d.glsl"
#include "fruit4d.glsl"

// QUALITY 0..3 (Low, Medium, High, Ultra) is defined by the host.
#ifndef QUALITY
#define QUALITY 2
#endif
#if QUALITY == 0
const int MAX_STEPS = 72;  const int SHADOW_STEPS = 16; const int AO_SAMPLES = 2;
#elif QUALITY == 1
const int MAX_STEPS = 96;  const int SHADOW_STEPS = 28; const int AO_SAMPLES = 3;
#elif QUALITY == 2
const int MAX_STEPS = 128; const int SHADOW_STEPS = 40; const int AO_SAMPLES = 4;
#else
const int MAX_STEPS = 160; const int SHADOW_STEPS = 64; const int AO_SAMPLES = 5;
#endif

const float FAR = 80.0;

const float MAT_FRUIT = 3.0;

// ---------------------------------------------------------------- fruit

float glomeHit(vec4 ro, vec4 rd, vec4 c, float r);

// Entry and exit of ray (ro, rd) through the glome |p - c| = r; false if missed.
bool glomeSpan(vec4 ro, vec4 rd, vec4 c, float r, out float t0, out float t1) {
    vec4 oc = ro - c;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - r * r);
    if (h < 0.0) return false;
    h = sqrt(h);
    t0 = max(-b - h, 0.0);
    t1 = -b + h;
    return t1 > 0.0;
}

// Detail level from the fruit's size on screen: 0 below ~6 px radius, 1 from ~24 px.
float fruitLod(float r, float t, float pixel) {
    return smoothstep(6.0, 24.0, r / max(pixel * t, 1e-6));
}

// Sphere-trace a fruit's own SDF between t0 and t1 (its bound glome). At
// lod 0 the fruit is its physics glome, hit analytically.
float fruitMarch(vec4 ro, vec4 rd, vec4 c, vec4 d, float t0, float t1, float lod) {
    if (lod <= 0.0) {
        float th = glomeHit(ro, rd, c, d.x);
        return th > 0.0 ? th : -1.0;
    }
    float t = t0;
    for (int k = 0; k < 24; ++k) {
        float h = fruitSDF(ro + rd * t - c, d.x, d.y, lod);
        if (h < 0.002 * d.x) return t;
        t += h;
        if (t > t1) break;
    }
    return -1.0;
}

vec4 fruitNormal(vec4 q, vec4 d, float lod) {
    float h = 0.002 * d.x;
    const float s = 0.4472136;
    const vec4 k0 = vec4( 1.0,  1.0,  1.0, -s);
    const vec4 k1 = vec4( 1.0, -1.0, -1.0, -s);
    const vec4 k2 = vec4(-1.0,  1.0, -1.0, -s);
    const vec4 k3 = vec4(-1.0, -1.0,  1.0, -s);
    const vec4 k4 = vec4( 0.0,  0.0,  0.0, 4.0 * s);
    return normalize(k0 * fruitSDF(q + h * k0, d.x, d.y, lod) + k1 * fruitSDF(q + h * k1, d.x, d.y, lod) +
                     k2 * fruitSDF(q + h * k2, d.x, d.y, lod) + k3 * fruitSDF(q + h * k3, d.x, d.y, lod) +
                     k4 * fruitSDF(q + h * k4, d.x, d.y, lod));
}

// Nearest positive hit of ray (ro, rd) with the glome |p - c| = r.
float glomeHit(vec4 ro, vec4 rd, vec4 c, float r) {
    vec4 oc = ro - c;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - r * r);
    if (h < 0.0) return -1.0;
    h = sqrt(h);
    float t = -b - h;
    return t > 0.0 ? t : (-b + h > 0.0 ? 0.0 : -1.0);
}

// Soft shadow cast by a glome on a ray (analytic; dot products only, so it
// holds in 4D as in 3D).
float glomeShadow(vec4 ro, vec4 rd, vec4 c, float r) {
    vec4 oc = ro - c;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - r * r);
    float d = sqrt(max(0.0, r * r - h)) - r;
    float t = -b - sqrt(max(h, 0.0));
    return t < 0.0 ? 1.0 : smoothstep(0.0, 1.0, 2.5 * 10.0 * d / t);
}

// Ambient light a glome blocks from a surface point (analytic approximation).
float glomeOcclusion(vec4 p, vec4 n, vec4 c, float r) {
    vec4 di = c - p;
    float l = length(di);
    float h = l / r;
    return max(0.0, dot(n, di / l)) / (h * h);
}

// ---------------------------------------------------------------- facets
//
// Low-poly look by quantizing the 4D normal: scale it, snap it to the
// nearest point of the D4 lattice (integer points with even sum, whose
// Voronoi cells are 24-cells), and renormalize. Facets are fixed to world
// orientation, so they hold still while the camera turns, cost no extra SDF
// evaluations, and a 4D turn reveals new facets.

vec4 d4Round(vec4 x) {
    vec4 f = floor(x + 0.5);
    if (mod(f.x + f.y + f.z + f.w, 2.0) != 0.0) {
        // Odd sum: re-round the coordinate that was rounded the furthest.
        vec4 e = x - f;
        vec4 a = abs(e);
        float m = max(max(a.x, a.y), max(a.z, a.w));
        vec4 pick = step(m, a);
        pick *= vec4(1.0, 1.0 - pick.x, 1.0 - max(pick.x, pick.y), 1.0 - max(max(pick.x, pick.y), pick.z));
        f += pick * sign(e + 1e-6);
    }
    return f;
}

// Quaternion product, vec4 = (x, y, z, w) with w real. Left-multiplying a
// 4D vector by a unit quaternion is a rotation in SO(4).
vec4 qmul(vec4 a, vec4 b) {
    return vec4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}
vec4 qconj(vec4 q) { return vec4(-q.xyz, q.w); }

// density: facets across the surface; higher = smaller facets. A seed turns
// the lattice by its own 4D rotation, so each fruit gets its own facets.
vec4 facetNormal(vec4 n, float density, float seed) {
    vec4 r = normalize(sin(vec4(1.7, 2.3, 3.1, 4.9) * (seed + 1.0) + vec4(0.3, 1.1, 2.2, 0.7)));
    vec4 m = seed > 0.0 ? qmul(r, n) : n;
    vec4 q = d4Round(m * density);
    if (dot(q, q) == 0.0) return n;
    q = normalize(q);
    return seed > 0.0 ? qmul(qconj(r), q) : q;
}

// ---------------------------------------------------------------- scene


// Gradient from the five vertices of a regular 5-cell (4D tetrahedron trick).
vec4 worldNormal(vec4 p) {
    const float h = 0.002;
    const float s = 0.4472136; // 1 / sqrt(5)
    const vec4 k0 = vec4( 1.0,  1.0,  1.0, -s);
    const vec4 k1 = vec4( 1.0, -1.0, -1.0, -s);
    const vec4 k2 = vec4(-1.0,  1.0, -1.0, -s);
    const vec4 k3 = vec4(-1.0, -1.0,  1.0, -s);
    const vec4 k4 = vec4( 0.0,  0.0,  0.0, 4.0 * s);
    return normalize(k0 * worldSDF(p + h * k0).x + k1 * worldSDF(p + h * k1).x +
                     k2 * worldSDF(p + h * k2).x + k3 * worldSDF(p + h * k3).x +
                     k4 * worldSDF(p + h * k4).x);
}

// Shadows and AO may use a cheaper, coarser world distance if the world
// provides one (#define WORLD_SHADOW_SDF and float worldShadowSDF(vec4)).
#ifdef WORLD_SHADOW_SDF
float occluderSDF(vec4 p) { return worldShadowSDF(p); }
#else
float occluderSDF(vec4 p) { return worldSDF(p).x; }
#endif

// Soft shadow towards the sun: the world is marched in 4D, fruit are analytic.
float softShadow(vec4 ro, vec4 rd) {
    float res = 1.0;
    for (int i = 0; i < MAX_FRUIT; ++i) {
        if (i >= uFruitCount) break;
        res = min(res, glomeShadow(ro, rd, uFruitCenters[i], uFruitData[i].x));
    }
    float t = 0.05;
    for (int i = 0; i < SHADOW_STEPS; ++i) {
        if (res < 0.01) break;
        float h = occluderSDF(ro + rd * t);
        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.03, 2.5);
        if (res < 0.01 || t > 25.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

float ambientOcclusion(vec4 p, vec4 n) {
    float occ = 0.0, w = 1.0;
    for (int i = 1; i <= AO_SAMPLES; ++i) {
        float h = 0.3 * float(i * i) / float(AO_SAMPLES * AO_SAMPLES) * 5.0;
        occ += (h - occluderSDF(p + n * h)) * w;
        w *= 0.6;
    }
    float ao = clamp(1.0 - 1.4 * occ, 0.0, 1.0);
    for (int i = 0; i < MAX_FRUIT; ++i) {
        if (i >= uFruitCount) break;
        ao *= 1.0 - clamp(glomeOcclusion(p, n, uFruitCenters[i], uFruitData[i].x), 0.0, 1.0);
    }
    return ao;
}

// Normal used for lighting: smooth normal ns bent towards its facet normal.
vec4 facetBlend(vec4 ns, float density, float seed) {
    return uFacet > 0.0 ? normalize(mix(ns, facetNormal(ns, density, seed), uFacet)) : ns;
}

const vec3 SUN_COLOR = vec3(2.1, 1.25, 0.62);
const vec3 SKY_AMBIENT = vec3(0.32, 0.28, 0.40);
const vec3 GROUND_BOUNCE = vec3(0.40, 0.24, 0.12);

// n: lighting (faceted) normal; ns: smooth normal for shadow/AO offsets.
vec3 shade(vec4 p, vec4 n, vec4 ns, vec4 rd, vec3 albedo, float gloss) {
    float sunDot = max(dot(n, uSunDir), 0.0);
    float sh = dot(ns, uSunDir) > -0.1 ? softShadow(p + ns * 0.02, uSunDir) : 0.0;
    float ao = ambientOcclusion(p, ns);
    vec3 sun = SUN_COLOR * sunDot * sh;
    vec3 sky = SKY_AMBIENT * (0.55 + 0.45 * n.y) * (0.3 + 0.7 * ao);
    vec3 bounce = GROUND_BOUNCE * clamp(-n.y, 0.0, 1.0) * ao;
    // Warm wrap-around light from the low sun behind the scene.
    float back = pow(clamp(dot(-rd, -uSunDir) * 0.5 + 0.5, 0.0, 1.0), 2.0);
    float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    vec3 col = albedo * (sun + sky + bounce);
    col += rim * back * 0.6 * SUN_COLOR * ao;
    vec4 hv = normalize(uSunDir - rd);
    col += gloss * pow(max(dot(n, hv), 0.0), 40.0) * SUN_COLOR * sh;
    return col;
}

// ---------------------------------------------------------------- hints
//
// Fruit just off the slice show as a faint ring where they would appear,
// in their own colour, tinted cool on the +normal side and warm on the -
// side, fading with distance from the slice. Rings and a thin fill read as
// hints, never as solid fruit; behind scene geometry they are fainter.

vec3 drawHints(vec3 col, vec4 ro, vec4 rd, float sceneDist) {
    float pixel = 2.0 * uTanHalfFov / uResolution.y;
    for (int i = 0; i < MAX_HINTS; ++i) {
        if (i >= uHintCount) break;
        vec4 c = uHintCenters[i];
        vec4 d = uHintData[i];
        float r = d.x;
        float tc = dot(c - ro, rd);
        if (tc <= 0.0) continue;
        float miss = length(c - ro - rd * tc);
        float width = max(1.5 * pixel * tc, 0.02 * r);
        float ring = exp(-pow((miss - r) / width, 2.0));
        float fill = 0.18 * (1.0 - smoothstep(r - width, r, miss));
        float occluded = tc - r > sceneDist ? 0.35 : 1.0;
        float a = d.w * occluded * (0.95 * ring + fill);
        vec3 side = d.z > 0.0 ? vec3(0.45, 0.75, 1.0) : vec3(1.0, 0.55, 0.35);
        col = mix(col, mix(fruitBaseColor(d.y), side, 0.35) * 1.3, clamp(a, 0.0, 0.8));
    }
    return col;
}

// ---------------------------------------------------------------- main

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    vec3 local = normalize(vec3(ndc.x * aspect * uTanHalfFov, ndc.y * uTanHalfFov, -1.0));
    vec4 ro = uCameraPos;
    vec4 rd = uCameraBasis * vec4(local, 0.0);

    // Fruit (only those crossing the slice are sent): the ray enters each
    // fruit's bound glome analytically, then traces the fruit's own SDF.
    float pixel = 2.0 * uTanHalfFov / uResolution.y;
    float tHit = FAR;
    int fruit = -1;
    float fruitLodHit = 0.0;
    for (int i = 0; i < MAX_FRUIT; ++i) {
        if (i >= uFruitCount) break;
        vec4 d = uFruitData[i];
        float t0, t1;
        if (!glomeSpan(ro, rd, uFruitCenters[i], d.x * FRUIT_BOUND, t0, t1) || t0 >= tHit) continue;
        float lod = fruitLod(d.x, t0, pixel);
        float t = fruitMarch(ro, rd, uFruitCenters[i], d, t0, min(t1, tHit), lod);
        if (t > 0.0 && t < tHit) { tHit = t; fruit = i; fruitLodHit = lod; }
    }

    // World: sphere-traced SDF, stopped early at the nearest fruit. The
    // closest approach to the bowl feeds its glow.
    float t = 0.0;
    float mat = 0.0;
    float bowlNear = 1e3;
    for (int i = 0; i < MAX_STEPS; ++i) {
        vec2 h = worldSDF(ro + rd * t);
        if (h.y == 2.0) bowlNear = min(bowlNear, h.x);
        if (h.x < 0.001 * t) { mat = h.y; break; }
        t += h.x;
        if (t > tHit) break;
    }

    vec3 col;
    float dist;
    if (fruit >= 0 && tHit <= t) {
        vec4 p = ro + rd * tHit;
        vec4 c = uFruitCenters[fruit];
        vec4 d = uFruitData[fruit];
        vec4 q = p - c;
        vec4 body = normalize(q); // the physics glome's normal
        vec4 ns = fruitLodHit > 0.0 ? fruitNormal(q, d, fruitLodHit) : body;
        vec4 n = facetBlend(ns, 2.5 + 2.5 * sqrt(d.x), d.w);
        // A little self-light keeps each fruit's colour identifiable even
        // when it is back-lit by the low sun.
        vec3 albedo = fruitAlbedo(q, ns, d.x, d.y, d.w, fruitLodHit);
        // Plus a soft fill from the viewer, so the side we see is never just
        // the shadow side of a back-lit fruit.
        float fill = 0.35 * max(dot(n, -rd), 0.0);
        col = shade(p, n, ns, rd, albedo, fruitGloss(d.y)) + albedo * (0.18 + fill) + d.z * vec3(1.0, 0.9, 0.7);
        // 4D lean: a fruit's cross-section normals tilt along the slice
        // normal as it drifts out of the slice. Warm it towards the w-axis
        // gold of the indicator so "about to vanish" reads at a glance.
        // Added light (mostly at the rim), so the fruit keeps its own hue.
        float lean = smoothstep(0.35, 0.95, abs(dot(body, uCameraBasis[3])));
        float edge = 0.35 + 0.65 * pow(1.0 - max(dot(body, -rd), 0.0), 2.0);
        col += vec3(0.55, 0.38, 0.06) * lean * edge;
        dist = tHit;
    } else if (mat > 0.0) {
        vec4 p = ro + rd * t;
        vec4 ns = worldNormal(p);
        vec4 n = facetBlend(ns, 6.0, 0.0);
        col = shade(p, n, ns, rd, materialColor(mat, p, n), mat == 2.0 ? 0.25 : 0.0);
        dist = t;
    } else {
        col = skyColor(rd);
        dist = FAR;
    }

    // Sunset fog.
    float fog = 1.0 - exp(-max(dist - uFogStart, 0.0) * 0.018);
    col = mix(col, skyColor(rd), fog);

    // The bowl glows softly, as in the cover art.
    // Not over fruit, which must stay readable.
    bool hitFruit = fruit >= 0 && tHit <= t;
    col += vec3(1.0, 0.72, 0.42) * (hitFruit ? 0.0 : 0.22) * exp(-bowlNear * 5.0);

    col = drawHints(col, ro, rd, dist);

    // Ghost of the next drop, blended over the scene.
    if (uPreviewData.x > 0.0) {
        float tp = glomeHit(ro, rd, uPreviewCenter, uPreviewData.x);
        if (tp > 0.0 && tp < dist) {
            vec4 n = normalize(ro + rd * tp - uPreviewCenter);
            float f = 0.35 + 0.4 * pow(1.0 - abs(dot(n, rd)), 2.0);
            col = mix(col, fruitBaseColor(uPreviewData.y) * 1.2, f);
        }
    }

    // Grade: soft tone map, a little extra saturation, and the cover art's
    // darker edges.
    col *= 0.85;
    col = col / (1.0 + col * 0.4);
    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    col = clamp(mix(vec3(luma), col, 1.18), 0.0, 1.0);
    vec2 v = vUV - 0.5;
    col *= 1.0 - 0.55 * dot(v, v);
    fragColor = vec4(col, 1.0);
}
