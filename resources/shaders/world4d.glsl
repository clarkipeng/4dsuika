// world4d.glsl: the 4D scene behind the raymarcher (see docs/4D.md).
//
// The renderer pastes this file into raymarch4d.fs in place of its #include, after
// the #version line, precision and its uniforms. Plain GLSL ES 3.00, no uniforms of
// its own; it reads uTime (seconds, drives drifting clouds) and uSunDir (unit vec4
// towards the sun) from the renderer.
//
// Contract:
//   vec2 worldSDF(vec4 p)                          x = signed distance, y = material id
//   vec3 skyColor(vec4 dir)                        radiance along a unit 4D direction
//   vec3 materialColor(float id, vec4 p, vec4 n)   albedo of a world material
//
// World units match physics: the bowl is centred at the origin with inner radius 2.9
// and outer 3.1, its rim at y = 0, opening up. y is up; the default slice is w = 0.
// Everything is built around the origin so turning the slice about the bowl always
// keeps the bowl in place while the island, trees and sky change around it.

// ---- material ids -------------------------------------------------------------
// 1 and 2 are fixed by the renderer (it lights the bowl's glow from id 2);
// 3 is the renderer's fruit; world props use 10 and up.
const float MAT_GRASS  = 1.0;
const float MAT_BOWL   = 2.0;
const float MAT_DIRT   = 10.0;
const float MAT_STONE  = 11.0;
const float MAT_BARK   = 12.0;
const float MAT_LEAVES = 13.0;
const float MAT_WATER  = 14.0;
const float MAT_WOOD   = 15.0;
const float MAT_CLOUD  = 16.0;
const float MAT_NONE   = 0.0;

// ---- geometry constants -------------------------------------------------------
const float BOWL_R      = 3.0;
const float BOWL_HALF   = 0.1;    // physics margin
const float GROUND_Y    = -2.35;  // grass level around the bowl
const float ISLAND_R    = 11.0;   // radius of the island top in (x, z, w)
const float ISLAND_BASE = -15.0;  // tip of the rocky underside

// The renderer lights in linear space and gamma-encodes at the end; colours here are
// authored in sRGB (picked from the cover) and converted on the way out.
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }

// ---- hashing and noise --------------------------------------------------------
// Sine-free hashes (Dave Hoskins): cheap and stable across GPUs.
float hash1(float n) { n = fract(n * 0.1031); n *= n + 33.33; n *= n + n; return fract(n); }
float hash3(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
float hash4(vec4 p) { p = fract(p * vec4(0.1031, 0.1030, 0.0973, 0.1099)); p += dot(p, p.wzxy + 33.33); return fract((p.x + p.y) * (p.z + p.w)); }

// Piecewise-linear noise on a 3D simplex grid: linear inside each tetrahedron,
// so a height field built from it is made of flat 4D facets. Slices of flat
// facets are flat facets, which gives the low-poly look at every rotation.
float facetNoise3(vec3 p) {
    const float F3 = 1.0 / 3.0;
    vec3 u = p + dot(p, vec3(F3));   // skewed coordinates
    vec3 s = floor(u);
    u -= s;                          // position inside the skewed cube, [0,1)^3
    // corners of the tetrahedron: walk the axes from largest to smallest component
    vec3 e = step(u.yzx, u.xyz);
    vec3 i1 = e * (1.0 - e.zxy);
    vec3 i2 = 1.0 - e.zxy * (1.0 - e);
    float a = max(max(u.x, u.y), u.z);
    float c = min(min(u.x, u.y), u.z);
    float m = u.x + u.y + u.z - a - c;
    return hash3(s) * (1.0 - a) + hash3(s + i1) * (a - m) + hash3(s + i2) * (m - c) + hash3(s + 1.0) * c;
}

// Smooth 4D value noise (for clouds and colour variation, not for shape facets).
float valueNoise4(vec4 p) {
    vec4 i = floor(p);
    vec4 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float r = 0.0;
    for (int k = 0; k < 16; k++) {
        vec4 o = vec4(float(k & 1), float((k >> 1) & 1), float((k >> 2) & 1), float((k >> 3) & 1));
        vec4 wgt = mix(1.0 - f, f, o);
        r += hash4(i + o) * wgt.x * wgt.y * wgt.z * wgt.w;
    }
    return r;
}

// ---- 4D primitives ------------------------------------------------------------
float sdGlome(vec4 p, float r) { return length(p) - r; }

float sdBox4(vec4 p, vec4 b) {
    vec4 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(max(q.x, q.y), max(q.z, q.w)), 0.0);
}

