// fruit4d.glsl: what the 11 fruit look like, as 4D shapes (see docs/4D.md).
//
// The renderer pastes this after world4d.glsl (it reuses hash1, hash4,
// valueNoise4, sdLimb, smin and toLinear from there). Plain GLSL ES 3.00, no
// uniforms. Shapes are built in fruit-local units, q / r, so the physics
// collision glome is |q| = r and every body stays close to it.
//
// Contract (renderer side in raymarch4d.fs):
//   q     point minus the fruit centre, rotated into that fruit's local frame
//   r     current radius (grows on spawn and merge)
//   type  0..10, the Fruit enum: cherry, strawberry, grape, dekopon, persimmon,
//         apple, pear, peach, pineapple, melon, watermelon
//   seed  stable per-fruit id >= 1, for variation
//   lod   detail 0..1: 1 full, 0 when the fruit covers only a few pixels
//
//   FRUIT_BOUND          fruitSDF stays inside the glome |q| = FRUIT_BOUND * r
//   float fruitSDF(vec4 q, float r, float type, float lod)
//   vec2  fruitSDFPart(vec4 q, float r, float type, float lod)   distance and the part there (FRUIT_SKIN ...)
//   vec3  fruitAlbedo(vec4 q, vec4 n, float r, float type, float part, float lod)   linear RGB; n is local
//   vec3  fruitBaseColor(float type)   one colour per fruit, for bursts and the drop preview
//   float fruitGloss(float type)       specular strength 0..1 for the skin
//
// Stems, leaves and crowns are real 4D shapes reaching into w, so they change
// shape when the slice turns. Below lod 0.35 they are left out.

#define FRUIT_BOUND 1.6

const float FRUIT_SKIN  = 0.0;
const float FRUIT_STEM  = 1.0;
const float FRUIT_LEAF  = 2.0;
const float FRUIT_CROWN = 3.0;

// ---- helpers ------------------------------------------------------------------

float smax(float a, float b, float k) { return -smin(-a, -b, k); }

// Ellipsoid distance (bound, after Inigo Quilez), radii r.
float sdEllipsoid3(vec3 p, vec3 r) {
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / max(k1, 1e-6);
}

// A 4D leaf blade: a thin ellipsoidal hyper-sheet starting at `base`, reaching
// along unit `dir` for `len`, `wid` wide in the two directions across it and
// `thick` along `nrm` (made orthogonal to dir here). Its slices are leaf-shaped
// ovals whose size and tilt change as the slice turns.
float sdBlade(vec4 q, vec4 base, vec4 dir, vec4 nrm, float len, float wid, float thick) {
    nrm = normalize(nrm - dir * dot(nrm, dir));
    vec4 v = q - base - dir * len;
    float a = dot(v, dir);
    float b = dot(v, nrm);
    float c = length(v - dir * a - nrm * b);
    return sdEllipsoid3(vec3(a, c, b), vec3(len, wid, thick));
}

// The six axis directions of the 3-space orthogonal to y (x, z, w and their
// negatives): crowns and calyxes spread along them, so a turn in w swaps which
// sepals the slice shows.
vec4 ringDir(int i) {
    float s = (i & 1) == 0 ? 1.0 : -1.0;
    int k = i / 2;
    return k == 0 ? vec4(s, 0.0, 0.0, 0.0) : k == 1 ? vec4(0.0, 0.0, s, 0.0) : vec4(0.0, 0.0, 0.0, s);
}

// 4D cellular pattern: distance from q to the jittered point of its own cell
// (cell size 1/k), in cell units. One cell, no neighbours: cheap, fine for dots.
float cellDot(vec4 q, float k) {
    vec4 c = floor(q * k);
    vec4 j = vec4(hash4(c), hash4(c + 17.3), hash4(c + 41.9), hash4(c + 73.1));
    return length(fract(q * k) - (0.25 + 0.5 * j));
}

