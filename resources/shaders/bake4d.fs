// Slice cache bake: four layers of the 3D distance texture per draw (one
// colour attachment each). Each texel holds worldStaticSDF at its centre,
// mapped from slice coordinates into 4D. See "Slice cache" in docs/4D.md.

layout(location = 0) out vec4 out0;
layout(location = 1) out vec4 out1;
layout(location = 2) out vec4 out2;
layout(location = 3) out vec4 out3;

uniform float uTime;   // world4d.glsl may read these (not in its static part)
uniform vec4  uSunDir;

uniform vec4  uCacheOrigin;  // world position of the box's (0, 0, 0) corner
uniform mat4  uCacheBasis;   // columns: slice axes x, y, z, slice normal
uniform float uCacheVoxel;   // texel size in world units
uniform float uLayer;        // texel z of out0; out1..3 follow

#include "world4d.glsl"

float bake(float layer) {
    vec3 x = vec3(gl_FragCoord.xy, layer + 0.5) * uCacheVoxel;
    return worldStaticSDF(uCacheOrigin + uCacheBasis * vec4(x, 0.0)).x;
}

void main() {
    out0 = vec4(bake(uLayer), 0.0, 0.0, 1.0);
    out1 = vec4(bake(uLayer + 1.0), 0.0, 0.0, 1.0);
    out2 = vec4(bake(uLayer + 2.0), 0.0, 0.0, 1.0);
    out3 = vec4(bake(uLayer + 3.0), 0.0, 0.0, 1.0);
}