// Spherinder standing along y: a 3-ball of radius r in (x, z, w) swept over |y| < h.
float sdSpherinderY(vec4 p, float r, float h) {
    vec2 d = vec2(length(p.xzw) - r, abs(p.y) - h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// Spherinder along a segment a-b with radius tapering ra -> rb (rounded ends).
float sdLimb(vec4 p, vec4 a, vec4 b, float ra, float rb) {
    vec4 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - mix(ra, rb, h);
}

// Cheap fixed 4D rotation from a seed: three plane rotations whose cos/sin come from
// the rational parametrisation ((1-t^2), 2t) / (1+t^2), so no trig per call.
vec4 seedRotate(vec4 p, float seed) {
    vec3 t = vec3(hash1(seed * 3.1), hash1(seed * 5.7 + 1.0), hash1(seed * 7.3 + 2.0)) * 2.0 - 1.0;
    vec3 k = 1.0 / (1.0 + t * t);
    vec3 c = (1.0 - t * t) * k, s = 2.0 * t * k;
    p.xy = mat2(c.x, s.x, -s.x, c.x) * p.xy;
    p.zw = mat2(c.y, s.y, -s.y, c.y) * p.zw;
    p.xw = mat2(c.z, s.z, -s.z, c.z) * p.xw;
    return p;
}

// A faceted glome: the polytope cut by the 24 hyperplanes of the 24-cell, (+-1, +-1, 0, 0)
// and permutations, each pushed out by its own jitter. Every 3D slice of it is a faceted
// low-poly blob. The four sign combinations of each axis pair are evaluated as a vec4.
// `scale` stretches it per axis (rocks are squat, clouds are wide).
float gemPair(float a, float b, float r, vec4 j) {
    vec4 d = vec4(a + b, a - b, b - a, -a - b) * 0.70710678 - r * j;
    return max(max(d.x, d.y), max(d.z, d.w));
}
float sdGem(vec4 p, float r, float seed, vec4 scale) {
    vec4 q = seedRotate(p / scale, seed);
    vec4 h = fract(seed * vec4(0.7548777, 0.5698403, 0.3247180, 0.8191725) + vec4(0.13, 0.57, 0.31, 0.89));
    vec4 j0 = 0.86 + 0.22 * h;
    vec4 j1 = 0.86 + 0.22 * fract(h.yzwx * 1.618 + 0.29);
    vec4 j2 = 0.86 + 0.22 * fract(h.zwxy * 2.414 + 0.61);
    float d = gemPair(q.x, q.y, r, j0);
    d = max(d, gemPair(q.x, q.z, r, j1));
    d = max(d, gemPair(q.x, q.w, r, j2));
    d = max(d, gemPair(q.y, q.z, r, j2.wxyz));
    d = max(d, gemPair(q.y, q.w, r, j0.zwxy));
    d = max(d, gemPair(q.z, q.w, r, j1.yzwx));
    return d * min(min(scale.x, scale.y), min(scale.z, scale.w));
}

// ---- island -------------------------------------------------------------------
// Directions in (x, z, w) for the faceted rim of the island: icosahedron vertices and a
// twisted copy, 24 faces in all.
const vec3 ISLAND_FACES[12] = vec3[12](
    vec3(0.0, 0.5257, 0.8507), vec3(0.0, -0.5257, 0.8507), vec3(0.0, 0.5257, -0.8507), vec3(0.0, -0.5257, -0.8507),
    vec3(0.5257, 0.8507, 0.0), vec3(-0.5257, 0.8507, 0.0), vec3(0.5257, -0.8507, 0.0), vec3(-0.5257, -0.8507, 0.0),
    vec3(0.8507, 0.0, 0.5257), vec3(-0.8507, 0.0, 0.5257), vec3(0.8507, 0.0, -0.5257), vec3(-0.8507, 0.0, -0.5257));

// Faceted radial distance in (x, z, w). Each face pushes in and out with height in
// bands whose phase is its own, so the cliff breaks into staggered strata.
float islandRadial(vec3 q, float y) {
    float d = 0.0;
    for (int i = 0; i < 12; i++) {
        float fi = float(i);
        float yi = y * (0.8 + 0.4 * fract(fi * 0.618)) + 3.0 * fract(fi * 0.4142);
        float band = floor(yi), f = yi - band;
        vec2 jj = mix(vec2(hash1(fi * 7.13 + band * 3.71), hash1(fi * 2.9 + band * 1.3)),
                      vec2(hash1(fi * 7.13 + band * 3.71 + 3.71), hash1(fi * 2.9 + band * 1.3 + 1.3)), f);
        vec2 j = 0.9 + 0.16 * jj;
        vec3 n = ISLAND_FACES[i];
        d = max(d, dot(q, n) / j.x);
        d = max(d, dot(q, vec3(n.y, n.z, -n.x) * 0.8 + vec3(n.z, -n.x, n.y) * 0.6) / j.y);
    }
    return d;
}

// Hills that live only at some w: as the slice turns in xw or zw, these rise out of
// the grass and sink back. Each is (x, z, w, radius) with a height.
float hillField(vec3 q) {
    float h = 0.0;
    h += 2.6 * max(0.0, 1.0 - length(q - vec3(-6.0, -5.5,  5.5)) / 4.5);
    h += 3.2 * max(0.0, 1.0 - length(q - vec3( 5.5, -6.0, -6.0)) / 5.0);
    h += 1.8 * max(0.0, 1.0 - length(q - vec3(-1.5,  6.5, -6.5)) / 4.0);
    h += 1.2 * max(0.0, 1.0 - length(q - vec3( 7.0, -5.0,  0.5)) / 3.5);  // visible at w = 0
    return h;
}

float pondDist(vec3 q) {
    // the pond: a flattened 3-ball in (x, z, w) in front of the bowl
    vec3 d = (q - vec3(-3.2, 5.0, 0.6)) / vec3(3.4, 2.3, 2.6);
    return (length(d) - 1.0) * 2.3;
}

// the pond's water level sits a little below the grass around it
const float WATER_Y = GROUND_Y - 0.55;

float groundHeight(vec3 q) {
    float r = length(q);
    float h = GROUND_Y + 0.35 * facetNoise3(q * 0.42) + hillField(q);
    // a gentle crown so the edge falls away
    h -= 0.008 * r * r;
    // flatten under and around the bowl
    float bowlPad = smoothstep(3.6, 6.0, length(q));
    h = mix(GROUND_Y, h, bowlPad);
    // the pond basin
    float pd = pondDist(q);
    h = mix(h, min(h, WATER_Y - 0.6), smoothstep(0.5, -0.6, pd));
    return h;
}


vec2 islandSDF(vec4 p) {
    vec3 q = p.xzw;
    // Top surface (height field, Lipschitz-corrected). The ground never dips more
    // than 1.2 below GROUND_Y, so deep under it the top can't be the nearest face.
    float gh = GROUND_Y - 1.2;
    float top = -1e5;
    if (p.y > GROUND_Y - 2.5) {
        gh = groundHeight(q);
        top = (p.y - gh) * 0.65;   // 1 / sqrt(1 + steepest slope^2)
    }
    // Faceted side wall that narrows toward the tip of the underside.
    float depth = clamp((GROUND_Y - p.y) / (GROUND_Y - ISLAND_BASE), 0.0, 1.0);
    float rBase = ISLAND_R * pow(max(1.0 - pow(depth, 1.7), 0.0), 0.75);
    // islandRadial(q) <= |q| / 0.9 and the radius below is >= rBase - 1.6 depth, so
    // when even that bound is inside the top surface the wall can be skipped.
    // The wall's gradient reaches about 1/0.9 from the faces plus 0.7 from the facet
    // noise, so its distance is scaled by 0.55 to stay a safe underestimate.
    float side = (length(q) / 0.9 - rBase + 1.6 * depth) * 0.55;
    if (side > top) {
        float yb = p.y * 0.6;
        float bf = floor(yb);
        float jag = mix(hash1(bf * 1.7), hash1(bf * 1.7 + 1.7), yb - bf);
        float radius = rBase + 0.9 * jag * depth;
        // small chunky facets: piecewise-linear noise over (x, z, w, y) keeps faces flat
        radius -= 1.6 * depth * facetNoise3(q * 0.42 + vec3(0.0, p.y * 0.55, p.y * 0.3));
        side = (islandRadial(q, yb) - radius) * 0.55;
    }
    // the bowl sits in a socket: nothing of the island inside the bowl's glome
    float d = max(max(top, side), BOWL_R + BOWL_HALF + 0.04 - length(p));
    float m = (p.y > gh - 0.75) ? MAT_GRASS : MAT_DIRT;
    return vec2(d, m);
}

vec2 pondSDF(vec4 p) {
    float d = max(max(p.y - WATER_Y, WATER_Y - 1.2 - p.y), pondDist(p.xzw) + 0.1);
    return vec2(d, MAT_WATER);
}

// ---- bowl ---------------------------------------------------------------------
float bowlSDF(vec4 p) {
    if (p.y > 0.0) {
        // rounded rim: distance to the 2-sphere |xzw| = R at y = 0
        return length(vec2(length(p.xzw) - BOWL_R, p.y)) - BOWL_HALF;
    }
    return abs(length(p) - BOWL_R) - BOWL_HALF;
}

// ---- rocks --------------------------------------------------------------------
// (x, z, w) on the ground, size, seed. Some sit at w far from 0: rotate to find them.
vec2 rocksSDF(vec4 p, float best) {
    float d = best;
    vec4 R[9] = vec4[9](
        vec4( 7.4,  2.4,  0.0, 1.25), vec4( 5.4,  4.6,  0.4, 0.6),
        vec4(-6.2,  1.6, -0.3, 0.55), vec4(-6.8,  3.8,  0.2, 0.45),
        vec4( 2.0,  7.4, -0.5, 0.5),  vec4( 3.6, -3.0,  5.2, 1.4),
        vec4(-4.0,  2.0, -5.8, 1.6),  vec4( 0.5, -7.0,  6.8, 1.1),
        vec4(-7.5, -1.0,  4.5, 0.9));
    for (int i = 0; i < 9; i++) {
        vec4 r = R[i];
        // bound first (the ground under a rock lies within 4 of GROUND_Y), then place it
        vec4 q = p - vec4(r.x, GROUND_Y, r.y, r.z);
        if (length(vec4(q.x, max(abs(q.y + 0.5) - 4.0, 0.0), q.zw)) - 1.8 * r.w > d) continue;
        q.y -= groundHeight(vec3(r.x, r.y, r.z)) - GROUND_Y + 0.15 * r.w;
        d = min(d, sdGem(q, r.w, float(i) + 3.0, vec4(1.15, 0.75, 1.0, 1.0)));
    }
    return vec2(d, MAT_STONE);
}

// ---- trees --------------------------------------------------------------------
// A tree: spherinder trunk and limbs, canopy of faceted glomes. Returns (d, mat).
// Callers skip it when the bound glome (centre base + (0, 6s, 0, 0), radius 9s) is
// farther than their best hit.
vec2 treeSDF(vec4 p, vec4 base, float s, float seed) {
    vec4 q = (p - base) / s;
    // trunk: a leaning spherinder limb flared into roots, splitting into two boughs.
    // Wood is evaluated with w squashed, so it is wider along w than the canopy and a
    // turned slice shows the trunk thinning but never a canopy floating on nothing.
    vec4 qq = q;
    q.w *= 0.25;
    float trunk = sdLimb(q, vec4(0.0, -0.6, 0.0, 0.0), vec4(-0.3, 5.4, 0.0, 0.1), 0.8, 0.45);
    trunk = min(trunk, sdLimb(q, vec4(0.0, -0.4, 0.0, 0.0), vec4(1.2, -0.1, 0.4, 0.0), 0.55, 0.12));
    trunk = min(trunk, sdLimb(q, vec4(0.0, -0.4, 0.0, 0.0), vec4(-1.0, -0.2, -0.6, 0.4), 0.5, 0.12));
    trunk = min(trunk, sdLimb(q, vec4(0.0, -0.4, 0.0, 0.0), vec4(0.1, -0.2, 0.3, -1.1), 0.5, 0.12));
    trunk = min(trunk, sdLimb(q, vec4(-0.3, 5.2, 0.0, 0.1), vec4(-3.0, 8.0, 0.4, -0.2), 0.4, 0.18));
    trunk = min(trunk, sdLimb(q, vec4(-0.3, 4.8, 0.0, 0.1), vec4(2.6, 7.4, -0.2, 0.3), 0.38, 0.16));
    trunk = min(trunk, sdLimb(q, vec4(-0.3, 5.3, 0.0, 0.1), vec4(-0.2, 9.4, -0.5, 1.0), 0.32, 0.14));
    trunk = min(trunk, sdLimb(q, vec4(1.0, 6.0, -0.1, 0.4), vec4(1.6, 8.3, 0.4, -1.2), 0.18, 0.08));
    q = qq;
    // canopy: big faceted glomes at the bough ends, one reaching off into w
    vec4 sq = vec4(1.0, 0.82, 1.0, 0.55);   // thinner in w than the wood
    float can = sdGem(q - vec4(-3.4, 9.0, 0.4, -0.3), 2.4, seed + 1.0, sq);
    can = min(can, sdGem(q - vec4(2.9, 8.3, -0.2, 0.4), 2.45, seed + 2.0, sq));
    can = min(can, sdGem(q - vec4(-0.3, 10.6, -0.6, 1.0), 2.7, seed + 3.0, sq));
    can = min(can, sdGem(q - vec4(0.6, 9.2, 1.4, -0.8), 1.9, seed + 4.0, sq));
    return (trunk < can) ? vec2(trunk * s, MAT_BARK) : vec2(can * s, MAT_LEAVES);
}

// ---- fence --------------------------------------------------------------------
vec2 fenceSDF(vec4 p) {
    vec4 c = vec4(-7.4, GROUND_Y, -1.6, 0.0);
    vec4 q = p - c;
    // rotate the fence to face the camera a bit
    q.xz = mat2(0.94, 0.34, -0.34, 0.94) * q.xz;
    float d = sdBox4(q - vec4(-1.3, 0.85, 0.0, 0.0), vec4(0.17, 1.05, 0.17, 0.3));
    d = min(d, sdBox4(q - vec4(1.3, 0.75, 0.0, 0.0), vec4(0.16, 0.95, 0.16, 0.3)));
    d = min(d, sdBox4(q - vec4(0.0, 1.15, 0.0, 0.0), vec4(1.9, 0.1, 0.09, 0.22)));
    d = min(d, sdBox4(q - vec4(-0.2, 0.55, 0.0, 0.0), vec4(1.7, 0.09, 0.08, 0.22)));
    return vec2(d, MAT_WOOD);
}

// ---- clouds -------------------------------------------------------------------
// A sea of faceted cloud banks drifting below and around the island, spread through
// x, z and w so every slice shows a different set.
vec2 cloudsSDF(vec4 p, float best) {
    float t = uTime * 0.02;
    float d = best;
    for (int i = 0; i < 10; i++) {
        float fi = float(i);
        float ang = fi * 0.628 + t;
        float rad = 30.0 + 10.0 * hash1(fi * 3.1);
        vec4 c = vec4(rad * cos(ang), -17.0 + 4.0 * hash1(fi * 5.3), rad * sin(ang),
                      14.0 * sin(fi * 1.7 + t * 0.7));
        vec4 q = p - c;
        if (length(q) - 12.5 > d) continue;
        float cl = sdGem(q, 4.5, fi + 20.0, vec4(1.7, 0.7, 1.4, 1.3));
        cl = min(cl, sdGem(q - vec4(4.5, 1.0, 1.0, 0.5), 3.4, fi + 40.0, vec4(1.3, 0.8, 1.2, 1.1)));
        cl = min(cl, sdGem(q - vec4(-4.8, 0.4, -1.0, -0.8), 3.0, fi + 60.0, vec4(1.3, 0.75, 1.2, 1.1)));
        d = min(d, cl);
    }
    return vec2(d, MAT_CLOUD);
}

// ---- scene --------------------------------------------------------------------
vec2 opU(vec2 a, vec2 b) { return (a.x < b.x) ? a : b; }

// Every part is skipped when a lower bound on its distance is already farther than the
// best hit so far. Bounds never stand in for distances: soft shadows read distances
// as near-misses, so a loose bound would draw false penumbrae.
vec2 worldSDF(vec4 p) {
    vec2 r = vec2(bowlSDF(p), MAT_BOWL);
    r = opU(r, pondSDF(p));
    if (length(p - vec4(0.0, -6.0, 0.0, 0.0)) - 13.5 < r.x) r = opU(r, islandSDF(p));
    if (length(p - vec4(-7.4, GROUND_Y + 1.0, -1.6, 0.0)) - 3.0 < r.x) r = opU(r, fenceSDF(p));
    vec2 rk = rocksSDF(p, r.x);
    if (rk.x < r.x) r = rk;
    // the main tree, and two that exist only far along w: rotate in xw or zw to find them
    vec4 t0 = vec4(5.6, GROUND_Y, -3.2, 0.0);
    vec4 t1 = vec4(-4.5, GROUND_Y + 0.3, -5.0, 6.2);
    vec4 t2 = vec4(4.0, GROUND_Y + 0.2, 4.5, 5.5);
    if (length(p - t0 - vec4(0.0, 6.0, 0.0, 0.0) * 0.85) - 9.0 * 0.85 < r.x) r = opU(r, treeSDF(p, t0, 0.85, 0.0));
    if (length(p - t1 - vec4(0.0, 6.0, 0.0, 0.0) * 0.7) - 9.0 * 0.7 < r.x) r = opU(r, treeSDF(p, t1, 0.7, 10.0));
    if (length(p - t2 - vec4(0.0, 6.0, 0.0, 0.0) * 0.5) - 9.0 * 0.5 < r.x) r = opU(r, treeSDF(p, t2, 0.5, 20.0));
    if (p.y + 7.0 < r.x) {
        vec2 cl = cloudsSDF(p, r.x);
        if (cl.x < r.x) r = cl;
    }
    return r;
}

// ---- sky ----------------------------------------------------------------------
// Sunset gradient over elevation; puffy clouds from 4D noise on the direction, so
// turning in w changes the clouds too.
// The horizon glow sits just below eye level; the cloud sea fills everything below it.
vec3 skySrgb(vec4 dir) {
    float y = dir.y + 0.04;
    vec3 zenith  = vec3(0.27, 0.19, 0.29);
    vec3 high    = vec3(0.50, 0.25, 0.30);
    vec3 mid     = vec3(0.78, 0.32, 0.21);
    vec3 horizon = vec3(0.95, 0.47, 0.18);
    vec3 low     = vec3(0.40, 0.26, 0.31);
    vec3 deep    = vec3(0.13, 0.16, 0.22);
    vec3 c;
    if (y > 0.3)        c = mix(high, zenith, smoothstep(0.3, 0.8, y));
    else if (y > 0.1)   c = mix(mid, high, smoothstep(0.1, 0.3, y));
    else if (y > 0.0)   c = mix(horizon, mid, smoothstep(0.0, 0.1, y));
    else if (y > -0.2)  c = mix(low, horizon, pow(smoothstep(-0.2, 0.0, y), 2.0));
    else                c = mix(deep, low, smoothstep(-0.6, -0.2, y));

    // sun glow and a soft-edged disc
    float s = max(dot(dir, uSunDir), 0.0);
    c += vec3(1.0, 0.50, 0.16) * pow(s, 6.0) * 0.35;
    c += vec3(1.0, 0.62, 0.22) * pow(s, 80.0) * 0.5;
    c = mix(c, vec3(1.0, 0.72, 0.30), smoothstep(0.9982, 0.9988, s));

    // puffy clouds: 4D noise on the direction, stepped for a flat painted look, so
    // turning in w changes the clouds too
    float t = uTime * 0.004;
    vec4 cp = dir * vec4(2.6, 6.5, 2.6, 2.6) + vec4(t, 0.0, -t * 0.5, t * 0.3);
    float n = valueNoise4(cp) * 0.6 + valueNoise4(cp * 2.3) * 0.3 + valueNoise4(cp * 5.0) * 0.1;
    float band = smoothstep(0.02, 0.1, y) * smoothstep(0.75, 0.25, y);
    float cov = smoothstep(0.52, 0.55, n) * band;
    vec3 cloudLit = mix(vec3(0.58, 0.27, 0.27), vec3(0.88, 0.42, 0.28), smoothstep(0.6, 0.98, s));
    c = mix(c, cloudLit, cov * 0.8);
    // the cloud sea below the horizon: dusky purple-blue, lighter tops
    float sn = valueNoise4(dir * vec4(5.0, 9.0, 5.0, 5.0) + vec4(0.0, 0.0, t, -t)) * 0.7
             + valueNoise4(dir * 12.0) * 0.3;
    float sea = smoothstep(0.45, 0.5, sn) * smoothstep(-0.02, -0.12, y);
    c = mix(c, mix(vec3(0.22, 0.23, 0.31), vec3(0.42, 0.30, 0.36), smoothstep(-0.4, -0.05, y)), sea * 0.7);
    return c;
}

vec3 skyColor(vec4 dir) { return toLinear(skySrgb(dir));
}

// ---- materials ----------------------------------------------------------------
// Albedo per material, sampled from footage/gameImage.png.
vec3 materialSrgb(float id, vec4 p) {
    float v = facetNoise3(p.xzw * 0.9 + p.y * 0.37);   // tint variation that follows the facets
    if (id == MAT_GRASS)  return mix(vec3(0.17, 0.27, 0.07), vec3(0.27, 0.34, 0.08), v);
    if (id == MAT_BOWL)   return vec3(0.96, 0.74, 0.52);
    if (id == MAT_DIRT)   return mix(vec3(0.34, 0.15, 0.05), vec3(0.46, 0.21, 0.07), v);
    if (id == MAT_STONE)  return mix(vec3(0.17, 0.22, 0.27), vec3(0.24, 0.29, 0.33), v);
    if (id == MAT_BARK)   return vec3(0.33, 0.15, 0.06);
    if (id == MAT_LEAVES) return mix(vec3(0.10, 0.24, 0.12), vec3(0.17, 0.31, 0.12), v);
    if (id == MAT_WATER)  return vec3(0.07, 0.20, 0.20);
    if (id == MAT_WOOD)   return vec3(0.30, 0.17, 0.09);
    if (id == MAT_CLOUD)  return vec3(0.55, 0.42, 0.50);
    return vec3(1.0, 0.0, 1.0);
}

vec3 materialColor(float id, vec4 p, vec4 n) { return toLinear(materialSrgb(id, p));
}