// Low-order 4D harmonics for silhouette character. These stay in fruit-local
// space, so a turn through w cuts a genuinely different outline.
float fruitLump(vec4 q, float amp) {
    vec4 u = normalize(q + vec4(0.0007, -0.0005, 0.0003, 0.0009));
    float h = 0.42 * sin(dot(u, normalize(vec4(1.4, 0.6, -0.9, 1.1))) * 4.2 + 0.3)
            + 0.34 * sin(dot(u, normalize(vec4(-0.7, 1.7, 1.2, -0.6))) * 5.4 + 2.1)
            + 0.24 * sin(dot(u, normalize(vec4(1.1, -1.0, 0.5, 1.8))) * 6.8 + 4.0);
    return amp * h;
}

// Grape berries sit on the D4 lattice (integer points with an even sum, shifted
// half a step in x, spacing 0.36): the densest packing of balls in 4D, each
// berry touching 24 others. Those inside a bunch that tapers downward are kept,
// 30 berries in four layers of y (14, 10, 5, 1), so a slice cuts each layer
// into berries of different sizes and turning the slice reshuffles them.
const float GRAPE_R = 0.25;
const vec4 GRAPE_BERRY[30] = vec4[30](
    // y = 0.36: 14
    vec4(-0.54, 0.36, -0.36, 0.00), vec4(-0.54, 0.36, 0.00, -0.36), vec4(-0.54, 0.36, 0.00, 0.36), vec4(-0.54, 0.36, 0.36, 0.00),
    vec4(-0.18, 0.36, -0.36, -0.36), vec4(-0.18, 0.36, -0.36, 0.36), vec4(-0.18, 0.36, 0.00, 0.00), vec4(-0.18, 0.36, 0.36, -0.36),
    vec4(-0.18, 0.36, 0.36, 0.36), vec4(0.18, 0.36, -0.36, 0.00), vec4(0.18, 0.36, 0.00, -0.36), vec4(0.18, 0.36, 0.00, 0.36),
    vec4(0.18, 0.36, 0.36, 0.00), vec4(0.54, 0.36, 0.00, 0.00),
    // y = 0.00: 10
    vec4(-0.54, 0.00, 0.00, 0.00), vec4(-0.18, 0.00, -0.36, 0.00), vec4(-0.18, 0.00, 0.00, -0.36), vec4(-0.18, 0.00, 0.00, 0.36),
    vec4(-0.18, 0.00, 0.36, 0.00), vec4(0.18, 0.00, -0.36, -0.36), vec4(0.18, 0.00, -0.36, 0.36), vec4(0.18, 0.00, 0.00, 0.00),
    vec4(0.18, 0.00, 0.36, -0.36), vec4(0.18, 0.00, 0.36, 0.36),
    // y = -0.36: 5
    vec4(-0.18, -0.36, 0.00, 0.00), vec4(0.18, -0.36, -0.36, 0.00), vec4(0.18, -0.36, 0.00, -0.36), vec4(0.18, -0.36, 0.00, 0.36),
    vec4(0.18, -0.36, 0.36, 0.00),
    // y = -0.72: 1
    vec4(0.18, -0.72, 0.00, 0.00));

// Distance to the bunch. The layer nearest in y goes first; another layer is
// skipped when its y distance alone already exceeds the best so far.
float grapeBunch(vec4 q) {
    int nearest = clamp(int(floor((0.36 - q.y) / 0.36 + 0.5)), 0, 3);
    float d = 1e5;
    for (int j = 0; j < 4; j++) {
        int k = j == 0 ? nearest : (j <= nearest ? j - 1 : j);
        if (abs(q.y - (0.36 - 0.36 * float(k))) - GRAPE_R > d) continue;
        int first = k == 0 ? 0 : k == 1 ? 14 : k == 2 ? 24 : 29;
        int count = k == 0 ? 14 : k == 1 ? 10 : k == 2 ? 5 : 1;
        for (int i = 0; i < count; i++) d = smin(d, length(q - GRAPE_BERRY[first + i]) - GRAPE_R, 0.03);
    }
    return d;
}

