// 4D raymarcher. The host prepends the #version line, precision, and
// #define MAX_FRUIT, then pastes world4d.glsl in place of its #include.
//
// Each pixel's ray starts at the camera and runs along
// uCameraBasis * (slice-space direction, 0), so it stays inside the 3D
// slice.
//
// PASS (defined by the host) picks what this program does:
//   0  everything in one pass (fallback without float render targets)
//   1  geometry: trace fruit and world, write hit and normal (G-buffer)
//   2  lighting at half resolution: shadow and AO of the G-buffer hits
//   3  compose: materials, light, sky, fog, hints, grade; lighting is
//      upsampled from pass 2 with depth-aware weights

#ifndef PASS
#define PASS 0
#endif

#if PASS == 1
layout(location = 0) out vec4 gGeom;   // t, kind, fruit lod, bowl distance
layout(location = 1) out vec4 gNormal; // smooth normal
#else
out vec4 fragColor;
#endif

uniform sampler2D uGeom;   // pass 1 targets, read by passes 2 and 3
uniform sampler2D uNormal;
uniform sampler2D uLight;  // pass 2 target: shadow, AO, t

uniform vec2  uResolution;          // render target size in pixels
uniform vec4  uCameraPos;
uniform mat4  uCameraBasis;         // columns: right, up, back, slice normal
uniform float uTanHalfFov;
uniform float uTime;
uniform vec4  uSunDir;

uniform int   uFruitCount;
uniform vec4  uFruitCenters[MAX_FRUIT];
uniform vec4  uFruitData[MAX_FRUIT]; // radius, type, flash, facet seed
uniform mat4  uFruitWorldToLocal[MAX_FRUIT]; // rotates (p - centre) into the fruit frame
uniform int   uHintCount;            // fruit just off the slice
uniform vec4  uHintCenters[MAX_HINTS]; // centre projected into the slice
uniform vec4  uHintData[MAX_HINTS];  // radius, type, signed gap to the slice, strength 0..1
uniform vec4  uPreviewCenter;        // ghost of the next drop
uniform vec4  uPreviewData;          // radius (0 = none), type
uniform float uFogStart;             // distance where fog begins (just past the orbit target)
uniform float uFacet;                // low-poly facet strength, 0 = smooth
uniform float uNearCut;              // world surfaces closer to the camera than this are cut away

// Slice cache (docs/4D.md): worldStaticSDF baked over a cube in slice
// coordinates. x in [0, uCacheSize]^3 maps to uCacheOrigin + uCacheBasis * x.
uniform int       uCacheMode;        // 0 = no cache (march the analytic SDF), 1 = cache
uniform sampler3D uCache;
uniform vec4      uCacheOrigin;
uniform mat4      uCacheBasis;       // columns: slice axes x, y, z, slice normal
uniform float     uCacheSize;        // cube side, 0 = no static world in this slice
uniform float     uCacheVoxel;
uniform vec3      uCacheUV;          // scale of the baked corner, min and max texture coordinate

#include "world4d.glsl"
#include "fruit4d.glsl"

// QUALITY 0..3 (Low, Medium, High, Ultra) is defined by the host.
#ifndef QUALITY
#define QUALITY 2
#endif
// Shadow steps read the slice cache (cheap texture fetches), so fewer suffice.
#if QUALITY == 0
const int MAX_STEPS = 72;  const int SHADOW_STEPS = 12; const int AO_SAMPLES = 2;
#elif QUALITY == 1
const int MAX_STEPS = 96;  const int SHADOW_STEPS = 18; const int AO_SAMPLES = 3;
#elif QUALITY == 2
const int MAX_STEPS = 128; const int SHADOW_STEPS = 24; const int AO_SAMPLES = 3;
#else
const int MAX_STEPS = 160; const int SHADOW_STEPS = 40; const int AO_SAMPLES = 5;
#endif

const float FAR = 80.0;

// --cost-map: count analytic SDF evaluations per pixel (world and fruit) and
// output that instead of colour. A deterministic profile that shared GPUs
// cannot disturb.
#ifdef COST_MAP
ivec3 gCost = ivec3(0); // static world, moving world, fruit
#define COST(n) gCost.x += (n)
#define COST_DYNAMIC(n) gCost.y += (n)
#define COST_FRUIT(n) gCost.z += (n)
#else
#define COST(n)
#define COST_DYNAMIC(n)
#define COST_FRUIT(n)
#endif

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

