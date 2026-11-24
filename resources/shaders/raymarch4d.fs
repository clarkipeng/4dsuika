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
//   4  4D eye retina: four layers of the 3D retina per draw, each texel the
//      colour and density seen along one 4D ray from the camera
//   5  4D eye display: the retina as a semi-transparent volume

#ifndef PASS
#define PASS 0
#endif

#if PASS == 1
layout(location = 0) out vec4 gGeom;   // t, kind, fruit lod, fruit part
layout(location = 1) out vec4 gNormal; // smooth normal
#elif PASS == 4
layout(location = 0) out vec4 out0;
layout(location = 1) out vec4 out1;
layout(location = 2) out vec4 out2;
layout(location = 3) out vec4 out3;
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
uniform float uFruitGlow[MAX_FRUIT]; // 0..1, a same-kind fruit is close in 4D
uniform mat4  uFruitWorldToLocal[MAX_FRUIT]; // rotates (p - centre) into the fruit frame
uniform int   uHintCount;            // fruit just off the slice
uniform vec4  uHintCenters[MAX_HINTS]; // centre projected into the slice
uniform vec4  uHintData[MAX_HINTS];  // radius, type, signed gap to the slice, strength 0..1
uniform int   uBurstCount;           // merge bursts
uniform vec4  uBurstCenters[MAX_BURSTS]; // world centre of the merge
uniform vec4  uBurstData[MAX_BURSTS]; // fruit radius, type, progress 0..1, power
uniform float uCues;                // 0 hides depth rings, hints and glow (tutorial, 3D phase)
uniform float uDanger;               // 0..1, the pile is near the rim
uniform float uBgSoft;               // 0..1, how much the background is softened (present pass blurs it)
uniform float uCalmFlash;            // 1: capture-only softer merge flash and burst sum (--calm-flash)
uniform vec4  uPreviewCenter;        // ghost of the next drop
uniform vec4  uPreviewData;          // radius (0 = none), type
uniform mat4  uPreviewWorldToLocal;  // the next fruit's real orientation
uniform vec4  uLandCenter;           // where it lands (w < -1e5: none)
uniform float uFogStart;             // distance where fog begins (just past the orbit target)
uniform float uFacet;                // low-poly facet strength, 0 = smooth
uniform float uNearCut;              // world surfaces closer to the camera than this are cut away

// Loop bounds the D3D11 compiler (FXC, which ANGLE uses in Windows browsers)
// cannot see. It unrolls loops with constant bounds and inlines every call,
// so a march with a fruit or world SDF inside becomes hundreds of copies of
// it and the program takes minutes to link. Never set, so always 0.
// Desktop GL compilers unroll well, so they keep constant bounds.
#ifdef GL_ES
uniform int uNoUnroll;
#define NO_UNROLL(n) ((n) + uNoUnroll)
#else
#define NO_UNROLL(n) (n)
#endif

// Slice cache (docs/4D.md): worldStaticSDF baked over a cube in slice
// coordinates. x in [0, uCacheSize]^3 maps to uCacheOrigin + uCacheBasis * x.
uniform int       uCacheMode;        // 0 = no cache (march the analytic SDF), 1 = cache
uniform sampler3D uCache;
uniform vec4      uCacheOrigin;
uniform mat4      uCacheBasis;       // columns: slice axes x, y, z, slice normal
uniform float     uCacheSize;        // cube side, 0 = no static world in this slice
uniform float     uCacheVoxel;
uniform vec3      uCacheUV;          // scale of the baked corner, min and max texture coordinate

// 4D eye (docs/4D.md, "View modes").
uniform sampler3D uRetina;           // pass 4 target, read by pass 5
uniform vec3      uRetinaSize;       // texels: u, v, s
uniform float     uLayer;            // pass 4: retina layer of out0; out1..3 follow
uniform mat3      uEyeView;          // pass 5: display camera axes (columns), looking along -column 2
uniform vec3      uEyeBox;           // pass 5: half extents of the retina box (u, v, s)

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