float pineappleScaleBump(vec4 q) {
    vec3 a = vec3(dot(q, normalize(vec4(1.0, 0.7, 0.0, 0.0))),
                  dot(q, normalize(vec4(0.0, 0.7, 1.0, 0.0))),
                  dot(q, normalize(vec4(0.0, 0.7, 0.0, 1.0))));
    vec3 b = vec3(dot(q, normalize(vec4(1.0, -0.7, 0.0, 0.0))),
                  dot(q, normalize(vec4(0.0, -0.7, 1.0, 0.0))),
                  dot(q, normalize(vec4(0.0, -0.7, 0.0, 1.0))));
    vec3 f = abs(fract(a * 5.0) - 0.5), f2 = abs(fract(b * 5.0) - 0.5);
    float m = min(min(min(f.x, f.y), f.z), min(min(f2.x, f2.y), f2.z));
    return smoothstep(0.19, 0.31, m);
}

float melonNetBump(vec4 q) {
    float net = abs(valueNoise4(q * 6.0) - 0.5) + 0.6 * abs(valueNoise4(q * 11.0 + 5.0) - 0.5);
    return smoothstep(0.075, 0.025, net);
}

// ---- shapes -------------------------------------------------------------------

vec2 fruitPart(vec2 a, float d, float part) { return d < a.x ? vec2(d, part) : a; }

// Stems, leaves and crowns are thin, so in a slice they would vanish after a tiny
// turn into w. They are evaluated with w squashed (crowns x 0.35, stems x 0.14,
// leaves x 0.7, so a stem outlasts its leaf and no leaf floats free), which makes them thicker along w than
// across: a turn thins and reshapes them before they go. Stems start deep in
// the body, so a turned slice never shows one floating above it.
vec4 wThick(vec4 q) { return vec4(q.xyz, q.w * 0.35); }
const vec4 STEM_SQUASH = vec4(1.0, 1.0, 1.0, 0.14);
// A stem limb; a and b are fruit-local points, squashed like qst.
float stemLimb(vec4 qst, vec4 a, vec4 b, float ra, float rb) {
    return sdLimb(qst, a * STEM_SQUASH, b * STEM_SQUASH, ra, rb);
}

