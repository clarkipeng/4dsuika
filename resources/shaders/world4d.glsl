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

// A faceted glome: the polytope cut by the 24 hyperplanes of the 24-cell, (+-1, +-1, 0, 0)
// and permutations, each pushed out by its own jitter. Every 3D slice of it is a faceted
// low-poly blob. The four sign combinations of each axis pair are evaluated as a vec4.
// `scale` stretches it per axis (rocks are squat, canopies thin in w). Its vertices lie
// within GEM_REACH * r of the centre (24-cell circumradius sqrt(2), jitter up to 1.08),
// which is what callers bound it by.
const float GEM_REACH = 1.53;

// Each gem g has a fixed random 4D rotation GEM_ROT[g] and jitters GEM_JIT[3g .. 3g+2],
// baked once (rational-parametrised plane rotations and golden-ratio jitter) so a gem
// costs one matrix multiply and 24 planes. Gems 0-8 are the rocks; 9 + 2k and 10 + 2k
// are the two halves of canopy k.
// Always index these tables with constants (unrolled code, as below): indexing them
// with a loop counter makes the GPU copy them per call, which cost ~10 ms a frame.
const mat4 GEM_ROT[17] = mat4[17](
    mat4(0.0510, 0.9022, 0.0000, 0.4282, -0.1067, 0.4313, 0.0000, -0.8959, 0.3680, 0.0000, 0.9288, -0.0438, -0.9223, 0.0000, 0.3706, 0.1098),
    mat4(0.2229, -0.9468, 0.0000, 0.2321, 0.6559, 0.3218, 0.0000, 0.6828, 0.7184, 0.0000, 0.0884, -0.6900, -0.0638, 0.0000, 0.9961, 0.0613),
    mat4(0.2801, -0.3023, 0.0000, 0.9111, 0.0888, 0.9532, 0.0000, 0.2890, -0.8754, 0.0000, 0.4015, 0.2691, -0.3838, 0.0000, -0.9159, 0.1180),
    mat4(0.3647, -0.3780, 0.0000, 0.8510, 0.1489, 0.9258, 0.0000, 0.3475, -0.8806, 0.0000, 0.2866, 0.3774, -0.2634, 0.0000, -0.9580, 0.1129),
    mat4(0.1258, 0.9539, 0.0000, 0.2726, -0.3998, 0.3002, 0.0000, -0.8660, -0.7768, 0.0000, 0.5177, 0.3586, -0.4700, 0.0000, -0.8556, 0.2170),
    mat4(0.3828, -0.8467, 0.0000, -0.3696, 0.6091, 0.5321, 0.0000, -0.5881, -0.6835, 0.0000, 0.1782, -0.7079, 0.1237, 0.0000, 0.9840, 0.1282),
    mat4(0.4765, 0.8792, 0.0000, 0.0047, -0.8791, 0.4765, 0.0000, -0.0086, -0.0072, 0.0000, 0.6767, 0.7362, -0.0066, 0.0000, -0.7362, 0.6767),
    mat4(0.1762, -0.2086, 0.0000, -0.9620, 0.0376, 0.9780, 0.0000, -0.2052, -0.9027, 0.0000, 0.3973, -0.1653, 0.3908, 0.0000, 0.9177, 0.0716),
    mat4(0.4300, -0.6270, 0.0000, -0.6497, 0.3460, 0.7790, 0.0000, -0.5228, -0.8321, 0.0000, 0.0660, -0.5507, 0.0550, 0.0000, 0.9978, 0.0364),
    mat4(0.3413, -0.6114, 0.0000, 0.7140, 0.2637, 0.7913, 0.0000, 0.5516, -0.1767, 0.0000, 0.9806, 0.0845, -0.8848, 0.0000, -0.1958, 0.4229),
    mat4(0.1223, -0.8201, 0.0000, 0.5590, 0.1753, 0.5722, 0.0000, 0.8011, -0.9172, 0.0000, 0.3442, 0.2007, -0.3362, 0.0000, -0.9389, 0.0736),
    mat4(0.0131, 0.9990, 0.0000, -0.0417, -0.2996, 0.0438, 0.0000, 0.9531, -0.9032, 0.0000, 0.3218, -0.2839, 0.3070, 0.0000, 0.9468, 0.0965),
    mat4(0.9024, -0.4209, 0.0000, 0.0919, 0.4187, 0.9071, 0.0000, 0.0426, -0.0701, 0.0000, 0.7221, 0.6883, -0.0731, 0.0000, -0.6918, 0.7184),
    mat4(0.0510, 0.9022, 0.0000, 0.4282, -0.1067, 0.4313, 0.0000, -0.8959, 0.3680, 0.0000, 0.9288, -0.0438, -0.9223, 0.0000, 0.3706, 0.1098),
    mat4(0.4343, -0.7733, 0.0000, -0.4619, 0.5297, 0.6340, 0.0000, -0.5634, 0.2445, 0.0000, 0.9420, 0.2298, 0.6863, 0.0000, -0.3355, 0.6453),
    mat4(0.2229, -0.9468, 0.0000, 0.2321, 0.6559, 0.3218, 0.0000, 0.6828, 0.7184, 0.0000, 0.0884, -0.6900, -0.0638, 0.0000, 0.9961, 0.0613),
    mat4(0.0045, -0.9999, 0.0000, -0.0154, 0.2797, 0.0160, 0.0000, -0.9600, 0.6810, 0.0000, 0.7049, 0.1984, 0.6768, 0.0000, -0.7093, 0.1972));
