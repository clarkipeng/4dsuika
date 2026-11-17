#pragma once
// Scripted camera path for reproducible captures (--camera-path file).
//
// One keyframe per line, `key=value` pairs, `#` starts a comment:
//   t=0   yaw=0  pitch=-23 distance=13 offset=0 xw=0 yw=0 zw=0 xy=0
//   t=4   yaw=90 distance=20 xw=60 ease=linear
// Angles are degrees. xw, yw, zw, xy are 4D turns in the camera-local planes
// (the same names as --turn), applied in that order. A key that leaves a field
// out keeps the previous key's value; the first key starts from the defaults.
// Between keys, orbit/distance/offset/target-y interpolate linearly in eased
// time (ease=smooth, the default, or ease=linear on the key that ends the
// segment) and the slice interpolates with slerp, so 4D turns take the
// shortest path. Before the first key and after the last, the camera holds.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "camera4d.hpp"

class CameraPath {
public:
    bool load(const std::string& file, std::string& error) {
        std::ifstream in(file);
        if (!in) { error = "cannot open " + file; return false; }
        keys_.clear();
        Key prev;
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            line = line.substr(0, line.find('#'));
            std::stringstream ss(line);
            std::string tok;
            Key k = prev;
            k.linear = false;  // ease belongs to one key, it is not inherited
            bool any = false, hasT = false;
            while (ss >> tok) {
                size_t eq = tok.find('=');
                if (eq == std::string::npos) { error = where(file, lineNo) + "expected name=value, got " + tok; return false; }
                std::string name = tok.substr(0, eq), val = tok.substr(eq + 1);
                any = true;
                if (name == "ease") {
                    if (val != "smooth" && val != "linear") { error = where(file, lineNo) + "ease is smooth or linear"; return false; }
                    k.linear = val == "linear";
                    continue;
                }
                float* f = field(k, name);
                if (!f) { error = where(file, lineNo) + "unknown field " + name; return false; }
                *f = float(std::atof(val.c_str()));
                hasT |= name == "t";
            }
            if (!any) continue;
            if (!hasT) { error = where(file, lineNo) + "missing t="; return false; }
            if (!keys_.empty() && k.t < keys_.back().t) { error = where(file, lineNo) + "t must not decrease"; return false; }
            prev = k;
            keys_.push_back(k);
        }
        for (Key& k : keys_) k.slice = sliceOf(k);
        if (keys_.empty()) { error = file + ": no keyframes"; return false; }
        return true;
    }

    float duration() const { return keys_.empty() ? 0.0f : keys_.back().t; }

    // Camera pose at time t (seconds). Keeps target x/z/w; only y is scripted.
    void apply(float t, Camera4D& cam) const {
        if (keys_.empty()) return;
        size_t hi = 0;
        while (hi < keys_.size() && keys_[hi].t <= t) ++hi;
        if (hi == 0) { set(keys_.front(), cam); return; }
        if (hi == keys_.size()) { set(keys_.back(), cam); return; }
        const Key &a = keys_[hi - 1], &b = keys_[hi];
        float u = (t - a.t) / std::max(b.t - a.t, 1e-6f);
        if (!b.linear) u = u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);  // smootherstep
        auto mix = [&](float x, float y) { return x + (y - x) * u; };
        cam.yaw = glm::radians(mix(a.yaw, b.yaw));
        cam.pitch = glm::radians(mix(a.pitch, b.pitch));
        cam.distance = mix(a.distance, b.distance);
        cam.offset = mix(a.offset, b.offset);
        cam.target.y = mix(a.ty, b.ty);
        cam.slice = Rot4::slerp(a.slice, b.slice, u);
    }

private:
    struct Key {
        float t = 0, yaw = 0, ty = 0, distance = Camera4D::kDefaultDistance;
        float pitch = glm::degrees(Camera4D::kDefaultPitch), offset = 0;
        float xw = 0, yw = 0, zw = 0, xy = 0;
        bool linear = false;
        Rot4 slice;
    };
    std::vector<Key> keys_;

    static std::string where(const std::string& f, int n) { return f + ":" + std::to_string(n) + ": "; }
    static float* field(Key& k, const std::string& n) {
        if (n == "t") return &k.t;
        if (n == "yaw") return &k.yaw;
        if (n == "pitch") return &k.pitch;
        if (n == "distance") return &k.distance;
        if (n == "offset") return &k.offset;
        if (n == "ty") return &k.ty;
        if (n == "xw") return &k.xw;
        if (n == "yw") return &k.yw;
        if (n == "zw") return &k.zw;
        if (n == "xy") return &k.xy;
        return nullptr;
    }
    static Rot4 sliceOf(const Key& k) {
        Camera4D c;  // yaw = pitch = 0, so turnLocal is in the plain world frame
        c.pitch = 0.0f;
        c.turnLocal(AX_X, AX_W, glm::radians(k.xw));
        c.turnLocal(AX_Y, AX_W, glm::radians(k.yw));
        c.turnLocal(AX_Z, AX_W, glm::radians(k.zw));
        c.turnLocal(AX_X, AX_Y, glm::radians(k.xy));
        return c.slice;
    }
    static void set(const Key& k, Camera4D& cam) {
        cam.yaw = glm::radians(k.yaw);
        cam.pitch = glm::radians(k.pitch);
        cam.distance = k.distance;
        cam.offset = k.offset;
        cam.target.y = k.ty;
        cam.slice = k.slice;
    }
};