// Shape in fruit-local units: (distance, part). With parts false, only the skin.
vec2 fruitShape(vec4 q, float type, bool parts) {
    vec4 qs = parts ? wThick(q) : vec4(0.0, 1e3, 0.0, 0.0);   // far away: parts never win
    vec4 qst = parts ? q * STEM_SQUASH : qs;          // stems outlast their leaves
    vec4 qsl = parts ? vec4(q.xyz, q.w * 0.7) : qs;     // leaves go first
    int t = int(type + 0.5);
    vec4 up = vec4(0.0, 1.0, 0.0, 0.0);
    vec2 r;

    if (t == 0) {                                   // cherry: round, dimple, long stem
        vec4 p = q + vec4(0.035, -0.015, 0.0, -0.025);
        float d = length(p * vec4(0.98, 1.03, 1.0, 1.02)) - 0.955 - fruitLump(q, 0.018);
        d = smax(d, 0.3 - length(q - vec4(0.0, 1.02, 0.0, 0.0)), 0.18);
        r = vec2(d, FRUIT_SKIN);
        float s = stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.08, 1.25, 0.0, 0.05), 0.06, 0.05);
        s = min(s, stemLimb(qst, vec4(0.08, 1.25, 0.0, 0.05), vec4(0.3, 1.55, 0.0, 0.1), 0.05, 0.04));
        s = min(s, stemLimb(qst, vec4(0.3, 1.55, 0.0, 0.1), vec4(0.55, 1.5, 0.0, 0.1), 0.04, 0.035));
        r = fruitPart(r, s, FRUIT_STEM);
        r = fruitPart(r, sdBlade(qsl, vec4(0.3, 1.52, 0.0, 0.1), normalize(vec4(-0.75, 0.05, 0.35, 0.3)),
                                 up, 0.42, 0.17, 0.025), FRUIT_LEAF);
    } else if (t == 1) {                            // strawberry: tapers to a point, sepals on top
        float s = mix(0.35, 1.0, smoothstep(-1.05, 0.5, q.y));
        vec4 p = vec4(q.x / s, (q.y + 0.05) * 1.02, q.z / s, q.w / s);
        float ribs = 0.018 * sin(dot(normalize(q.xzw + vec3(0.01)), normalize(vec3(1.0, -0.4, 0.7))) * 18.0);
        float d = (length(p) - 0.95 - fruitLump(q, 0.018) - ribs) * s * 0.85;
        d = smax(d, q.y - 0.62, 0.18);
        r = vec2(d, FRUIT_SKIN);
        float c = 1e5;
        for (int i = 0; i < 6; i++) {
            vec4 dir = normalize(ringDir(i) - up * 0.15);
            c = min(c, sdBlade(qs, vec4(0.0, 0.6, 0.0, 0.0), dir, up, 0.5, 0.19, 0.035));
        }
        r = fruitPart(r, c, FRUIT_CROWN);
        r = fruitPart(r, stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.05, 0.95, 0.0, 0.06), 0.06, 0.045), FRUIT_STEM);
    } else if (t == 2) {                            // grape: a D4-packed bunch, stalk, tendril, leaf
        r = vec2(grapeBunch(q), FRUIT_SKIN);
        float s = stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(-0.05, 1.05, 0.0, 0.04), 0.07, 0.055);
        s = min(s, stemLimb(qst, vec4(-0.05, 1.05, 0.0, 0.04), vec4(-0.3, 1.32, 0.0, 0.1), 0.055, 0.04));
        s = min(s, stemLimb(qst, vec4(-0.05, 0.95, 0.0, 0.04), vec4(0.3, 1.2, 0.05, -0.1), 0.03, 0.02));   // tendril
        s = min(s, stemLimb(qst, vec4(0.3, 1.2, 0.05, -0.1), vec4(0.38, 1.38, 0.0, -0.05), 0.02, 0.015));
        r = fruitPart(r, s, FRUIT_STEM);
        r = fruitPart(r, sdBlade(qsl, vec4(-0.08, 1.0, 0.0, 0.04), normalize(vec4(-0.85, 0.15, -0.3, 0.35)),
                                 up, 0.42, 0.26, 0.025), FRUIT_LEAF);
    } else if (t == 3) {                            // dekopon: squat orange with its knobbly neck bump
        float d = length(q * vec4(1.0, 1.13, 1.0, 1.0)) - 0.92 - fruitLump(q, 0.03);
        d = smin(d, length((q - vec4(0.02, 0.79, 0.0, -0.03)) * vec4(1.08, 0.82, 1.0, 1.05)) - 0.37 - fruitLump(q * 1.6, 0.018), 0.20);
        r = vec2(d * 0.9, FRUIT_SKIN);
        r = fruitPart(r, stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.0, 1.2, 0.0, 0.0), 0.06, 0.05), FRUIT_STEM);
        r = fruitPart(r, sdBlade(qsl, vec4(0.0, 1.15, 0.0, 0.0), normalize(vec4(0.8, 0.35, 0.3, 0.25)),
                                 up, 0.44, 0.18, 0.025), FRUIT_LEAF);
    } else if (t == 4) {                            // persimmon: squat, softly square, big calyx
        vec3 a = abs(q.xzw);
        vec3 a2 = a * a;
        float sq = sqrt(sqrt(dot(a2, a2)));         // L4 norm: rounded-square in (x, z, w)
        float rr = mix(length(q.xzw), sq * 1.08, 0.6);
        float d = length(vec2(rr, q.y * 1.22)) - 0.95 - fruitLump(q, 0.018);
        r = vec2(d * 0.8, FRUIT_SKIN);
        float c = 1e5;
        for (int i = 0; i < 6; i++) {
            vec4 dir = normalize(ringDir(i) + up * 0.12);
            c = min(c, sdBlade(qs, vec4(0.0, 0.72, 0.0, 0.0), dir, up, 0.38, 0.24, 0.04));
        }
        r = fruitPart(r, c, FRUIT_CROWN);
        r = fruitPart(r, stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.0, 0.95, 0.0, 0.0), 0.08, 0.07), FRUIT_STEM);
    } else if (t == 5) {                            // apple: dimpled top and bottom, stem and leaf
        vec4 p = q;
        p.x += 0.04 * smoothstep(-0.7, 0.7, q.y);
        p.w -= 0.03 * smoothstep(-0.4, 0.8, q.y);
        float d = length(p * vec4(1.0, 1.07, 1.0, 1.0)) - 0.95 - 0.09 * q.y - fruitLump(q, 0.02);
        d = smax(d, 0.36 - length(q - vec4(0.0, 1.08, 0.0, 0.0)), 0.22);
        d = smax(d, 0.22 - length(q - vec4(0.0, -1.0, 0.0, 0.0)), 0.15);
        r = vec2(d * 0.9, FRUIT_SKIN);
        r = fruitPart(r, stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.06, 1.18, 0.0, 0.05), 0.055, 0.045), FRUIT_STEM);
        r = fruitPart(r, sdBlade(qsl, vec4(0.05, 1.05, 0.0, 0.04), normalize(vec4(0.85, 0.3, 0.2, 0.25)),
                                 up, 0.48, 0.19, 0.025), FRUIT_LEAF);
    } else if (t == 6) {                            // pear: wide bottom narrowing to the neck
        vec4 p = q;
        p.x += 0.10 * smoothstep(-0.75, 0.45, q.y);
        p.w -= 0.06 * smoothstep(-0.55, 0.55, q.y);
        float d = smin(length(p - vec4(0.02, -0.30, 0.0, 0.0)) - 0.74,
                       length(p - vec4(-0.08, 0.5, 0.0, 0.04)) - 0.34, 0.5);
        d -= fruitLump(q, 0.018);
        r = vec2(d, FRUIT_SKIN);
        r = fruitPart(r, stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.12, 1.28, 0.0, 0.1), 0.06, 0.045), FRUIT_STEM);
        r = fruitPart(r, sdBlade(qsl, vec4(0.1, 1.15, 0.0, 0.08), normalize(vec4(-0.75, 0.3, 0.4, 0.25)),
                                 up, 0.3, 0.13, 0.025), FRUIT_LEAF);
    } else if (t == 7) {                            // peach: round with a suture groove
        vec4 sut = normalize(vec4(1.0, 0.0, 0.0, 0.7));
        float g = dot(q, sut);
        float d = length(q * vec4(0.98, 1.02, 1.0, 1.03)) - 0.96 - fruitLump(q, 0.016);
        d += 0.1 * exp(-g * g * 30.0) * smoothstep(-0.7, 0.3, q.y);
        d = smax(d, 0.25 - length(q - vec4(0.0, 1.0, 0.0, 0.0)), 0.15);
        r = vec2(d * 0.75, FRUIT_SKIN);
        r = fruitPart(r, stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.0, 1.02, 0.0, 0.0), 0.06, 0.05), FRUIT_STEM);
        float l = sdBlade(qsl, vec4(0.0, 1.0, 0.0, 0.0), normalize(vec4(-0.7, 0.25, -0.5, 0.3)), up, 0.46, 0.18, 0.025);
        l = min(l, sdBlade(qsl, vec4(0.0, 1.0, 0.0, 0.0), normalize(vec4(0.75, 0.3, 0.3, -0.35)), up, 0.36, 0.15, 0.025));
        r = fruitPart(r, l, FRUIT_LEAF);
    } else if (t == 8) {                            // pineapple: tall oval body, spiky crown
        float d = sdEllipsoid3(vec3(length(q.xzw), q.y + 0.08, 0.0), vec3(0.78, 0.86, 1.0));
        d -= 0.035 * pineappleScaleBump(q) * smoothstep(-0.78, -0.48, q.y) * (1.0 - smoothstep(0.35, 0.7, q.y));
        r = vec2(d, FRUIT_SKIN);
        float c = sdBlade(qsl, vec4(0.0, 0.7, 0.0, 0.0), up, vec4(1.0, 0.0, 0.0, 0.0), 0.5, 0.07, 0.03);
        for (int i = 0; i < 6; i++) {
            vec4 dir = normalize(up + ringDir(i) * 0.45);
            c = min(c, sdBlade(qsl, vec4(0.0, 0.68, 0.0, 0.0), dir, ringDir((i + 2) % 6), 0.44, 0.07, 0.025));
            vec4 dir2 = normalize(up * 0.6 + ringDir(i) * 0.8);
            c = min(c, sdBlade(qsl, vec4(0.0, 0.66, 0.0, 0.0), dir2, ringDir((i + 2) % 6), 0.32, 0.065, 0.025));
        }
        r = fruitPart(r, c, FRUIT_CROWN);
    } else if (t == 9) {                            // melon: round, T-shaped stem
        float d = length(q * vec4(1.0, 1.04, 1.0, 1.0)) - 0.97 - 0.026 * melonNetBump(q) - fruitLump(q, 0.012);
        r = vec2(d, FRUIT_SKIN);
        float s = stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.0, 1.15, 0.0, 0.0), 0.07, 0.06);
        s = min(s, stemLimb(qst, vec4(-0.2, 1.17, 0.0, -0.1), vec4(0.2, 1.17, 0.0, 0.1), 0.05, 0.05));
        r = fruitPart(r, s, FRUIT_STEM);
    } else {                                        // watermelon: big, slightly oval, stub
        float stripeRidge = 0.02 * sin(valueNoise4(vec4(normalize(q.xzw + 1e-5) * 4.0, 0.0)) * 32.0);
        float d = length(q * vec4(1.0, 1.06, 1.0, 1.0)) - 0.98 - stripeRidge;
        r = vec2(d, FRUIT_SKIN);
        float s = stemLimb(qst, vec4(0.0, 0.3, 0.0, 0.0), vec4(0.04, 1.08, 0.0, 0.03), 0.05, 0.04);
        vec4 a = vec4(0.04, 1.08, 0.0, 0.03);
        for (int i = 1; i <= 5; i++) {              // a curl of tendril, widening into w
            float u = float(i) * 1.1;
            vec4 b = vec4(0.04 + 0.16 * sin(u), 1.1 + 0.1 * float(i) / 5.0 + 0.1 * (1.0 - cos(u)), 0.0, 0.03 + 0.06 * float(i) / 5.0);
            s = min(s, stemLimb(qst, a, b, 0.022, 0.018));
            a = b;
        }
        r = fruitPart(r, s, FRUIT_STEM);
        r = fruitPart(r, sdBlade(qsl, vec4(0.0, 1.0, 0.0, 0.03), normalize(vec4(-0.8, 0.2, 0.3, 0.35)),
                                 up, 0.3, 0.16, 0.02), FRUIT_LEAF);
    }
    return r;
}