// Sphere-trace a fruit's own SDF between t0 and t1 (its bound glome), then
// take the local normal at the hit by differences on the five vertices of a
// 4-simplex. At lod 0 the fruit is its physics glome, hit analytically.
// Returns (t, part at the hit), t = -1 on a miss, and the local normal n.
// March and normal share one loop and one fruitSDFPart call: compilers inline
// every call, and D3D11 (ANGLE in Windows browsers) takes minutes over a
// shader with many copies of the fruit.
vec2 fruitTrace(vec4 ro, vec4 rd, vec4 d, float t0, float t1, float lod, out vec4 n) {
    n = vec4(0.0);
    if (lod <= 0.0) {
        float th = glomeHit(ro, rd, vec4(0.0), d.x);
        n = normalize(ro + rd * max(th, 0.0));
        return vec2(th > 0.0 ? th : -1.0, FRUIT_SKIN);
    }
    float h = 0.002 * d.x;
    const float s = 0.4472136;
    const vec4 K[5] = vec4[5](vec4( 1.0,  1.0,  1.0, -s), vec4( 1.0, -1.0, -1.0, -s),
                              vec4(-1.0,  1.0, -1.0, -s), vec4(-1.0, -1.0,  1.0, -s),
                              vec4( 0.0,  0.0,  0.0, 4.0 * s));
    float t = t0, part = FRUIT_SKIN;
    int probe = -1; // >= 0 once hit: the normal sample being taken
    for (int k = 0; k < NO_UNROLL(24 + 5); ++k) {
        vec4 q = ro + rd * t;
        if (probe >= 0) q += h * K[probe];
        vec2 f = fruitSDFPart(q, d.x, d.y, lod);
        COST_FRUIT(1);
        if (probe >= 0) {
            n += K[probe] * f.x;
            if (++probe == 5) {
                n = normalize(n);
                return vec2(t, part);
            }
        } else if (f.x < 0.002 * d.x) {
            probe = 0;
            part = f.y;
        } else {
            t += f.x;
            if (t > t1 || k >= 23) break;
        }
    }
    return vec2(-1.0, FRUIT_SKIN);
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
// (worldStaticSDFFor), so f0 is re-evaluated for that same function; moving
// hits (clouds) differentiate the moving parts. The evaluations share one
// loop so each SDF is inlined once (see marchStatic).
vec4 worldNormal(vec4 p, float f0, float mat, bool dynamic) {
    const float h = 0.002;
    vec4 f = vec4(f0);
    for (int k = dynamic ? 1 : 0; k < NO_UNROLL(4); ++k) {
        vec4 q = k == 0 ? p : p + h * uCameraBasis[k - 1];
        if (dynamic) {
            f[k] = worldDynamicSDF(q, 6.0).x;
            COST_DYNAMIC(1);
        } else {
            f[k] = worldStaticSDFFor(q, mat).x; // part-only, far cheaper than the whole world
            COST(1);
        }
    }
    return normalize(uCameraBasis[0] * (f.y - f.x) + uCameraBasis[1] * (f.z - f.x) + uCameraBasis[2] * (f.w - f.x));
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
// One loop with one worldStaticSDF call: compilers inline every call, and
// D3D11 (ANGLE in Windows browsers) takes minutes over a shader with many.
vec3 marchStatic(vec4 ro, vec4 rd, float tMax) {
    bool cached = uCacheMode != 0;
    vec3 o = toCache(ro), d = dirToCache(rd);
    float t = 0.0, t1 = tMax;
    if (cached) {
        if (!cacheSpan(o, d, t, t1)) return vec3(tMax, 0.0, 0.0);
        t1 = min(t1, tMax);
    }
    // Cutaway: in a turned slice a hill that lives at some w can rise around
    // the camera. Nothing nearer than uNearCut is drawn; a ray that starts
    // inside a solid there walks out of it (up to 24 steps) before tracing.
    int walk = t < uNearCut || !cached ? 24 : 0;
    t = max(t, uNearCut);
    // With the cache, steps read the cached distance less a safe margin and
    // fall back to the analytic SDF within a voxel of a surface.
    float margin = cached ? 0.9 * uCacheVoxel + 0.01 : 0.0;
    for (int i = 0; i < NO_UNROLL(MAX_STEPS + 24); ++i) {
        vec4 p = ro + rd * t;
        vec2 h = vec2(cached ? cacheDist(o + d * t) - margin : 0.0, MAT_NONE);
        bool exact = !cached || h.x < uCacheVoxel;
        if (exact) {
            h = worldStaticSDF(p);
            COST(1);
        }
        if (walk > 0) {
            if (h.x <= 0.02) {
                t += cached ? max(-h.x, 0.0) + 0.05 : 0.1;
                --walk;
                continue;
            }
            walk = 0;
        }
        if (exact && h.x < 0.001 * t) return vec3(t, h.y, h.x);
        t += h.x;
        if (t > t1) break;
    }
    return vec3(tMax, 0.0, 0.0);
}

// Moving parts (clouds), marched live. Steps are capped so the parts' own
// bounds skip most of the work.
vec3 marchDynamic(vec4 ro, vec4 rd, float tMax) {
    float t = 0.0;
    for (int i = 0; i < NO_UNROLL(48); ++i) {
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
    for (int i = 0; i < NO_UNROLL(MAX_FRUIT); ++i) {
        if (i >= uFruitCount) break;
        res = min(res, glomeShadow(ro, rd, uFruitCenters[i], uFruitData[i].x));
    }
    if (uCacheMode != 0) {
        vec3 o = toCache(ro), d = dirToCache(rd);
        float t = uCacheVoxel;
        for (int i = 0; i < NO_UNROLL(SHADOW_STEPS); ++i) {
            vec3 x = o + d * t;
            if (res < 0.01 || t > 25.0 || !inCache(x)) break;
            float h = cacheDist(x);
            res = min(res, 10.0 * h / t);
            t += clamp(h, 0.03, 2.5);
        }
        return clamp(res, 0.0, 1.0);
    }
    float t = 0.05;
    for (int i = 0; i < NO_UNROLL(SHADOW_STEPS); ++i) {
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
    for (int i = 1; i <= NO_UNROLL(AO_SAMPLES); ++i) {
        float h = 0.3 * float(i * i) / float(AO_SAMPLES * AO_SAMPLES) * 5.0;
        vec3 x = o + d * h;
        float dist = cached ? (inCache(x) ? cacheDist(x) : h) : occluderSDF(p + n * h);
        COST(cached ? 0 : 1);
        occ += (h - dist) * w;
        w *= 0.6;
    }
    float ao = clamp(1.0 - 1.4 * occ, 0.0, 1.0);
    for (int i = 0; i < NO_UNROLL(MAX_FRUIT); ++i) {
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
// hints, never as solid fruit; behind scene geometry they are fainter, and
// behind the bowl (which is opaque) they do not show.

vec3 depthColor(float offset, float span) {
    vec3 side = offset < 0.0 ? vec3(1.0, 0.31, 0.18) : vec3(0.20, 0.68, 1.0);
    return mix(vec3(1.0, 0.76, 0.32), side, smoothstep(0.0, span, abs(offset)));
}
vec3 depthColor(float offset) { return depthColor(offset, 1.5); }

vec3 drawHints(vec3 col, vec4 ro, vec4 rd, float sceneDist, bool bowl) {
    float pixel = 2.0 * uTanHalfFov / uResolution.y;
    for (int i = 0; i < NO_UNROLL(MAX_HINTS); ++i) {
        if (i >= uHintCount) break;
        vec4 c = uHintCenters[i];
        vec4 d = uHintData[i];
        float r = d.x;
        float tc = dot(c - ro, rd);
        if (tc <= 0.0) continue;
        float miss = length(c - ro - rd * tc);
        // A ring of round dots (never a solid line, which is the held fruit's
        // ghost outline): dots about 9 px across, one every ~20 px, plus a
        // whisper of fill.
        float dotR = max(4.5 * pixel * tc, 0.03 * r);
        float nDots = clamp(floor(6.2831853 * r / (20.0 * pixel * tc)), 8.0, 72.0);
        vec4 delta = ro + rd * tc - c;
        vec2 plane = vec2(dot(delta, uCameraBasis[0]), dot(delta, uCameraBasis[1]));
        float ang = atan(plane.y, plane.x + 1e-6);
        float cell = fract(ang * nDots / 6.2831853) - 0.5;
        vec2 q = vec2(cell * 6.2831853 / nDots * r, miss - r);
        float dotD = length(q) / dotR;
        float ring = 1.0 - smoothstep(0.75, 1.0, dotD);
        float halo = 0.5 * exp(-dotD * dotD * 0.6);
        float fill = 0.035 * (1.0 - smoothstep(r - dotR, r, miss));
        float occluded = tc - r > sceneDist ? (bowl ? 0.0 : 0.6) : 1.0;
        float k = d.w * occluded * uCues;
        vec3 tint = depthColor(d.z);
        col = mix(col, tint * 0.35, clamp(k * halo * 0.6, 0.0, 0.6));        // dark halo: reads on light fruit
        col = mix(col, mix(tint, vec3(1.0), 0.25), clamp(k * (ring + fill) * 1.2, 0.0, 0.95));
    }
    return col;
}

// ---------------------------------------------------------------- merge bursts
//
// A merge sends out a shell in 4D, from the fruit's radius to about 2.6 times
// it. The slice sees the shell's cross-section, a ring that grows, thins and
// fades, with sparks along it and a flash at the middle early on. A shell
// the slice misses does not show, which is the point: turn to where it went.

vec3 drawBursts(vec3 col, vec4 ro, vec4 rd, float sceneDist, bool bowl) {
    float pixel = 2.0 * uTanHalfFov / uResolution.y;
    vec3 glow = vec3(0.0);
    for (int i = 0; i < NO_UNROLL(MAX_BURSTS); ++i) {
        if (i >= uBurstCount) break;
        vec4 d = uBurstData[i];
        float t = d.z;
        float grow = 1.0 - (1.0 - t) * (1.0 - t);
        float R = d.x * (1.0 + 1.6 * grow);
        float off = dot(uBurstCenters[i] - ro, uCameraBasis[3]);
        if (abs(off) >= R) continue;
        vec4 c = uBurstCenters[i] - off * uCameraBasis[3];
        float tc = dot(c - ro, rd);
        if (tc <= 0.0) continue;
        float section = sqrt(R * R - off * off);
        vec4 delta = ro + rd * tc - c;
        float miss = length(delta);
        float width = max((3.5 - 2.0 * t) * pixel * tc, 0.04 * R);
        float e = (miss - section) / width;
        float ring = exp(-e * e);
        vec2 plane = vec2(dot(delta, uCameraBasis[0]), dot(delta, uCameraBasis[1]));
        float ang = atan(plane.y, plane.x + 1e-6);
        float sparks = exp(-4.0 * pow(abs(sin(7.0 * ang + 3.0 * t)), 0.5)) * smoothstep(-section * 0.1, section * 0.5, miss)
                       * (1.0 - smoothstep(section * 0.75, section * 1.05, miss));
        float flash = exp(-miss * miss / (d.x * d.x * 0.9)) * (1.0 - smoothstep(0.0, 0.4, t));
        float fade = pow(1.0 - t, 1.4) * d.w;
        float occluded = tc - section > sceneDist ? (bowl ? 0.0 : 0.35) : 1.0;
        vec3 tint = mix(fruitBaseColor(d.y) * 1.6, vec3(1.0, 0.95, 0.78), 0.55);
        // --calm-flash: a combo brightens the ring and sparks only; the centre
        // flash stays at a single merge's strength so it never hides the fruit.
        float flashFade = uCalmFlash > 0.5 ? pow(1.0 - t, 1.4) * min(d.w, 1.0) * 0.3 : fade;
        glow += tint * ((1.1 * ring + 0.55 * sparks) * fade + 1.2 * flash * flashFade) * occluded;
    }
    if (uCalmFlash < 0.5) return col + glow;
    // --calm-flash: overlapping bursts (a chain) add up; a soft knee (linear up
    // to 0.25, easing to 0.45) and a screen blend keep their sum from washing
    // the bowl and the new fruit out to white.
    float m = max(glow.r, max(glow.g, glow.b));
    if (m > 0.25) glow *= (0.25 + 0.2 * (1.0 - exp(-(m - 0.25) / 0.2))) / m;
    return col + glow * (1.0 - clamp(col, 0.0, 1.0));
}

// A translucent copy of the next fruit (same model and SDF, same orientation)
// at `center`: faint body, brighter rim.
vec3 drawDropGhost(vec3 col, vec4 ro, vec4 rd, float dist, vec4 center, float alpha) {
    vec4 d = vec4(uPreviewData.x, uPreviewData.y, 0.0, 0.0);
    float t0, t1;
    if (!glomeSpan(ro, rd, center, d.x * FRUIT_BOUND, t0, t1) || t0 >= dist) return col;
    float pixel = 2.0 * uTanHalfFov / uResolution.y;
    float lod = fruitLod(d.x, t0, pixel);
    vec4 lro = uPreviewWorldToLocal * (ro - center);
    vec4 lrd = uPreviewWorldToLocal * rd;
    vec4 nl;
    float t = fruitTrace(lro, lrd, d, t0, min(t1, dist), lod, nl).x;
    if (t <= 0.0) return col;
    vec4 ns = normalize(transpose(uPreviewWorldToLocal) * nl);
    float facing = abs(dot(ns, rd));
    float rim = pow(1.0 - facing, 2.0);
    float shade = 0.75 + 0.25 * dot(ns.xyz, normalize(vec3(0.4, 0.8, 0.3)));
    vec3 c = fruitBaseColor(uPreviewData.y) * 1.3 * shade;
    c = mix(c, vec3(1.0, 0.97, 0.9), 0.3 * rim);
    vec3 body = mix(col, c, alpha * (0.6 + 0.3 * rim));
    // A crisp cream silhouette about 2.5 px wide: the held fruit's outline is
    // solid, the off-slice hints are dotted, so the two never read alike.
    float radPx = d.x / (pixel * t);
    float k = clamp(sqrt(7.0 / max(radPx, 1.0)), 0.05, 0.9);
    float line = 1.0 - smoothstep(0.55 * k, k, facing);
    return mix(body, vec3(1.0, 0.96, 0.86), line * 0.92);
}

// ---------------------------------------------------------------- passes

// What a pixel's ray hit. kind: 0 sky, > 0 world material id,
// < 0 fruit -(index + 1).
struct Hit {
    float t;
    float kind;
    float lod;      // fruit detail
    vec4 ns;        // smooth normal
    float part;     // fruit part at the hit (FRUIT_SKIN ...)
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
    float fruitLodHit = 0.0, fruitPartHit = FRUIT_SKIN;
    vec4 fruitN = vec4(0.0);
    for (int i = 0; i < NO_UNROLL(MAX_FRUIT); ++i) {
        if (i >= uFruitCount) break;
        vec4 d = uFruitData[i];
        float t0, t1;
        if (!glomeSpan(ro, rd, uFruitCenters[i], d.x * FRUIT_BOUND, t0, t1) || t0 >= tHit) continue;
        float lod = fruitLod(d.x, t0, pixel);
        vec4 n;
        vec2 tp = fruitTrace(fruitLocalPoint(i, ro), fruitLocalVector(i, rd), d, t0, min(t1, tHit), lod, n);
        if (tp.x > 0.0 && tp.x < tHit) {
            tHit = tp.x; fruit = i; fruitLodHit = lod; fruitPartHit = tp.y;
            fruitN = n;
        }
    }

    // World, stopped early at the nearest fruit.
    Hit h;
    vec3 hs = marchStatic(ro, rd, min(tHit, FAR));
    vec3 hd = marchDynamic(ro, rd, hs.x);
    vec3 hw = hd.y > 0.0 ? hd : hs;

    // Nearest wins: the bowl is opaque, so a fruit behind its wall is hidden.
    if (fruit >= 0 && tHit <= hw.x) {
        h.t = tHit;
        h.kind = -float(fruit + 1);
        h.lod = fruitLodHit;
        h.part = fruitPartHit;
        h.ns = normalize(fruitWorldVector(fruit, fruitN));
    } else if (hw.y > 0.0) {
        h.t = hw.x;
        h.kind = hw.y;
        h.lod = 0.0;
        h.part = 0.0;
        h.ns = worldNormal(ro + rd * hw.x, hw.z, hw.y, hd.y > 0.0);
    } else {
        h.t = FAR;
        h.kind = 0.0;
        h.lod = 0.0;
        h.part = 0.0;
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

// What shows where nothing is hit. The review turntable uses a fixed studio
// backdrop instead of the sky, so only the subject changes as the slice turns.
vec3 background(vec2 px, vec4 rd) {
#ifdef TURNTABLE
    float y = px.y / uResolution.y;
    return mix(vec3(0.012, 0.012, 0.018), vec3(0.075, 0.05, 0.075), y);
#else
    vec3 sky = skyColor(rd);
    return mix(sky, vec3(dot(sky, vec3(0.299, 0.587, 0.114))), 0.18);
#endif
}

// Grade, linear in, display out: soft tone map, a little extra saturation.
vec3 grade(vec3 col) {
    col *= 0.85;
    col = col / (1.0 + col * 0.4);
    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    return clamp(mix(vec3(luma), col, 1.18), 0.0, 1.0);
}

// Final colour of a pixel from its hit and light (shadow, AO).
// Softness of this pixel for the present pass (0 sharp .. 1 fully softened),
// written to the scene's alpha as 1 - gSoft.
float gSoft = 0.0;

vec3 compose(vec2 px, vec4 ro, vec4 rd, Hit h, vec2 light) {
    vec3 col = vec3(0.0);
    gSoft = 0.0;
    float dist = h.t;
    if (h.kind < 0.0) {
        int fruit = int(-h.kind) - 1;
        vec4 p = ro + rd * h.t;
        vec4 c = uFruitCenters[fruit];
        vec4 d = uFruitData[fruit];
        vec4 q = fruitLocalPoint(fruit, p);
        vec4 ns = h.ns;
        vec4 n = facetBlend(ns, 2.5 + 2.5 * sqrt(d.x), d.w);
        // A little self-light keeps each fruit's colour identifiable even
        // when it is back-lit by the low sun.
        vec3 albedo = fruitAlbedo(q, normalize(fruitLocalVector(fruit, ns)), d.x, d.y, h.part, h.lod);
        // Plus a soft fill from the viewer, so the side we see is never just
        // the shadow side of a back-lit fruit.
        float fill = 0.35 * max(dot(n, -rd), 0.0);
        col = shade(p, n, ns, rd, albedo, fruitGloss(d.y), 0.0, light) + albedo * (0.18 + fill)
            + d.z * (uCalmFlash > 0.5 ? 0.3 * mix(albedo, vec3(1.0, 0.9, 0.7), 0.5) : vec3(1.0, 0.9, 0.7));
        // Same-kind glow: a pulsing light rim on the fruit itself (so it follows
        // the real silhouette), faster the closer its partner is in 4D.
        float glow = uFruitGlow[fruit] * uCues;
        if (glow > 0.0) {
            float rimG = pow(1.0 - abs(dot(n, rd)), 1.5);
            float pulse = 0.8 + 0.2 * sin(uTime * (3.0 + 6.0 * glow));
            col += mix(albedo, vec3(1.0, 0.93, 0.75), 0.55) * glow * pulse * (0.2 + 2.2 * rimG);
        }
#ifndef TURNTABLE
        float offset = dot(c - ro, uCameraBasis[3]);
        vec4 sectionNormal = normalize(p - c + offset * uCameraBasis[3]);
        // A ring of constant screen width (about 6 px) round the fruit's
        // cross-section, coloured by its depth: near the edge, cos^2 of the
        // angle to the view ray is about 2 * edge distance / section radius.
        float section = sqrt(max(d.x * d.x - offset * offset, 1e-4));
        float edge = 2.0 * (2.0 * uTanHalfFov / uResolution.y) * h.t / section;
        float cos2 = dot(sectionNormal, rd);
        cos2 *= cos2;
        float ring = 1.0 - smoothstep(6.0 * edge * 0.75, 6.0 * edge, cos2);
        float inner = smoothstep(6.0 * edge, 10.0 * edge, cos2) * (1.0 - smoothstep(10.0 * edge, 14.0 * edge, cos2));
        col = mix(col * (1.0 - 0.3 * inner * uCues), depthColor(offset, 0.7) * 1.15, 0.95 * ring * uCues);
#endif
    } else if (h.kind > 0.0) {
        vec4 p = ro + rd * h.t;
        vec4 n = facetBlend(h.ns, 6.0, 0.0);
        col = shade(p, n, h.ns, rd, materialColor(h.kind, p, n), h.kind == MAT_BOWL ? 0.25 : 0.0,
                    wrapOf(h.kind), light);
        if (h.kind == MAT_BOWL) {
            vec3 radial = normalize(p.xzw);
            float lipD = abs(p.y - bowlLipY(radial) + 0.027);
            float lip = 1.0 - smoothstep(0.018, 0.065 * (1.0 + 2.5 * uDanger), lipD);
            // Lit rim: the rounded top of the lip catches the low sun and the
            // sky as a crisp cream line, so the bowl's outline reads at a glance.
            float crisp = 1.0 - smoothstep(0.012, 0.03, lipD);
            float catchL = 0.8 + 0.5 * max(dot(n, uSunDir), 0.0);
            col = mix(col, vec3(1.0, 0.88, 0.70) * catchL * 1.25, 0.7 * crisp);
            // Danger: the rim turns red and pulses as the pile nears it. The
            // only glow on the bowl is this rim band (a third of the old body glow).
            float alarm = uDanger * (0.55 + 0.45 * sin(uTime * 7.0));
            col += mix(vec3(1.0, 0.72, 0.42), vec3(1.0, 0.10, 0.05), uDanger) * ((0.075 + 0.9 * alarm) * lip);
        }
    }

    // Sunset fog. The sky (noise clouds, not cheap) is evaluated once, and
    // only where it shows.
    float fog = dist >= FAR ? 1.0 : 1.0 - exp(-max(dist - uFogStart, 0.0) * 0.012);
    if (fog > 0.002) col = mix(col, background(px, rd), fog);

#ifndef TURNTABLE
    // Softened background: the sky and the world beyond the bowl's surroundings
    // lose some saturation and contrast here and are blurred in the present
    // pass, so the bowl and the fruit are what stays crisp.
    if (h.kind == 0.0) gSoft = uBgSoft;
    else if (h.kind > 0.0 && h.kind != MAT_BOWL)
        gSoft = uBgSoft * smoothstep(BOWL_R + 1.5, BOWL_R + 6.0, length((ro + rd * h.t).xyz));
    if (gSoft > 0.0) {
        float luma = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(col, mix(vec3(luma), col, 0.6) * 0.92 + vec3(0.035, 0.022, 0.025), gSoft);
    }
    vec3 beforeOverlay = col;
#endif

    col = drawHints(col, ro, rd, dist, h.kind == MAT_BOWL);
    col = drawBursts(col, ro, rd, dist, h.kind == MAT_BOWL);

    // Ghosts of the next drop (in the air) and where it lands: the fruit's own
    // SDF in its own orientation, translucent, so the outline is the real shape.
    if (uPreviewData.x > 0.0) {
        // Landing ghost first, then the one in the air; one call site (see marchStatic).
        for (int g = uLandCenter.w > -1e5 ? 0 : 1; g < NO_UNROLL(2); ++g)
            col = drawDropGhost(col, ro, rd, dist, g == 0 ? uLandCenter : uPreviewCenter, g == 0 ? 0.45 : 0.55);
    }

#ifndef TURNTABLE
    // Hints, bursts and the drop ghosts drawn over the background stay sharp.
    if (dot(abs(col - beforeOverlay), vec3(1.0)) > 0.01) gSoft = 0.0;
#endif
    // The cover art's darker edges.
    col = grade(col);
    vec2 v = px / uResolution - 0.5;
    col *= 1.0 - 0.55 * dot(v, v);
    return col;
}

Hit readGeometry(ivec2 texel) {
    vec4 g = texelFetch(uGeom, texel, 0);
    Hit h;
    h.t = g.x;
    h.kind = g.y;
    h.lod = g.z;
    h.part = g.w;
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
    for (int k = 0; k < NO_UNROLL(4); ++k) {
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

// ---------------------------------------------------------------- 4D eye
//
// A 4D camera sees the scene on a 3D retina, as a 3D eye sees it on a 2D one.
// Retina coordinates (u, v, s) in [-1, 1]^3 look along the camera's forward
// axis tilted by u right, v up and s along the slice normal, so s = 0 is
// exactly the slice view's image. Each texel is one 4D ray: whole fruit show
// at once (their 4D outline, not a cross-section), at every s where a ray hits
// them. Shading has no shadows; the world is the coarse occluder surface with
// its materials, faint in the volume so the fruit read through it.

const float EYE_WORLD = 0.008, EYE_BOWL = 0.02; // densities (fruit 1)

// nl, part: the local normal and part from fruitTrace.
vec4 eyeFruitColour(int i, vec4 ro, vec4 rd, float t, float lod, vec4 nl, float part) {
    vec4 p = ro + rd * t;
    vec4 d = uFruitData[i];
    vec4 q = fruitLocalPoint(i, p);
    vec4 ns = normalize(fruitWorldVector(i, nl));
    vec4 n = facetBlend(ns, 2.5 + 2.5 * sqrt(d.x), d.w);
    vec3 albedo = fruitAlbedo(q, normalize(nl), d.x, d.y, part, lod);
    float fill = 0.35 * max(dot(n, -rd), 0.0);
    return vec4(shade(p, n, ns, rd, albedo, fruitGloss(d.y), 0.0, vec2(1.0)) + albedo * (0.18 + fill) +
                d.z * vec3(1.0, 0.9, 0.7), 1.0);
}

// Colour (display space) and density seen along a 4D ray.
vec4 eyeRay(vec4 ro, vec4 rd) {
    float pixel = 2.0 * uTanHalfFov / uResolution.y;
    float tHit = FAR;
    int fruit = -1;
    float lodHit = 0.0, partHit = FRUIT_SKIN;
    vec4 nHit = vec4(0.0);
    for (int i = 0; i < NO_UNROLL(MAX_FRUIT); ++i) {
        if (i >= uFruitCount) break;
        vec4 d = uFruitData[i];
        float t0, t1;
        if (!glomeSpan(ro, rd, uFruitCenters[i], d.x * FRUIT_BOUND, t0, t1) || t0 >= tHit) continue;
        float lod = fruitLod(d.x, t0, pixel);
        vec4 n;
        vec2 tp = fruitTrace(fruitLocalPoint(i, ro), fruitLocalVector(i, rd), d, t0, min(t1, tHit), lod, n);
        if (tp.x > 0.0 && tp.x < tHit) { tHit = tp.x; fruit = i; lodHit = lod; partHit = tp.y; nHit = n; }
    }
    // The world's coarse surface, stopped at the fruit.
    float t = 0.0, tw = -1.0;
    for (int k = 0; k < NO_UNROLL(64); ++k) {
        float h = occluderSDF(ro + rd * t);
        if (h < 0.004 * t + 0.01) { tw = t; break; }
        t += h;
        if (t > min(tHit, 45.0)) break;
    }
    vec4 fc = fruit >= 0 ? eyeFruitColour(fruit, ro, rd, tHit, lodHit, nHit, partHit) : vec4(0.0);
    if (tw < 0.0) return fruit >= 0 ? vec4(grade(fc.rgb), 1.0) : vec4(0.0);
    vec4 p = ro + rd * tw;
    float mat = worldSDF(p).y;
    const float e = 0.02;
    float f[5]; // at p, then a step along each axis; one call site (see marchStatic)
    for (int k = 0; k < NO_UNROLL(5); ++k) {
        vec4 o = vec4(0.0);
        if (k > 0) o[k - 1] = e;
        f[k] = occluderSDF(p + o);
    }
    vec4 n = normalize(vec4(f[1], f[2], f[3], f[4]) - f[0]);
    vec3 wc = shade(p, n, n, rd, materialColor(mat, p, n), 0.0, wrapOf(mat), vec2(1.0));
    // The experimental retina volume uses a translucent bowl.
    if (mat == MAT_BOWL && fruit >= 0) return vec4(grade(mix(fc.rgb, wc, 0.3)), 1.0);
    return vec4(grade(wc), mat == MAT_BOWL ? EYE_BOWL : EYE_WORLD);
}

// Retina texel (texel coordinates) to its 4D ray direction.
vec4 eyeDir(vec3 texel) {
    vec3 r = texel / uRetinaSize * 2.0 - 1.0;
    float aspect = uRetinaSize.x / uRetinaSize.y;
    return uCameraBasis * normalize(vec4(r.x * aspect * uTanHalfFov, r.y * uTanHalfFov, -1.0, r.z * uTanHalfFov));
}

// The retina as a volume seen from a 3D display camera outside it: front to
// back through the box, density from each texel, over a plain backdrop with
// the box's edges drawn faintly.
vec3 eyeDisplay(vec2 px) {
    vec2 ndc = px / uResolution * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    const float tanDisplay = 0.36;
    float dist = 1.4 * max(uEyeBox.y, uEyeBox.x / aspect) / tanDisplay + uEyeBox.z;
    vec3 ro = uEyeView * vec3(0.0, 0.0, dist);
    vec3 rd = normalize(uEyeView * vec3(ndc.x * aspect * tanDisplay, ndc.y * tanDisplay, -1.0));
    vec3 col = mix(vec3(0.012, 0.012, 0.018), vec3(0.075, 0.05, 0.075), px.y / uResolution.y);
    col = pow(col, vec3(1.0 / 2.2));
    // Ray-box span.
    vec3 inv = 1.0 / rd;
    vec3 ta = (-uEyeBox - ro) * inv, tb = (uEyeBox - ro) * inv;
    vec3 tmin = min(ta, tb), tmax = max(ta, tb);
    float t0 = max(max(tmin.x, tmin.y), tmin.z), t1 = min(min(tmax.x, tmax.y), tmax.z);
    if (t1 <= max(t0, 0.0)) return col;
    // Edges: where the entry or exit point is near two faces at once.
    float pixel = 2.0 * tanDisplay / uResolution.y;
    vec3 a = abs(ro + rd * t0) / uEyeBox, b = abs(ro + rd * t1) / uEyeBox;
    float w0 = 1.5 * pixel * t0 / min(uEyeBox.x, uEyeBox.y), w1 = 1.5 * pixel * t1 / min(uEyeBox.x, uEyeBox.y);
    float e0 = step(1.0 - w0, a.x) + step(1.0 - w0, a.y) + step(1.0 - w0, a.z);
    float e1 = step(1.0 - w1, b.x) + step(1.0 - w1, b.y) + step(1.0 - w1, b.z);
    col += vec3(0.10, 0.09, 0.08) * step(2.0, e1);
    const int STEPS = 160;
    float dt = (t1 - t0) / float(STEPS);
    // Density per unit length along s: a fruit's outline is opaque across its
    // thickness in s, the world faint across the whole box.
    float sigma = 40.0 / uEyeBox.z;
    vec3 sum = vec3(0.0);
    float alpha = 0.0;
    // Per-pixel jitter (interleaved gradient noise) turns the layers' banding into fine grain.
    float jitter = fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
    for (int k = 0; k < NO_UNROLL(STEPS); ++k) {
        vec3 x = ro + rd * (t0 + (float(k) + jitter) * dt);
        vec4 c = texture(uRetina, x / uEyeBox * 0.5 + 0.5);
        float a = 1.0 - exp(-c.a * sigma * dt);
        sum += (1.0 - alpha) * a * c.rgb;
        alpha += (1.0 - alpha) * a;
        if (alpha > 0.98) break;
    }
    col = sum + (1.0 - alpha) * col;
    col += vec3(0.16, 0.14, 0.12) * step(2.0, e0);
    vec2 v = px / uResolution - 0.5;
    return col * (1.0 - 0.55 * dot(v, v));
}

// ---------------------------------------------------------------- main

void main() {
    vec2 px = gl_FragCoord.xy;
    vec4 ro = uCameraPos;
#if PASS == 1
    Hit h = traceScene(ro, rayDir(px));
    gGeom = vec4(h.t, h.kind, h.lod, h.part);
    gNormal = h.ns;
#elif PASS == 2
    // Each half-resolution texel lights the top-left pixel of its 2x2 block.
    ivec2 texel = min(ivec2(px) * 2, ivec2(uResolution) - 1);
    Hit h = readGeometry(texel);
    vec2 light = lightHit(ro, rayDir(vec2(texel) + 0.5), h);
    fragColor = vec4(light, h.t, 1.0);
#elif PASS == 4
    vec4 layer[4]; // one eyeRay call site (see marchStatic)
    for (int k = 0; k < NO_UNROLL(4); ++k) layer[k] = eyeRay(ro, eyeDir(vec3(px, uLayer + float(k) + 0.5)));
    out0 = layer[0];
    out1 = layer[1];
    out2 = layer[2];
    out3 = layer[3];
#elif PASS == 5
    fragColor = vec4(eyeDisplay(px), 1.0);
#elif PASS == 3
    ivec2 texel = ivec2(px);
    Hit h = readGeometry(texel);
    vec2 light = h.kind == 0.0 ? vec2(1.0) : upsampleLight(texel, h.t);
    fragColor = vec4(compose(px, ro, rayDir(px), h, light), 1.0 - gSoft);
#else
    vec4 rd = rayDir(px);
    Hit h = traceScene(ro, rd);
    fragColor = vec4(compose(px, ro, rd, h, lightHit(ro, rd, h)), 1.0 - gSoft);
#endif
#if defined(COST_MAP) && PASS != 1 && PASS != 4
    // Evaluations / 255 per channel: R static world, G moving world, B fruit.
    fragColor = vec4(vec3(gCost) / 255.0, 1.0);
#endif
}