vec4 fruitLocalPoint(int i, vec4 p) {
    return uFruitWorldToLocal[i] * (p - uFruitCenters[i]);
}

vec4 fruitLocalVector(int i, vec4 v) {
    return uFruitWorldToLocal[i] * v;
}

vec4 fruitWorldVector(int i, vec4 v) {
    return transpose(uFruitWorldToLocal[i]) * v;
}

// Sphere-trace a fruit's own SDF between t0 and t1 (its bound glome). At
// lod 0 the fruit is its physics glome, hit analytically.
float fruitMarch(vec4 ro, vec4 rd, vec4 d, float t0, float t1, float lod) {
    if (lod <= 0.0) {
        float th = glomeHit(ro, rd, vec4(0.0), d.x);
        return th > 0.0 ? th : -1.0;
    }
    float t = t0;
    for (int k = 0; k < 24; ++k) {
        float h = fruitSDF(ro + rd * t, d.x, d.y, lod);
        COST_FRUIT(1);
        if (h < 0.002 * d.x) return t;
        t += h;
        if (t > t1) break;
    }
    return -1.0;
}

vec4 fruitLocalNormal(vec4 q, vec4 d, float lod) {
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

vec4 fruitWorldNormal(int i, vec4 q, vec4 d, float lod) {
    return normalize(fruitWorldVector(i, fruitLocalNormal(q, d, lod)));
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


// Normal of the surface's cross-section by the slice: the 4D gradient
// projected into the slice, from forward differences along the slice axes
// (f0 is the distance at p, known from the march). Three evaluations; light
// and shadows both work in the slice, so the part of the gradient along the
// slice normal is not needed.
// Static hits differentiate only the parts of their material
// (worldStaticSDFFor), so f0 is re-evaluated for that same function.
vec4 worldNormal(vec4 p, float f0, float mat, bool dynamic) {
    const float h = 0.002;
    vec4 e0 = h * uCameraBasis[0], e1 = h * uCameraBasis[1], e2 = h * uCameraBasis[2];
    vec3 f;
    if (dynamic) {
        COST_DYNAMIC(3);
        f = vec3(worldSDF(p + e0).x, worldSDF(p + e1).x, worldSDF(p + e2).x);
    } else {
        COST(4); // part-only evaluations, each far cheaper than the whole world
        f0 = worldStaticSDFFor(p, mat).x;
        f = vec3(worldStaticSDFFor(p + e0, mat).x, worldStaticSDFFor(p + e1, mat).x,
                 worldStaticSDFFor(p + e2, mat).x);
    }
    return normalize(uCameraBasis[0] * (f.x - f0) + uCameraBasis[1] * (f.y - f0) + uCameraBasis[2] * (f.z - f0));
}

// ---------------------------------------------------------------- slice cache
//
// Trilinear filtering of exact distances is within 0.87 voxel of the truth
// (the SDF is 1-Lipschitz), so cache value - 0.9 voxel is a safe step.
// Where that is under a voxel the march falls back to the analytic SDF, so
// thin parts are never stepped over and hits are exact.

vec3 toCache(vec4 p) { return (transpose(uCacheBasis) * (p - uCacheOrigin)).xyz; }
vec3 dirToCache(vec4 d) { return (transpose(uCacheBasis) * d).xyz; }
float cacheDist(vec3 x) {
    return texture(uCache, clamp(x / uCacheSize * uCacheUV.x, uCacheUV.y, uCacheUV.z)).r;
}
bool inCache(vec3 x) { return all(greaterThanEqual(x, vec3(0.0))) && all(lessThanEqual(x, vec3(uCacheSize))); }

// Entry and exit of the ray with the cache cube; false if it misses.
bool cacheSpan(vec3 o, vec3 d, out float t0, out float t1) {
    vec3 inv = 1.0 / d;
    vec3 a = (vec3(0.0) - o) * inv, b = (vec3(uCacheSize) - o) * inv;
    vec3 lo = min(a, b), hi = max(a, b);
    t0 = max(max(max(lo.x, lo.y), lo.z), 0.0);
    t1 = min(min(hi.x, hi.y), hi.z);
    return uCacheSize > 0.0 && t1 > t0;
}

// Static world along an in-slice ray: (t, material, distance at t),
// material 0 = miss.
// bowlNear collects the closest approach to the bowl for its glow.
vec3 marchStatic(vec4 ro, vec4 rd, float tMax, inout float bowlNear) {
    if (uCacheMode == 0) {
        float t = uNearCut; // cutaway, as below
        for (int i = 0; i < 24 && worldStaticSDF(ro + rd * t).x < 0.02; ++i) t += 0.1;
        for (int i = 0; i < MAX_STEPS; ++i) {
            vec4 p = ro + rd * t;
            vec2 h = worldStaticSDF(p);
            COST(1);
            bowlNear = min(bowlNear, bowlSDF(p));
            if (h.x < 0.001 * t) return vec3(t, h.y, h.x);
            t += h.x;
            if (t > tMax) break;
        }
        return vec3(tMax, 0.0, 0.0);
    }
    vec3 o = toCache(ro), d = dirToCache(rd);
    float t, t1;
    if (!cacheSpan(o, d, t, t1)) return vec3(tMax, 0.0, 0.0);
    t1 = min(t1, tMax);
    // Cutaway: in a turned slice a hill that lives at some w can rise around
    // the camera. Nothing nearer than uNearCut is drawn; a ray that starts
    // inside a solid there walks out of it before tracing.
    if (t < uNearCut) {
        t = uNearCut;
        for (int i = 0; i < 24; ++i) {
            vec4 p = ro + rd * t;
            float c = cacheDist(o + d * t);
            float h = c < uCacheVoxel ? worldStaticSDF(p).x : c;
            if (h > 0.02) break;
            t += max(-h, 0.0) + 0.05;
        }
    }
    float margin = 0.9 * uCacheVoxel + 0.01;
    for (int i = 0; i < MAX_STEPS; ++i) {
        vec4 p = ro + rd * t;
        bowlNear = min(bowlNear, bowlSDF(p));
        float adv = cacheDist(o + d * t) - margin;
        if (adv < uCacheVoxel) {
            vec2 h = worldStaticSDF(p);
            COST(1);
            if (h.x < 0.001 * t) return vec3(t, h.y, h.x);
            adv = h.x;
        }
        t += adv;
        if (t > t1) break;
    }
    return vec3(tMax, 0.0, 0.0);
}

// Moving parts (clouds), marched live. Steps are capped so the parts' own
// bounds skip most of the work.
vec3 marchDynamic(vec4 ro, vec4 rd, float tMax) {
    float t = 0.0;
    for (int i = 0; i < 48; ++i) {
        vec2 h = worldDynamicSDF(ro + rd * t, 6.0);
        COST_DYNAMIC(1);
        if (h.x < 0.001 * t + 0.002) return h.y > 0.0 ? vec3(t, h.y, h.x) : vec3(tMax, 0.0, 0.0);
        t += h.x;
        if (t > tMax) break;
    }
    return vec3(tMax, 0.0, 0.0);
}

// Shadows and AO may use a cheaper, coarser world distance if the world
// provides one (#define WORLD_SHADOW_SDF and float worldShadowSDF(vec4)).
#ifdef WORLD_SHADOW_SDF
float occluderSDF(vec4 p) { return worldShadowSDF(p); }
#else
float occluderSDF(vec4 p) { return worldSDF(p).x; }
#endif

// Soft shadow towards the sun; fruit are analytic. With the cache, the
// shadow ray runs inside the slice (rd is the sun projected into it) over
// the cached distances.
float softShadow(vec4 ro, vec4 rd) {
    float res = 1.0;
    for (int i = 0; i < MAX_FRUIT; ++i) {
        if (i >= uFruitCount) break;
        res = min(res, glomeShadow(ro, rd, uFruitCenters[i], uFruitData[i].x));
    }
    if (uCacheMode != 0) {
        vec3 o = toCache(ro), d = dirToCache(rd);
        float t = uCacheVoxel;
        for (int i = 0; i < SHADOW_STEPS; ++i) {
            vec3 x = o + d * t;
            if (res < 0.01 || t > 25.0 || !inCache(x)) break;
            float h = cacheDist(x);
            res = min(res, 10.0 * h / t);
            t += clamp(h, 0.03, 2.5);
        }
        return clamp(res, 0.0, 1.0);
    }
    float t = 0.05;
    for (int i = 0; i < SHADOW_STEPS; ++i) {
        if (res < 0.01) break;
        float h = occluderSDF(ro + rd * t);
        COST(1);
        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.03, 2.5);
        if (res < 0.01 || t > 25.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

float ambientOcclusion(vec4 p, vec4 n) {
    float occ = 0.0, w = 1.0;
    // With the cache, samples step along the normal's part inside the slice.
    vec3 o = toCache(p), d = dirToCache(n);
    bool cached = uCacheMode != 0 && dot(d, d) > 0.01;
    d = cached ? normalize(d) : d;
    for (int i = 1; i <= AO_SAMPLES; ++i) {
        float h = 0.3 * float(i * i) / float(AO_SAMPLES * AO_SAMPLES) * 5.0;
        vec3 x = o + d * h;
        float dist = cached ? (inCache(x) ? cacheDist(x) : h) : occluderSDF(p + n * h);
        COST(cached ? 0 : 1);
        occ += (h - dist) * w;
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

// Direction shadow rays take: the sun, or with the cache its projection into
// the slice (zero when the sun is almost normal to the slice).
vec4 shadowDir() {
    if (uCacheMode == 0) return uSunDir;
    vec4 s = uSunDir - dot(uSunDir, uCacheBasis[3]) * uCacheBasis[3];
    return dot(s, s) > 0.0025 ? normalize(s) : vec4(0.0);
}

// n: lighting (faceted) normal; ns: smooth normal. wrap: light that passes
// through (foliage), 0 = opaque. light: shadow and AO (lightHit).
vec3 shade(vec4 p, vec4 n, vec4 ns, vec4 rd, vec3 albedo, float gloss, float wrap, vec2 light) {
    float sunDot = max((dot(n, uSunDir) + wrap) / (1.0 + wrap), 0.0);
    float sh = mix(light.x, 1.0, 0.5 * wrap);
    float ao = light.y;
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

// ---------------------------------------------------------------- passes

// What a pixel's ray hit. kind: 0 sky, > 0 world material id,
// < 0 fruit -(index + 1).
struct Hit {
    float t;
    float kind;
    float lod;      // fruit detail
    float bowlNear; // closest approach to the bowl (glow)
    vec4 ns;        // smooth normal
    float behind;   // bowl hits: fruit seen through the wall, index + 1 (0 = none)
    float behindT;  // and its distance
};

// Ray through the centre of render-target pixel `px` (in pixels).
vec4 rayDir(vec2 px) {
    vec2 ndc = px / uResolution * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    vec3 local = normalize(vec3(ndc.x * aspect * uTanHalfFov, ndc.y * uTanHalfFov, -1.0));
    return uCameraBasis * vec4(local, 0.0);
}

Hit traceScene(vec4 ro, vec4 rd) {
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
        vec4 localRo = fruitLocalPoint(i, ro);
        vec4 localRd = fruitLocalVector(i, rd);
        float t = fruitMarch(localRo, localRd, d, t0, min(t1, tHit), lod);
        if (t > 0.0 && t < tHit) { tHit = t; fruit = i; fruitLodHit = lod; }
    }

    // World, stopped early at the nearest fruit.
    Hit h;
    h.bowlNear = 1e3;
    vec3 hs = marchStatic(ro, rd, min(tHit, FAR), h.bowlNear);
    vec3 hd = marchDynamic(ro, rd, hs.x);
    vec3 hw = hd.y > 0.0 ? hd : hs;

    h.behind = 0.0;
    h.behindT = 0.0;
    if (fruit >= 0 && tHit <= hw.x) {
        vec4 c = uFruitCenters[fruit];
        vec4 d = uFruitData[fruit];
        vec4 p = ro + rd * tHit;
        vec4 q = fruitLocalPoint(fruit, p);
        h.t = tHit;
        h.kind = -float(fruit + 1);
        h.lod = fruitLodHit;
        h.ns = fruitLodHit > 0.0 ? fruitWorldNormal(fruit, q, d, fruitLodHit) : normalize(p - c);
    } else if (hw.y > 0.0) {
        h.t = hw.x;
        h.kind = hw.y;
        h.lod = 0.0;
        h.ns = worldNormal(ro + rd * hw.x, hw.z, hw.y, hd.y > 0.0);
        if (hw.y == MAT_BOWL && fruit >= 0) {
            h.behind = float(fruit + 1);
            h.behindT = tHit;
        }
    } else {
        h.t = FAR;
        h.kind = 0.0;
        h.lod = 0.0;
        h.ns = vec4(0.0);
    }
    return h;
}

// Leaves pass some sun through, so canopies seen from below stay green.
float wrapOf(float kind) { return kind == MAT_LEAVES ? 0.6 : 0.0; }

// Shadow (before wrap) and AO at a hit.
vec2 lightHit(vec4 ro, vec4 rd, Hit h) {
    if (h.kind == 0.0) return vec2(1.0);
    vec4 p = ro + rd * h.t;
    float wrap = wrapOf(h.kind);
    vec4 sd = shadowDir();
    float sh = dot(sd, sd) == 0.0 ? 1.0
             : dot(h.ns, sd) > -0.1 - wrap ? softShadow(p + h.ns * 0.02, sd) : 0.0;
    return vec2(sh, ambientOcclusion(p, h.ns));
}

// Final colour of a pixel from its hit and light (shadow, AO).
vec3 compose(vec2 px, vec4 ro, vec4 rd, Hit h, vec2 light) {
    vec3 col = vec3(0.0);
    float dist = h.t;
    if (h.kind < 0.0) {
        int fruit = int(-h.kind) - 1;
        vec4 p = ro + rd * h.t;
        vec4 c = uFruitCenters[fruit];
        vec4 d = uFruitData[fruit];
        vec4 qWorld = p - c;
        vec4 q = fruitLocalPoint(fruit, p);
        vec4 body = normalize(qWorld); // the physics glome's normal
        vec4 ns = h.ns;
        vec4 n = facetBlend(ns, 2.5 + 2.5 * sqrt(d.x), d.w);
        // A little self-light keeps each fruit's colour identifiable even
        // when it is back-lit by the low sun.
        vec3 albedo = fruitAlbedo(q, normalize(fruitLocalVector(fruit, ns)), d.x, d.y, d.w, h.lod);
        // Plus a soft fill from the viewer, so the side we see is never just
        // the shadow side of a back-lit fruit.
        float fill = 0.35 * max(dot(n, -rd), 0.0);
        col = shade(p, n, ns, rd, albedo, fruitGloss(d.y), 0.0, light) + albedo * (0.18 + fill) + d.z * vec3(1.0, 0.9, 0.7);
        // 4D lean: a fruit's cross-section normals tilt along the slice
        // normal as it drifts out of the slice. Warm it towards the w-axis
        // gold of the indicator so "about to vanish" reads at a glance.
        // Added light (mostly at the rim), so the fruit keeps its own hue.
        float lean = smoothstep(0.35, 0.95, abs(dot(body, uCameraBasis[3])));
        float edge = 0.35 + 0.65 * pow(1.0 - max(dot(body, -rd), 0.0), 2.0);
        col += vec3(0.55, 0.38, 0.06) * lean * edge;
    } else if (h.kind > 0.0) {
        vec4 p = ro + rd * h.t;
        vec4 n = facetBlend(h.ns, 6.0, 0.0);
        col = shade(p, n, h.ns, rd, materialColor(h.kind, p, n), h.kind == MAT_BOWL ? 0.25 : 0.0,
                    wrapOf(h.kind), light);
        // The bowl is frosted glass for the fruit inside: they show through
        // its wall, softened, so the player never loses them behind it.
        if (h.behind > 0.0) {
            int i = int(h.behind) - 1;
            vec4 d = uFruitData[i];
            vec4 p = ro + rd * h.behindT;
            vec4 q = fruitLocalPoint(i, p);
            vec4 fn = normalize(q);
            vec4 fnWorld = normalize(fruitWorldVector(i, fn));
            vec3 albedo = fruitAlbedo(q, fn, d.x, d.y, d.w, 0.0);
            float lit = 0.35 + 0.65 * max(dot(fnWorld, uSunDir), 0.0) + 0.3 * max(dot(fnWorld, -rd), 0.0);
            vec3 seen = albedo * lit * vec3(1.0, 0.9, 0.8) * 1.6;
            float rim = pow(1.0 - abs(dot(h.ns, rd)), 2.0);
            col = mix(seen, col, 0.35 + 0.5 * rim);
        }
    }

    // Sunset fog. The sky (noise clouds, not cheap) is evaluated once, and
    // only where it shows.
    float fog = dist >= FAR ? 1.0 : 1.0 - exp(-max(dist - uFogStart, 0.0) * 0.018);
    if (fog > 0.002) col = mix(col, skyColor(rd), fog);

    // The bowl glows softly, as in the cover art.
    // Not over fruit, which must stay readable.
    col += vec3(1.0, 0.72, 0.42) * (h.kind < 0.0 ? 0.0 : 0.22) * exp(-h.bowlNear * 5.0);

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
    vec2 v = px / uResolution - 0.5;
    col *= 1.0 - 0.55 * dot(v, v);
    return col;
}

Hit readGeometry(ivec2 texel) {
    vec4 g = texelFetch(uGeom, texel, 0);
    Hit h;
    h.t = g.x;
    h.kind = g.y;
    bool bowl = h.kind == MAT_BOWL;
    h.lod = bowl ? 0.0 : g.z;
    h.bowlNear = bowl ? 0.0 : g.w;
    h.behind = bowl ? g.z : 0.0;
    h.behindT = bowl ? g.w : 0.0;
    h.ns = texelFetch(uNormal, texel, 0);
    return h;
}

// Lighting for full-resolution pixel `texel` (depth t) from the
// half-resolution pass: bilinear weights times depth similarity, so light
// never bleeds across silhouettes.
vec2 upsampleLight(ivec2 texel, float t) {
    vec2 hp = (vec2(texel) + 0.5) * 0.5 - 0.5;
    ivec2 base = ivec2(floor(hp));
    vec2 f = hp - vec2(base);
    ivec2 size = textureSize(uLight, 0) - 1;
    vec2 sum = vec2(0.0);
    float wsum = 0.0;
    vec2 nearest = vec2(1.0);
    float best = 1e9;
    for (int k = 0; k < 4; ++k) {
        ivec2 o = ivec2(k & 1, k >> 1);
        vec4 l = texelFetch(uLight, clamp(base + o, ivec2(0), size), 0);
        float dz = abs(l.z - t) / max(t, 1.0);
        vec2 bw = mix(1.0 - f, f, vec2(o));
        float w = bw.x * bw.y / (1e-3 + dz * 50.0);
        sum += l.xy * w;
        wsum += w;
        if (dz < best) { best = dz; nearest = l.xy; }
    }
    return best > 0.02 ? nearest : sum / wsum;
}

// ---------------------------------------------------------------- main

void main() {
    vec2 px = gl_FragCoord.xy;
    vec4 ro = uCameraPos;
#if PASS == 1
    Hit h = traceScene(ro, rayDir(px));
    // z: fruit lod, or for bowl hits the fruit behind the wall; w: bowl
    // distance (glow), or for bowl hits that fruit's distance.
    bool bowl = h.kind == MAT_BOWL;
    gGeom = vec4(h.t, h.kind, bowl ? h.behind : h.lod, bowl ? h.behindT : h.bowlNear);
    gNormal = h.ns;
#elif PASS == 2
    // Each half-resolution texel lights the top-left pixel of its 2x2 block.
    ivec2 texel = min(ivec2(px) * 2, ivec2(uResolution) - 1);
    Hit h = readGeometry(texel);
    vec2 light = lightHit(ro, rayDir(vec2(texel) + 0.5), h);
    fragColor = vec4(light, h.t, 1.0);
#elif PASS == 3
    ivec2 texel = ivec2(px);
    Hit h = readGeometry(texel);
    vec2 light = h.kind == 0.0 ? vec2(1.0) : upsampleLight(texel, h.t);
    fragColor = vec4(compose(px, ro, rayDir(px), h, light), 1.0);
#else
    vec4 rd = rayDir(px);
    Hit h = traceScene(ro, rd);
    fragColor = vec4(compose(px, ro, rd, h, lightHit(ro, rd, h)), 1.0);
#endif
#if defined(COST_MAP) && PASS != 1
    // Evaluations / 255 per channel: R static world, G moving world, B fruit.
    fragColor = vec4(vec3(gCost) / 255.0, 1.0);
#endif
}