// ---- surfaces -----------------------------------------------------------------
// All patterns are functions of the 4D local point (after the tilt), so they stay
// glued to the fruit and change coherently when the slice turns.

vec3 fruitSkinSrgb(int t, vec4 q, vec4 n, float detail) {
    float v = valueNoise4(q * 3.0);
    if (t == 0) {                                   // cherry: deep glossy red, darker at the base
        return mix(vec3(0.30, 0.01, 0.06), vec3(0.62, 0.03, 0.12), smoothstep(-0.9, 0.6, q.y) * (0.8 + 0.2 * v));
    }
    if (t == 1) {                                   // strawberry: red with yellow seeds in pockets
        vec3 c = mix(vec3(0.74, 0.07, 0.10), vec3(0.93, 0.18, 0.16), v);
        c = mix(c, vec3(0.95, 0.75, 0.55), smoothstep(0.45, 0.62, q.y) * 0.5);   // pale shoulder
        float s = cellDot(q, 6.0);
        c = mix(c, vec3(0.45, 0.02, 0.05), smoothstep(0.4, 0.3, s) * detail);   // pocket
        c = mix(c, vec3(0.98, 0.86, 0.35), smoothstep(0.24, 0.17, s) * detail);  // seed
        return c;
    }
    if (t == 2) {                                   // grape: purple with a dusty bloom
        vec3 c = mix(vec3(0.30, 0.10, 0.42), vec3(0.48, 0.22, 0.62), v);
        float bloom = smoothstep(0.35, 0.8, valueNoise4(q * 7.0 + 3.0));
        return mix(c, vec3(0.66, 0.58, 0.78), bloom * 0.45 * detail);
    }
    if (t == 3) {                                   // dekopon: orange peel with dimples
        vec3 c = mix(vec3(0.95, 0.47, 0.06), vec3(1.0, 0.6, 0.12), v);
        float s = cellDot(q, 9.0);
        return mix(c, vec3(0.78, 0.33, 0.03), smoothstep(0.25, 0.1, s) * 0.7 * detail);
    }
    if (t == 4) {                                   // persimmon: red-orange, deeper toward the base
        return mix(vec3(0.78, 0.22, 0.04), vec3(0.98, 0.45, 0.08), smoothstep(-1.0, 0.7, q.y) * (0.75 + 0.25 * v));
    }
    if (t == 5) {                                   // apple: red with a yellow-green blush and streaks
        vec4 sunSide = normalize(vec4(0.8, 0.2, 0.3, 0.2));
        float side = smoothstep(-0.3, 0.6, dot(normalize(q), sunSide) + 0.3 * (v - 0.5));
        float streak = valueNoise4(vec4(q.x * 9.0, q.y * 2.0, q.z * 9.0, q.w * 9.0));
        vec3 c = mix(vec3(0.75, 0.62, 0.15), vec3(0.78, 0.07, 0.07), side);
        return mix(c, vec3(0.6, 0.04, 0.05), smoothstep(0.6, 0.72, streak) * side * detail * 0.35);
    }
    if (t == 6) {                                   // pear: yellow-green with russet speckles
        vec3 c = mix(vec3(0.66, 0.70, 0.18), vec3(0.85, 0.80, 0.30), v);
        float s = cellDot(q, 11.0);
        return mix(c, vec3(0.50, 0.36, 0.12), smoothstep(0.14, 0.08, s) * detail);
    }
    if (t == 7) {                                   // peach: rose blush over golden yellow
        vec4 blushDir = normalize(vec4(0.6, 0.4, 0.5, -0.5));
        float b = smoothstep(-0.5, 0.7, dot(normalize(q), blushDir) + 0.25 * (v - 0.5));
        return mix(vec3(0.98, 0.72, 0.38), vec3(0.93, 0.34, 0.33), b);
    }
    if (t == 8) {                                   // pineapple: golden scales with brown seams
        vec3 c = mix(vec3(0.80, 0.55, 0.14), vec3(0.95, 0.72, 0.22), v);
        // three families of tilted hyperplanes make the 4D scale lattice
        vec3 a = vec3(dot(q, normalize(vec4(1.0, 0.7, 0.0, 0.0))),
                      dot(q, normalize(vec4(0.0, 0.7, 1.0, 0.0))),
                      dot(q, normalize(vec4(0.0, 0.7, 0.0, 1.0))));
        // diamonds: a pair of crossing helical families per axis pair, plus the w one
        vec3 a2 = vec3(dot(q, normalize(vec4(1.0, -0.7, 0.0, 0.0))),
                       dot(q, normalize(vec4(0.0, -0.7, 1.0, 0.0))),
                       dot(q, normalize(vec4(0.0, -0.7, 0.0, 1.0))));
        vec3 f = abs(fract(a * 5.0) - 0.5), f2 = abs(fract(a2 * 5.0) - 0.5);
        float m = min(min(min(f.x, f.y), f.z), min(min(f2.x, f2.y), f2.z));
        float seam = smoothstep(0.06, 0.015, m);
        float eye = smoothstep(0.2, 0.3, m);
        c = mix(c, vec3(0.45, 0.25, 0.07), seam * detail);
        return mix(c, vec3(0.55, 0.38, 0.10), eye * 0.6 * detail);
    }
    if (t == 9) {                                   // melon: green-gold with pale netting
        vec3 c = mix(vec3(0.42, 0.55, 0.20), vec3(0.62, 0.68, 0.30), v);
        float net = abs(valueNoise4(q * 6.0) - 0.5) + 0.6 * abs(valueNoise4(q * 11.0 + 5.0) - 0.5);
        return mix(c, vec3(0.88, 0.86, 0.68), smoothstep(0.07, 0.02, net) * detail);
    }
    // watermelon: dark wavy stripes running pole to pole. They depend only on the
    // direction in (x, z, w), so they follow meridians whatever the slice.
    vec3 dir = normalize(q.xzw + 1e-5);
    float s = valueNoise4(vec4(dir * 2.2, 0.0)) * 0.75 + valueNoise4(vec4(dir * 6.0, 1.0)) * 0.25;
    float stripe = smoothstep(0.45, 0.5, fract(s * 6.0));
    vec3 c = mix(vec3(0.42, 0.64, 0.22), vec3(0.08, 0.30, 0.10), stripe * (0.4 + 0.6 * detail));
    return mix(c, vec3(0.62, 0.66, 0.30), smoothstep(-0.7, -1.0, q.y) * 0.6);   // pale ground spot
}

