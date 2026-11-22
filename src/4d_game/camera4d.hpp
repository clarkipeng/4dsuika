#pragma once
// 4D camera: a position in R^4 and an orientation in SO(4).
//
// Orientations are stored as a pair of unit quaternions (l, r): a vector
// v = (x, y, z, w) is read as the quaternion w + xi + yj + zk and maps to
// l * v * r. Every SO(4) rotation has this form, composition is quaternion
// multiplication, re-orthonormalisation is just normalising two quaternions,
// and slerping both halves gives smooth, shortest-path interpolation, which
// the reset animation uses.
#include <cmath>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

enum Axis4 { AX_X = 0, AX_Y = 1, AX_Z = 2, AX_W = 3 };

struct Rot4 {
    glm::quat l{1, 0, 0, 0};
    glm::quat r{1, 0, 0, 0};

    // Rotation by angle in the plane (a, b), turning axis a towards axis b.
    static Rot4 plane(Axis4 a, Axis4 b, float angle) {
        float sign = 1.0f;
        if (a > b) { std::swap(a, b); sign = -1.0f; }
        angle *= sign;
        const float c = std::cos(angle * 0.5f), s = std::sin(angle * 0.5f);
        auto q = [&](int axis, float sn) {
            glm::quat out(c, 0, 0, 0);
            out[axis] = sn; // glm::quat stores x, y, z, w; index 0..2 = i, j, k
            return out;
        };
        Rot4 R;
        if (b == AX_W) {
            // Planes touching w: left and right halves turn the same way.
            // Positive angle here turns w towards a, so flip to turn a towards w.
            R.l = q(a, -s);
            R.r = q(a, -s);
        } else {
            // Spatial planes x->y (k), y->z (i), z->x (j): conjugation.
            int axis = 3 - a - b;            // the axis left out of {x,y,z}
            float o = (axis == AX_Y) ? -1.0f : 1.0f; // x->z is -(z->x)
            R.l = q(axis, o * s);
            R.r = q(axis, -o * s);
        }
        return R;
    }

    Rot4 operator*(const Rot4& o) const { return {l * o.l, o.r * r}; }
    Rot4 inverse() const { return {glm::conjugate(l), glm::conjugate(r)}; }
    void normalize() { l = glm::normalize(l); r = glm::normalize(r); }

    glm::vec4 apply(const glm::vec4& v) const {
        glm::quat q(v.w, v.x, v.y, v.z);
        glm::quat o = l * q * r;
        return glm::vec4(o.x, o.y, o.z, o.w);
    }
    glm::mat4 matrix() const {
        glm::mat4 M;
        for (int i = 0; i < 4; ++i) {
            glm::vec4 e(0.0f);
            e[i] = 1.0f;
            M[i] = apply(e);
        }
        return M;
    }
    static Rot4 slerp(const Rot4& a, const Rot4& b, float t) {
        // (l, r) and (-l, -r) are the same rotation; pick the closer sign.
        glm::quat bl = b.l, br = b.r;
        if (glm::dot(a.l, bl) + glm::dot(a.r, br) < 0.0f) { bl = -bl; br = -br; }
        Rot4 out{glm::slerp(a.l, bl, t), glm::slerp(a.r, br, t)};
        out.normalize();
        return out;
    }
};

// Orbit camera in 4D. The 3D slice the player sees passes through
// target + offset * normal; inside it the camera orbits the target.
//
//   basis  R = slice * orbit       (columns: right, up, back, slice normal)
//   pos    p = target + offset * R[3] + distance * R[2]
//
// `slice` holds every 4D turn (and roll); `orbit` is ordinary yaw and pitch
// inside the slice, so mouse turning never tilts the slice into w.
struct Camera4D {
    glm::vec4 target{0.0f, kDefaultTargetY, 0.0f, 0.0f};
    Rot4 slice;
    float yaw = 0.0f, pitch = kDefaultPitch;
    float distance = kDefaultDistance;
    float offset = 0.0f;                 // along the slice normal
    float fovY = glm::radians(45.0f);

    static constexpr float kDefaultPitch = -0.30f;
    static constexpr float kDefaultDistance = 8.8f;
    static constexpr float kDefaultTargetY = -0.1f;  // gameplay look-at height (the rim is y = 0)
    static constexpr float kOffsetMax = 3.0f;

