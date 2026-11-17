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
uniform vec4  uFruitData[MAX_FRUIT]; // radius, type, flash, unused
uniform int   uHintCount;            // fruit just off the slice
uniform vec4  uHintCenters[MAX_HINTS]; // centre projected into the slice
uniform vec4  uHintData[MAX_HINTS];  // radius, type, signed gap to the slice, strength 0..1
uniform vec4  uPreviewCenter;        // ghost of the next drop
uniform vec4  uPreviewData;          // radius (0 = none), type
uniform float uFacet;                // low-poly facet strength, 0 = smooth

#include "world4d.glsl"

const float FAR = 80.0;
const int   MAX_STEPS = 128;

const float MAT_FRUIT = 3.0;

// ---------------------------------------------------------------- fruit

vec3 fruitColor(float type) {
    int t = int(type + 0.5);
    if (t == 0)  return vec3(0.80, 0.08, 0.15); // cherry
    if (t == 1)  return vec3(0.95, 0.25, 0.30); // strawberry
    if (t == 2)  return vec3(0.55, 0.30, 0.85); // grape
    if (t == 3)  return vec3(1.00, 0.62, 0.15); // dekopon
    if (t == 4)  return vec3(1.00, 0.45, 0.10); // persimmon
    if (t == 5)  return vec3(0.85, 0.12, 0.10); // apple
    if (t == 6)  return vec3(0.90, 0.88, 0.45); // pear
    if (t == 7)  return vec3(1.00, 0.70, 0.70); // peach
    if (t == 8)  return vec3(0.95, 0.80, 0.25); // pineapple
    if (t == 9)  return vec3(0.65, 0.90, 0.45); // melon
    return vec3(0.20, 0.60, 0.25);              // watermelon
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

float fruitSDF(vec4 p) {
    float d = FAR;
    for (int i = 0; i < MAX_FRUIT; ++i) {
        if (i >= uFruitCount) break;
        d = min(d, length(p - uFruitCenters[i]) - uFruitData[i].x);
    }
    return d;
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

// density: facets across the surface; higher = smaller facets.
vec4 facetNormal(vec4 n, float density) {
    vec4 q = d4Round(n * density);
    return dot(q, q) > 0.0 ? normalize(q) : n;
}

// ---------------------------------------------------------------- scene

float sceneSDF(vec4 p) { return min(worldSDF(p).x, fruitSDF(p)); }

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

// Soft shadow marched through the 4D scene towards the sun.
float softShadow(vec4 ro, vec4 rd) {
    float res = 1.0;
    float t = 0.05;
    for (int i = 0; i < 48; ++i) {
        float h = sceneSDF(ro + rd * t);
        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.03, 0.6);
        if (res < 0.01 || t > 25.0) break;
    }
    return clamp(res, 0.0, 1.0);
}

float ambientOcclusion(vec4 p, vec4 n) {
    float occ = 0.0, w = 1.0;
    for (int i = 1; i <= 5; ++i) {
        float h = 0.06 * float(i * i);
        occ += (h - sceneSDF(p + n * h)) * w;
        w *= 0.6;
    }
    return clamp(1.0 - 1.4 * occ, 0.0, 1.0);
}

// Normal used for lighting: smooth normal ns bent towards its facet normal.
vec4 facetBlend(vec4 ns, float density) {
    return uFacet > 0.0 ? normalize(mix(ns, facetNormal(ns, density), uFacet)) : ns;
}

const vec3 SUN_COLOR = vec3(2.1, 1.25, 0.62);
const vec3 SKY_AMBIENT = vec3(0.30, 0.21, 0.38);
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
        col = mix(col, mix(fruitColor(d.y), side, 0.35) * 1.3, clamp(a, 0.0, 0.8));
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

    // Fruit: exact ray-glome hits (only fruit crossing the slice are sent).
    float tHit = FAR;
    int fruit = -1;
    for (int i = 0; i < MAX_FRUIT; ++i) {
        if (i >= uFruitCount) break;
        float t = glomeHit(ro, rd, uFruitCenters[i], uFruitData[i].x);
        if (t > 0.0 && t < tHit) { tHit = t; fruit = i; }
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
        vec4 ns = normalize(p - c);
        vec4 n = facetBlend(ns, 2.5 + 2.5 * sqrt(d.x));
        col = shade(p, n, ns, rd, fruitColor(d.y), 0.5) + d.z * vec3(1.0, 0.9, 0.7);
        dist = tHit;
    } else if (mat > 0.0) {
        vec4 p = ro + rd * t;
        vec4 ns = worldNormal(p);
        vec4 n = facetBlend(ns, 6.0);
        col = shade(p, n, ns, rd, materialColor(mat, p, n), mat == 2.0 ? 0.25 : 0.0);
        dist = t;
    } else {
        col = skyColor(rd);
        dist = FAR;
    }

    // Sunset fog.
    float fog = 1.0 - exp(-max(dist - 16.0, 0.0) * 0.018);
    col = mix(col, skyColor(rd), fog);

    // The bowl glows softly, as in the cover art.
    col += vec3(1.0, 0.72, 0.42) * 0.22 * exp(-bowlNear * 5.0);

    col = drawHints(col, ro, rd, dist);

    // Ghost of the next drop, blended over the scene.
    if (uPreviewData.x > 0.0) {
        float tp = glomeHit(ro, rd, uPreviewCenter, uPreviewData.x);
        if (tp > 0.0 && tp < dist) {
            vec4 n = normalize(ro + rd * tp - uPreviewCenter);
            float f = 0.35 + 0.4 * pow(1.0 - abs(dot(n, rd)), 2.0);
            col = mix(col, fruitColor(uPreviewData.y) * 1.2, f);
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