// ---- contract -----------------------------------------------------------------

float fruitSDF(vec4 q, float r, float type, float lod) {
    return fruitShape(q / r, type, lod >= 0.35).x * r;
}

vec2 fruitSDFPart(vec4 q, float r, float type, float lod) {
    vec2 s = fruitShape(q / r, type, lod >= 0.35);
    return vec2(s.x * r, s.y);
}

// part: from fruitSDFPart at the hit, so colouring costs no shape evaluation.
vec3 fruitAlbedo(vec4 q, vec4 n, float r, float type, float part, float lod) {
    q /= r;
    int t = int(type + 0.5);
    vec3 c;
    if (part == FRUIT_STEM) c = (t == 2 || t == 0 || t == 10) ? vec3(0.32, 0.45, 0.14) : vec3(0.36, 0.22, 0.10);
    else if (part == FRUIT_LEAF) c = mix(vec3(0.16, 0.42, 0.12), vec3(0.28, 0.55, 0.16), valueNoise4(q * 6.0));
    else if (part == FRUIT_CROWN) c = t == 4 ? vec3(0.28, 0.32, 0.10)
                                   : t == 8 ? mix(vec3(0.18, 0.40, 0.16), vec3(0.36, 0.55, 0.22), smoothstep(0.7, 1.4, q.y))
                                   : vec3(0.18, 0.45, 0.12);
    else c = fruitSkinSrgb(t, q, n, clamp(lod, 0.0, 1.0));
    return toLinear(c);
}

