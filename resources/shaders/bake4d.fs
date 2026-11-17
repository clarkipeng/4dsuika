// Slice cache bake: one layer of the 3D distance texture per draw. Each texel
// holds worldStaticSDF at its centre, mapped from slice coordinates into 4D.
// See "Slice cache" in docs/4D.md.

out vec4 fragColor;

uniform float uTime;   // world4d.glsl may read these (not in its static part)
uniform vec4  uSunDir;

uniform vec4  uCacheOrigin;  // world position of the box's (0, 0, 0) corner
uniform mat4  uCacheBasis;   // columns: slice axes x, y, z, slice normal
uniform float uCacheVoxel;   // texel size in world units
uniform float uLayer;        // texel z of this draw

#include "world4d.glsl"

void main() {
    vec3 x = vec3(gl_FragCoord.xy, uLayer + 0.5) * uCacheVoxel;
    vec4 p = uCacheOrigin + uCacheBasis * vec4(x, 0.0);
    fragColor = vec4(worldStaticSDF(p).x, 0.0, 0.0, 1.0);
}