const vec4 GEM_JIT[51] = vec4[51](
    vec4(0.9468, 0.9215, 0.9225, 0.9365), vec4(1.0233, 1.0249, 1.0475, 1.0643), vec4(0.9251, 0.9588, 0.9838, 0.9226),
    vec4(0.8929, 1.0469, 0.9940, 0.8967), vec4(1.0061, 0.9205, 0.9831, 0.9770), vec4(0.8776, 0.8627, 1.0736, 1.0053),
    vec4(1.0590, 0.9522, 1.0654, 1.0769), vec4(1.0730, 1.0361, 1.0547, 1.0257), vec4(1.0500, 1.0778, 1.0345, 0.9968),
    vec4(1.0050, 1.0776, 0.9168, 1.0371), vec4(1.0559, 1.0157, 0.9904, 0.9385), vec4(0.9114, 0.9817, 0.9043, 1.0795),
    vec4(0.9511, 0.9830, 0.9883, 0.9973), vec4(0.9027, 0.9113, 0.9260, 1.0712), vec4(0.8638, 0.8857, 0.9941, 1.0710),
    vec4(0.8972, 0.8883, 1.0597, 0.9575), vec4(0.9696, 1.0269, 0.8616, 0.9840), vec4(1.0363, 1.0097, 0.8640, 1.0626),
    vec4(1.0633, 1.0137, 0.9111, 0.9178), vec4(0.9525, 1.0065, 1.0173, 1.0327), vec4(0.8977, 0.9136, 1.0449, 0.9252),
    vec4(1.0093, 0.9190, 0.9826, 0.8780), vec4(1.0193, 0.9021, 0.9529, 0.9454), vec4(1.0701, 1.0376, 0.9147, 0.9167),
    vec4(0.9554, 1.0444, 1.0540, 1.0582), vec4(1.0022, 1.0177, 1.0245, 1.0782), vec4(1.0226, 1.0326, 1.0045, 0.9994),
    vec4(1.0547, 0.8908, 0.9996, 1.0160), vec4(0.9736, 0.9297, 0.9562, 1.0188), vec4(0.8913, 0.9308, 1.0241, 1.0685),
    vec4(0.8961, 0.9371, 1.0261, 0.8627), vec4(1.0486, 0.9725, 0.9282, 0.9822), vec4(0.9551, 1.0007, 0.8614, 0.9604),
    vec4(1.0007, 1.0161, 1.0711, 0.9762), vec4(0.9564, 1.0453, 0.8919, 0.9315), vec4(1.0637, 1.0548, 0.8940, 0.9311),
    vec4(1.0622, 1.0625, 0.8775, 1.0429), vec4(1.0315, 0.9521, 0.9998, 1.0309), vec4(1.0365, 0.9958, 1.0423, 1.0431),
    vec4(0.9468, 0.9215, 0.9225, 0.9365), vec4(1.0233, 1.0249, 1.0475, 1.0643), vec4(0.9251, 0.9588, 0.9838, 0.9226),
    vec4(1.0083, 0.9679, 0.9489, 1.0031), vec4(0.8783, 1.0677, 0.9354, 0.9437), vec4(0.9889, 0.8997, 0.9121, 1.0346),
    vec4(0.8929, 1.0469, 0.9940, 0.8967), vec4(1.0061, 0.9205, 0.9831, 0.9770), vec4(0.8776, 0.8627, 1.0736, 1.0053),
    vec4(0.9543, 0.8732, 1.0204, 0.9634), vec4(0.9452, 0.9633, 0.8710, 1.0764), vec4(0.9414, 1.0237, 1.0019, 1.0262));