// One colour per fruit (the skin's mean), for merge bursts and the drop preview.
vec3 fruitBaseColor(float type) {
    int t = int(type + 0.5);
    vec3 c = t == 0 ? vec3(0.50, 0.03, 0.09)
           : t == 1 ? vec3(0.86, 0.13, 0.13)
           : t == 2 ? vec3(0.42, 0.18, 0.55)
           : t == 3 ? vec3(0.98, 0.54, 0.09)
           : t == 4 ? vec3(0.90, 0.35, 0.06)
           : t == 5 ? vec3(0.78, 0.15, 0.08)
           : t == 6 ? vec3(0.76, 0.75, 0.24)
           : t == 7 ? vec3(0.96, 0.52, 0.36)
           : t == 8 ? vec3(0.88, 0.63, 0.18)
           : t == 9 ? vec3(0.55, 0.62, 0.26)
           :          vec3(0.20, 0.47, 0.16);
    return toLinear(c);
}

float fruitGloss(float type) {
    int t = int(type + 0.5);
    if (t == 0 || t == 5) return 0.8;               // cherry, apple: shiny
    if (t == 2 || t == 3 || t == 4) return 0.5;
    if (t == 7 || t == 9 || t == 8) return 0.12;    // fuzzy, netted, scaly
    return 0.35;
}