    Rot4 orbit() const {
        return Rot4::plane(AX_Z, AX_X, yaw) * Rot4::plane(AX_Y, AX_Z, pitch);
    }
    Rot4 orientation() const { return slice * orbit(); }
    glm::mat4 basis() const { return orientation().matrix(); }
    glm::vec4 normal() const { return slice.apply(glm::vec4(0, 0, 0, 1)); }
    glm::vec4 position() const {
        glm::mat4 R = basis();
        return target + offset * R[3] + distance * R[2];
    }

    // Map a point given in camera-slice coordinates (right, up, back) to world.
    glm::vec4 sliceToWorld(const glm::vec3& local) const {
        glm::mat4 R = basis();
        return position() + R * glm::vec4(local, 0.0f);
    }
    // World-space ray through a pixel; ndc in [-1, 1]^2, y up.
    void worldRay(glm::vec2 ndc, float aspect, glm::vec4& origin, glm::vec4& dir) const {
        float t = std::tan(fovY * 0.5f);
        glm::vec4 local(ndc.x * t * aspect, ndc.y * t, -1.0f, 0.0f);
        origin = position();
        dir = glm::normalize(basis() * local);
    }

    // Screen position (ndc, y up) of a world point that lies in front of the camera.
    bool project(const glm::vec4& world, float aspect, glm::vec2& ndc) const {
        glm::vec4 local = glm::transpose(basis()) * (world - position());
        if (local.z >= -1e-4f) return false;
        float t = std::tan(fovY * 0.5f);
        ndc = glm::vec2(local.x / (-local.z * t * aspect), local.y / (-local.z * t));
        return true;
    }

    // Turn in a camera-local plane (axes of what the player sees: 0 right,
    // 1 up, 2 back, 3 slice normal). Applied to `slice` so orbit stays intact.
    void turnLocal(Axis4 a, Axis4 b, float angle) {
        Rot4 o = orbit();
        slice = slice * o * Rot4::plane(a, b, angle) * o.inverse();
        slice.normalize();
    }
    void orbitBy(float dyaw, float dpitch) {
        yaw += dyaw;
        pitch = glm::clamp(pitch + dpitch, -1.45f, 1.45f);
    }
    void zoom(float d) { distance = glm::clamp(distance - d, 4.0f, 40.0f); }

    // Slow cinematic view of the whole island for the menu: drifts around it
    // and sways the slice gently through xw so the 4D shows from the start.
    static Camera4D showcase(float time) {
        Camera4D c;
        c.distance = 26.0f;
        c.pitch = -0.30f;
        c.yaw = 0.06f * time;
        c.target = glm::vec4(0.0f, -1.5f, 0.0f, 0.0f);
        c.turnLocal(AX_X, AX_W, 0.30f * std::sin(0.11f * time));
        return c;
    }
    void moveAlongNormal(float d) { offset = glm::clamp(offset + d, -kOffsetMax, kOffsetMax); }

    // One step of the "back to the default slice" animation; returns true when home.
    bool easeHome(float dt) {
        float k = 1.0f - std::exp(-dt * 6.0f);
        slice = Rot4::slerp(slice, Rot4{}, k);
        pitch += (kDefaultPitch - pitch) * k;
        yaw += (std::remainder(0.0f - yaw, glm::two_pi<float>())) * k;
        offset += (0.0f - offset) * k;
        distance += (kDefaultDistance - distance) * k;
        bool home = std::abs(glm::dot(slice.l, glm::quat(1, 0, 0, 0))) > 0.99999f &&
                    std::abs(glm::dot(slice.r, glm::quat(1, 0, 0, 0))) > 0.99999f &&
                    std::abs(pitch - kDefaultPitch) < 1e-3f && std::abs(offset) < 1e-3f;
        if (home) { slice = Rot4{}; pitch = kDefaultPitch; offset = 0.0f; }
        return home;
    }

    // Angle (radians) between the slice normal and world w: 0 = default slice.
    float tilt() const { return std::acos(glm::clamp(std::abs(normal().w), 0.0f, 1.0f)); }
};