float gemPair(float a, float b, float r, vec4 j) {
    vec4 d = vec4(a + b, a - b, b - a, -a - b) * 0.70710678 - r * j;
    return max(max(d.x, d.y), max(d.z, d.w));
}
float sdGem(vec4 p, float r, int g, vec4 scale) {
    vec4 q = GEM_ROT[g] * (p / scale);
    vec4 j0 = GEM_JIT[3 * g], j1 = GEM_JIT[3 * g + 1], j2 = GEM_JIT[3 * g + 2];
    float d = gemPair(q.x, q.y, r, j0);
    d = max(d, gemPair(q.x, q.z, r, j1));
    d = max(d, gemPair(q.x, q.w, r, j2));
    d = max(d, gemPair(q.y, q.z, r, j2.wxyz));
    d = max(d, gemPair(q.y, q.w, r, j0.zwxy));
    d = max(d, gemPair(q.z, q.w, r, j1.yzwx));
    return d * min(min(scale.x, scale.y), min(scale.z, scale.w));
}

// Rounder, finer-faceted gem: the intersection of gems g and g + 1.
float sdGem2(vec4 p, float r, int g, vec4 scale) {
    return max(sdGem(p, r * 1.06, g, scale), sdGem(p, r * 1.06, g + 1, scale));
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
    h += 2.4 * max(0.0, 1.0 - length(q - vec3( 6.0, -3.5,  5.0)) / 4.0);
    h += 2.8 * max(0.0, 1.0 - length(q - vec3(-5.0, -3.0, -5.5)) / 4.5);
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

// The ground's height in (x, z, w). `facets` adds the 0..0.35 low-poly bumps; without
// them the ground is never above the real one (the shadow occluder uses that).
float groundHeight(vec3 q, bool facets) {
    float r = length(q);
    float h = GROUND_Y + hillField(q);
    if (facets) h += 0.35 * facetNoise3(q * 0.42);
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


// Radius of the island in (x, z, w) at a relative depth below the grass: it stays
// wide through the upper cliff, then rounds into the rocky tip like the cover.
float islandProfile(float depth) {
    return ISLAND_R * pow(max(1.0 - pow(depth, 2.4), 0.0), 0.62);
}

// The grass overhangs the cliff top: within ~1 below the grass the island is a
// little wider, so the lip casts a band of shadow on the dirt below it.
float grassLip(float y, float gh) {
    return 0.55 * smoothstep(gh - 1.3, gh - 0.35, y);
}

vec2 islandSDF(vec4 p) {
    vec3 q = p.xzw;
    // Top surface (height field, Lipschitz-corrected). The ground never dips more
    // than 1.2 below GROUND_Y, so deep under it the top can't be the nearest face.
    float gh = GROUND_Y - 1.2;
    float top = -1e5;
    if (p.y > GROUND_Y - 2.5) {
        gh = groundHeight(q, true);
        top = (p.y - gh) * 0.65;   // 1 / sqrt(1 + steepest slope^2)
    }
    // Faceted side wall that narrows toward the tip of the underside.
    float depth = clamp((GROUND_Y - p.y) / (GROUND_Y - ISLAND_BASE), 0.0, 1.0);
    float rBase = islandProfile(depth);
    // islandRadial(q) <= |q| / 0.9 and the radius below is >= rBase - 1.6 depth, so
    // when even that bound is inside the top surface the wall can be skipped.
    // The wall's gradient reaches about 1/0.9 from the faces plus 0.7 from the facet
    // noise, so its distance is scaled by 0.55 to stay a safe underestimate.
    float side = (length(q) / 0.9 - rBase + 1.6 * depth - 0.55) * 0.55;
    if (side > top) {
        float yb = p.y * 0.6;
        float bf = floor(yb);
        float jag = mix(hash1(bf * 1.7), hash1(bf * 1.7 + 1.7), yb - bf);
        float radius = rBase + 0.9 * jag * depth + grassLip(p.y, gh);
        // small chunky facets: piecewise-linear noise over (x, z, w, y) keeps faces flat
        radius -= 1.6 * depth * facetNoise3(q * 0.42 + vec3(0.0, p.y * 0.55, p.y * 0.3));
        side = (islandRadial(q, yb) - radius) * 0.55;
    }
    // the bowl sits in a socket: nothing of the island inside the bowl's glome
    float d = max(max(top, side), BOWL_R + BOWL_HALF + 0.04 - length(p));
    float m = (p.y > gh - 0.75) ? MAT_GRASS : MAT_DIRT;
    // a muddy bank where the ground dips into the pond
    if (abs(pondDist(q) - 0.15) < 0.4 && p.y < gh + 0.2) m = MAT_DIRT;
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
// Centres in 4D (sunk 0.15 size above the ground height there) and sizes. Some sit at
// w far from 0: rotate to find them.
const vec4 ROCKS[9] = vec4[9](
        vec4( 7.40, -2.474,  2.40,  0.00), vec4( 5.40, -2.561,  4.60,  0.40),
        vec4(-6.20, -2.377,  1.60, -0.30), vec4(-6.80, -2.680,  3.80,  0.20),
        vec4( 2.00, -2.731,  7.40, -0.50), vec4( 3.60, -1.424, -3.00,  5.20),
        vec4(-4.00, -2.243,  2.00, -5.80), vec4( 0.50, -2.651, -7.00,  6.80),
        vec4(-7.50, -2.632, -1.00,  4.50));
const float ROCK_SIZE[9] = float[9](1.25, 0.6, 0.55, 0.45, 0.5, 1.4, 1.6, 1.1, 0.9);
const vec4 ROCK_SCALE = vec4(1.15, 0.75, 1.0, 1.0);

#define ROCK(i) { vec4 q = p - ROCKS[i]; \
    if (length(q) - GEM_REACH * 1.15 * ROCK_SIZE[i] < d) d = min(d, sdGem(q, ROCK_SIZE[i], i, ROCK_SCALE)); }

vec2 rocksSDF(vec4 p, float best) {
    // the rock centres lie within 9.8 of the origin in (x, z, w) and 1 of y = -2.3,
    // and no rock reaches farther than 2.9 from its centre
    float d = best;
    if (length(vec2(max(length(p.xzw) - 9.8, 0.0), max(abs(p.y + 2.3) - 1.0, 0.0))) - 2.9 > d)
        return vec2(d, MAT_STONE);
    ROCK(0) ROCK(1) ROCK(2) ROCK(3) ROCK(4) ROCK(5) ROCK(6) ROCK(7) ROCK(8)
    return vec2(d, MAT_STONE);
}

// ---- standing stones --------------------------------------------------------
// Spherinders (a 3-ball swept along an axis) leaning into w, all away from w = 0 so
// the default slice matches the cover. A turned slice cuts them as upright
// cylinders, leaning ellipsoids or round stones depending on the angle, the
// cue Miegakure's spherinder grove uses.
float sdSpherinder(vec4 q, vec4 axis, float r, float h) {
    float a = dot(q, axis);
    vec2 d = vec2(length(q - axis * a) - r, abs(a) - h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - 0.06;
}

// (x, z, w) on the ground, and the lean into w
const vec4 STONES[4] = vec4[4](vec4( 7.5,  1.0,  4.0,  0.45), vec4(-7.0,  3.0,  3.5, -0.5),
                               vec4( 2.0, -7.5, -4.5,  0.55), vec4(-2.5, -6.5,  4.5, -0.4));

vec2 stonesSDF(vec4 p, float best) {
    float d = best;
    for (int i = 0; i < 4; i++) {
        vec4 s = STONES[i];
        vec4 q = p - vec4(s.x, GROUND_Y + 1.3, s.y, s.z);
        if (length(q) - 1.8 > d) continue;   // the stone reaches 1.75
        vec4 axis = normalize(vec4(0.12 * float(i - 1), 1.0, 0.08, s.w));
        // faceted: a spherinder cut by a few planes reads as hewn stone
        float st = sdSpherinder(q, axis, 0.55, 1.6);
        st = max(st, dot(q, normalize(vec4(1.0, 0.3, 0.2, 0.5))) - 0.5);
        st = max(st, dot(q, normalize(vec4(-0.6, 0.2, 1.0, -0.4))) - 0.48);
        d = min(d, st);
    }
    return vec2(d, MAT_STONE);
}

// ---- trees --------------------------------------------------------------------
// A tree: spherinder trunk and limbs, canopy of faceted glomes. Returns (d, mat), or a
// far miss when nothing of the tree is nearer than best. Each part (roots and trunk,
// boughs, every canopy gem) is skipped when its bound glome is farther than best.
// Wood is evaluated with w squashed, so it is wider along w than the canopy and a
// turned slice shows the trunk thinning but never a canopy floating on nothing.
// Canopy k uses gems 9 + 2k and 10 + 2k. The trees share them; each is mirrored
// differently (`flip`, +-1 per axis) so no two look alike.
const vec4 CANOPY[4] = vec4[4](vec4(-3.4, 9.0, 0.4, -0.3), vec4(2.9, 8.3, -0.2, 0.4),
                               vec4(-0.3, 10.6, -0.6, 1.0), vec4(0.6, 9.2, 1.4, -0.8));
const float CANOPY_R[4] = float[4](2.4, 2.45, 2.7, 1.9);
const vec4 CANOPY_SCALE = vec4(1.0, 0.82, 1.0, 0.55);   // thinner in w than the wood

#define CANOPY_GEM(k) { vec4 c = q - CANOPY[k]; \
    if (length(c) - GEM_REACH * 1.06 * CANOPY_R[k] < can) can = min(can, sdGem2(c, CANOPY_R[k], 9 + 2 * k, CANOPY_SCALE)); }

vec2 treeSDF(vec4 p, vec4 base, float s, vec4 flip, float best) {
    vec4 q = (p - base) * flip / s;
    float b = best / s;
    vec4 w = vec4(q.xyz, q.w * 0.25);   // wood space
    float wood = b;
    // roots and trunk, bound by a glome around them
    if (length(w - vec4(0.0, 2.4, 0.0, 0.0)) - 3.9 < wood) {
        wood = min(wood, sdLimb(w, vec4(0.0, -0.6, 0.0, 0.0), vec4(-0.3, 5.4, 0.0, 0.1), 0.8, 0.45));
        wood = min(wood, sdLimb(w, vec4(0.0, -0.4, 0.0, 0.0), vec4(1.2, -0.1, 0.4, 0.0), 0.55, 0.12));
        wood = min(wood, sdLimb(w, vec4(0.0, -0.4, 0.0, 0.0), vec4(-1.0, -0.2, -0.6, 0.4), 0.5, 0.12));
        wood = min(wood, sdLimb(w, vec4(0.0, -0.4, 0.0, 0.0), vec4(0.1, -0.2, 0.3, -1.1), 0.5, 0.12));
    }
    // the boughs
    if (length(w - vec4(-0.2, 7.2, 0.0, 0.0)) - 3.5 < wood) {
        wood = min(wood, sdLimb(w, vec4(-0.3, 5.2, 0.0, 0.1), vec4(-3.0, 8.0, 0.4, -0.2), 0.4, 0.18));
        wood = min(wood, sdLimb(w, vec4(-0.3, 4.8, 0.0, 0.1), vec4(2.6, 7.4, -0.2, 0.3), 0.38, 0.16));
        wood = min(wood, sdLimb(w, vec4(-0.3, 5.3, 0.0, 0.1), vec4(-0.2, 9.4, -0.5, 1.0), 0.32, 0.14));
        wood = min(wood, sdLimb(w, vec4(1.0, 6.0, -0.1, 0.4), vec4(1.6, 8.3, 0.4, -1.2), 0.18, 0.08));
    }
    // canopy: big faceted glomes at the bough ends, one reaching off into w
    float can = min(wood, b);
    CANOPY_GEM(0) CANOPY_GEM(1) CANOPY_GEM(2) CANOPY_GEM(3)
    if (can < wood) return vec2(can * s, MAT_LEAVES);
    return wood < b ? vec2(wood * s, MAT_BARK) : vec2(1e5, MAT_NONE);
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
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// The banks circle the island together, so the drift is one rotation of p; each bank
// is (x, y, z, w phase) at zero drift and leans along its own 4D direction.
const vec4 CLOUD[10] = vec4[10](
    vec4(34.00, -17.0, 0.00, 0.0), vec4(30.18, -15.7, 21.91, 1.7), vec4(13.56, -15.9, 41.65, 3.4),
    vec4(-13.00, -16.2, 40.15, 5.1), vec4(-28.61, -15.0, 20.84, 6.8), vec4(-38.20, -13.5, 0.06, 8.5),
    vec4(-30.79, -15.7, -22.28, 10.2), vec4(-13.29, -16.9, -40.58, 11.9), vec4(11.10, -15.4, -34.46, 13.6),
    vec4(33.91, -15.7, -24.78, 15.3));
const vec4 CLOUD_AXIS[10] = vec4[10](
    vec4(1.000, 0.0, 0.000, 0.000), vec4(-0.552, 0.0, 0.618, 0.559), vec4(-0.105, 0.0, -0.935, 0.339),
    vec4(0.735, 0.0, 0.521, -0.434), vec4(-0.829, 0.0, 0.190, -0.526), vec4(0.478, 0.0, -0.866, 0.149),
    vec4(0.271, 0.0, 0.773, 0.573), vec4(-0.902, 0.0, -0.373, 0.218), vec4(0.779, 0.0, -0.376, -0.501),
    vec4(-0.244, 0.0, 0.848, -0.471));

// a flat-bottomed cumulus: soft union of glomes along a 4D direction
#define CLOUD_BANK(i) { vec3 q3 = pr.xyz - CLOUD[i].xyz; \
    if (length(q3) - 11.0 < d) { \
        vec4 q = vec4(q3, pr.w - 14.0 * sin(CLOUD[i].w + t * 0.7)); \
        if (length(q) - 11.0 < d) { \
            vec4 along = CLOUD_AXIS[i]; \
            float cl = length(q) - 4.2; \
            cl = smin(cl, length(q - along * 4.6 - vec4(0.0, -0.6, 0.0, 0.0)) - 3.2, 1.5); \
            cl = smin(cl, length(q + along * 4.4 - vec4(0.0, -0.8, 0.0, 0.0)) - 2.9, 1.5); \
            cl = smin(cl, length(q - along * 1.8 - vec4(0.0, 2.4, 0.0, 0.0)) - 2.6, 1.5); \
            cl = smin(cl, length(q + along * 7.6 - vec4(0.0, -1.6, 0.0, 0.0)) - 1.8, 1.2); \
            d = min(d, max(cl, -2.2 - q.y)); } } }

vec2 cloudsSDF(vec4 p, float best) {
    float t = uTime * 0.02;
    float d = best;
    // every bank lies within 11 of its centre, and the centres lie in a ring of radius
    // 34-44 at height -17 to -13
    vec2 ring = vec2(length(p.xz) - 39.0, p.y + 15.0);
    if (length(max(abs(ring) - vec2(5.0, 2.0), 0.0)) - 11.0 > d) return vec2(d, MAT_CLOUD);
    // turn p back by the drift so the bank centres stay put
    float ct = cos(t), st = sin(t);
    vec4 pr = vec4(ct * p.x + st * p.z, p.y, -st * p.x + ct * p.z, p.w);
    CLOUD_BANK(0) CLOUD_BANK(1) CLOUD_BANK(2) CLOUD_BANK(3) CLOUD_BANK(4)
    CLOUD_BANK(5) CLOUD_BANK(6) CLOUD_BANK(7) CLOUD_BANK(8) CLOUD_BANK(9)
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
#ifndef SKIP_FENCE
    if (length(p - vec4(-7.4, GROUND_Y + 1.0, -1.6, 0.0)) - 2.0 < r.x) r = opU(r, fenceSDF(p));
#endif
#ifndef SKIP_ROCKS
    vec2 rk = rocksSDF(p, r.x);
    if (rk.x < r.x) r = rk;
    vec2 sn = stonesSDF(p, r.x);
    if (sn.x < r.x) r = sn;
#endif
    // the main tree, and two that exist only far along w: rotate in xw or zw to find them
#ifndef SKIP_TREES
    vec4 t0 = vec4(5.6, GROUND_Y, -3.2, 0.0);
    vec4 t1 = vec4(-4.5, GROUND_Y + 0.3, -5.0, 6.2);
    vec4 t2 = vec4(4.0, GROUND_Y + 0.2, 4.5, 5.5);
    if (length(p - t0 - vec4(0.0, 6.0, 0.0, 0.0) * 0.85) - 9.0 * 0.85 < r.x) r = opU(r, treeSDF(p, t0, 0.85, vec4(1.0), r.x));
    if (length(p - t1 - vec4(0.0, 6.0, 0.0, 0.0) * 0.7) - 9.0 * 0.7 < r.x) r = opU(r, treeSDF(p, t1, 0.7, vec4(-1.0, 1.0, 1.0, 1.0), r.x));
    if (length(p - t2 - vec4(0.0, 6.0, 0.0, 0.0) * 0.5) - 9.0 * 0.5 < r.x) r = opU(r, treeSDF(p, t2, 0.5, vec4(1.0, 1.0, -1.0, 1.0), r.x));
#endif
#ifndef SKIP_CLOUDS
    vec2 cl = cloudsSDF(p, r.x);
    if (cl.x < r.x) r = cl;
#endif
    return r;
}

// ---- coarse occluder for shadows and AO ----------------------------------------
// The renderer's soft shadows and AO march this instead of worldSDF. It keeps the
// big occluders (bowl, ground with its hills and pond, the cliff, fence, trees)
// and drops the rocks, stones, clouds, ground facets and canopy facets. The cliff uses its inner
// bound, so this surface never sits outside the real one and AO can only come out
// a little lighter.
#define WORLD_SHADOW_SDF
float worldShadowSDF(vec4 p) {
    float d = bowlSDF(p);
    vec3 q = p.xzw;
    float depth = clamp((GROUND_Y - p.y) / (GROUND_Y - ISLAND_BASE), 0.0, 1.0);
    float rBase = islandProfile(depth);
    float side = (length(q) / 0.9 - rBase + 1.6 * depth) * 0.55;
    float top = p.y > GROUND_Y - 2.5 ? (p.y - groundHeight(q, false)) * 0.65 : -1e5;
    float island = max(max(top, side), BOWL_R + BOWL_HALF + 0.04 - length(p));
    d = min(d, island);
    if (length(p - vec4(-7.4, GROUND_Y + 1.0, -1.6, 0.0)) - 3.0 < d) d = min(d, fenceSDF(p).x);
    // trees: trunk capsule and canopy glomes, as in treeSDF (scale s, canopy w x 0.55)
    vec4 t0 = vec4(5.6, GROUND_Y, -3.2, 0.0);
    vec4 tq = (p - t0) / 0.85;
    if (length(tq - vec4(0.0, 6.0, 0.0, 0.0)) - 9.0 < d / 0.85) {
        vec4 tw = vec4(tq.xyz, tq.w * 0.25);
        float tr = sdLimb(tw, vec4(0.0, -0.6, 0.0, 0.0), vec4(-0.3, 5.4, 0.0, 0.1), 0.8, 0.45);
        vec4 cq = vec4(tq.xyz, tq.w / 0.55);
        float can = length(cq - vec4(-3.4, 9.0, 0.4, -0.3 / 0.55)) - 2.3;
        can = min(can, length(cq - vec4(2.9, 8.3, -0.2, 0.4 / 0.55)) - 2.35);
        can = min(can, length(cq - vec4(-0.3, 10.6, -0.6, 1.0 / 0.55)) - 2.6);
        d = min(d, min(tr, can * 0.55) * 0.85);
    }
    return d;
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
    float cov = smoothstep(0.53, 0.54, n) * band;
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
vec3 materialSrgb(float id, vec4 p, vec4 n) {
    float v = facetNoise3(p.xzw * 0.9 + p.y * 0.37);   // tint variation that follows the facets
    if (id == MAT_GRASS) {
        // sunlit tops lean yellow-green, the steep lip and banks lean deep green
        vec3 top = mix(vec3(0.16, 0.31, 0.07), vec3(0.27, 0.38, 0.08), v);
        vec3 lip = vec3(0.09, 0.21, 0.08);
        return mix(lip, top, smoothstep(0.35, 0.85, n.y));
    }
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

vec3 materialColor(float id, vec4 p, vec4 n) { return toLinear(materialSrgb(id, p, n));
}
