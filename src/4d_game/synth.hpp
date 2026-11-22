#pragma once
// Small procedural sounds (UI ticks, merge chimes, the new-best arpeggio),
// built once at start as 16-bit mono WAVs in memory and handed to SDL_mixer,
// which converts them to the device format. No extra asset files.
#ifndef __EMSCRIPTEN__
#include <SDL.h>
#include <SDL_mixer.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace synth {

constexpr int kRate = 44100;

inline Mix_Chunk* toChunk(const std::vector<float>& s) {
    const uint32_t bytes = uint32_t(s.size() * 2);
    std::vector<uint8_t> wav(44 + bytes);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&wav[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&wav[at], &v, 2); };
    std::memcpy(&wav[0], "RIFF", 4); put32(4, 36 + bytes); std::memcpy(&wav[8], "WAVE", 4);
    std::memcpy(&wav[12], "fmt ", 4); put32(16, 16); put16(20, 1); put16(22, 1);
    put32(24, kRate); put32(28, kRate * 2); put16(32, 2); put16(34, 16);
    std::memcpy(&wav[36], "data", 4); put32(40, bytes);
    for (size_t i = 0; i < s.size(); ++i) {
        float v = std::fmax(-1.0f, std::fmin(1.0f, s[i]));
        put16(44 + i * 2, uint16_t(int16_t(v * 32000.0f)));
    }
    return Mix_LoadWAV_RW(SDL_RWFromConstMem(wav.data(), int(wav.size())), 1);
}

// A struck tone: partials (frequency ratio, level, decay seconds) with a 2 ms attack.
struct Partial { float ratio, level, decay; };
inline void strike(std::vector<float>& out, float at, float freq, float gain,
                   std::initializer_list<Partial> parts, float length) {
    const size_t start = size_t(at * kRate), n = size_t(length * kRate);
    if (out.size() < start + n) out.resize(start + n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float t = float(i) / kRate;
        float env = std::fmin(1.0f, t / 0.002f);
        float v = 0.0f;
        for (const Partial& p : parts) v += p.level * std::exp(-t / p.decay) * std::sin(6.2831853f * freq * p.ratio * t);
        out[start + i] += gain * env * v;
    }
}

// 0 focus moved (soft tick), 1 pressed (wooden tock), 2 value changed (blip).
inline Mix_Chunk* uiSound(int kind) {
    std::vector<float> s;
    if (kind == 0) strike(s, 0.0f, 1760.0f, 0.10f, {{1.0f, 1.0f, 0.012f}, {2.0f, 0.3f, 0.006f}}, 0.06f);
    else if (kind == 1) strike(s, 0.0f, 740.0f, 0.30f, {{1.0f, 1.0f, 0.035f}, {2.76f, 0.5f, 0.012f}, {5.4f, 0.2f, 0.005f}}, 0.14f);
    else strike(s, 0.0f, 1175.0f, 0.14f, {{1.0f, 1.0f, 0.03f}, {3.0f, 0.15f, 0.01f}}, 0.09f);
    return toChunk(s);
}

// Merge chime for the fruit a merge makes: a marimba note on a pentatonic
// ladder, high for small fruit and lower, longer and fuller for big ones.
inline float mergeNote(int fruit) {
    static const float notes[] = {1318.5f, 1174.7f, 1046.5f, 880.0f, 784.0f, 659.3f, 587.3f, 523.3f, 440.0f, 392.0f, 329.6f, 293.7f};
    return notes[fruit < 0 ? 0 : fruit > 11 ? 11 : fruit];
}
inline Mix_Chunk* mergeChime(int fruit) {
    std::vector<float> s;
    float f = mergeNote(fruit), size = 0.25f + 0.05f * float(fruit);
    strike(s, 0.0f, f, 0.30f, {{1.0f, 1.0f, size}, {3.93f, 0.35f, 0.05f}, {9.2f, 0.08f, 0.012f}}, size * 4.0f);
    strike(s, 0.0f, f * 0.5f, 0.10f + 0.012f * float(fruit), {{1.0f, 1.0f, size * 1.4f}}, size * 4.0f);
    return toChunk(s);
}

// New best on game over: a rising four-note arpeggio.
inline Mix_Chunk* newBest() {
    std::vector<float> s;
    const float notes[] = {523.3f, 659.3f, 784.0f, 1046.5f};
    for (int i = 0; i < 4; ++i)
        strike(s, 0.09f * float(i), notes[i], 0.22f, {{1.0f, 1.0f, i == 3 ? 0.5f : 0.18f}, {3.93f, 0.25f, 0.04f}}, i == 3 ? 1.6f : 0.6f);
    return toChunk(s);
}

} // namespace synth
#endif
