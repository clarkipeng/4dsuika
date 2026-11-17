#pragma once
// Frame recorder: dumps the current framebuffer as numbered PNGs.
//
// Readback is asynchronous: glReadPixels into a ring of pixel buffer objects
// returns at once, and a frame is mapped one or two frames later, when the GPU
// has finished it. Encoding runs on worker threads behind a bounded queue, so
// gameplay never waits on PNG compression unless the disk falls far behind
// (then capture blocks rather than eat memory).
//
// Recording runs the game at a fixed timestep (1 / fps) no matter how long a
// frame takes to render, so the video is deterministic and plays at real speed.
#ifndef __EMSCRIPTEN__
#include <glad/glad.h>
// Needs stb_image_write.h included (with its implementation) before this header.

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class Recorder {
public:
    int fps = 60;

    ~Recorder() { stop(); }

    bool active() const { return active_; }
    float dt() const { return 1.0f / float(fps); }
    int frameCount() const { return written_; }

    bool start(const std::string& dir) {
        if (active_) return true;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) { std::cerr << "record: cannot create " << dir << ": " << ec.message() << std::endl; return false; }
        dir_ = dir;
        index_ = captured_ = written_ = 0;
        w_ = h_ = 0;
        quit_ = false;
        unsigned n = std::max(2u, std::min(4u, std::thread::hardware_concurrency() / 2));
        for (unsigned i = 0; i < n; ++i) workers_.emplace_back([this] { work(); });
        active_ = true;
        std::cout << "recording to " << dir_ << " at " << fps << " fps" << std::endl;
        return true;
    }

    // Read the framebuffer currently bound for reading (w x h, RGBA8).
    void capture(int w, int h) {
        if (!active_) return;
        if (w != w_ || h != h_) { flush(); allocate(w, h); }
        int slot = captured_ % kRing;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[slot]);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        pending_[slot] = index_++;
        ++captured_;
        // The oldest outstanding frame is ready by now; collect it.
        if (captured_ - collected_ >= kRing - 1) collect();
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    void stop() {
        if (!active_) return;
        flush();
        { std::lock_guard<std::mutex> l(m_); quit_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
        workers_.clear();
        if (pbo_[0]) { glDeleteBuffers(kRing, pbo_); pbo_[0] = 0; }
        active_ = false;
        std::cout << "recorded " << written_ << " frames to " << dir_ << std::endl;
    }

private:
    static constexpr int kRing = 4;
    static constexpr size_t kMaxQueued = 16;
    struct Job { int index; int w, h; std::vector<unsigned char> rgba; };

    bool active_ = false;
    std::string dir_;
    int w_ = 0, h_ = 0;
    int index_ = 0, captured_ = 0, collected_ = 0;
    std::atomic<int> written_{0};
    GLuint pbo_[kRing] = {};
    int pending_[kRing] = {};

    std::vector<std::thread> workers_;
    std::queue<Job> jobs_;
    std::mutex m_;
    std::condition_variable cv_, space_;
    bool quit_ = false;

    void allocate(int w, int h) {
        if (!pbo_[0]) glGenBuffers(kRing, pbo_);
        for (int i = 0; i < kRing; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, size_t(w) * h * 4, nullptr, GL_STREAM_READ);
        }
        w_ = w; h_ = h; captured_ = collected_ = 0;
    }

    // Map the oldest outstanding PBO and queue its pixels for encoding.
    void collect() {
        int slot = collected_ % kRing;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[slot]);
        const void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, size_t(w_) * h_ * 4, GL_MAP_READ_BIT);
        if (p) {
            Job j{pending_[slot], w_, h_, std::vector<unsigned char>(size_t(w_) * h_ * 4)};
            std::memcpy(j.rgba.data(), p, j.rgba.size());
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            std::unique_lock<std::mutex> l(m_);
            space_.wait(l, [&] { return jobs_.size() < kMaxQueued; });
            jobs_.push(std::move(j));
            cv_.notify_one();
        }
        ++collected_;
    }

    void flush() {
        if (w_ == 0) return;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo_[0]);
        while (collected_ < captured_) collect();
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        std::unique_lock<std::mutex> l(m_);
        space_.wait(l, [&] { return jobs_.empty(); });
    }

    void work() {
        for (;;) {
            Job j;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [&] { return quit_ || !jobs_.empty(); });
                if (jobs_.empty()) return;
                j = std::move(jobs_.front());
                jobs_.pop();
                space_.notify_all();
            }
            // RGBA bottom-up -> RGB top-down.
            std::vector<unsigned char> rgb(size_t(j.w) * j.h * 3);
            for (int y = 0; y < j.h; ++y) {
                const unsigned char* src = &j.rgba[size_t(j.h - 1 - y) * j.w * 4];
                unsigned char* dst = &rgb[size_t(y) * j.w * 3];
                for (int x = 0; x < j.w; ++x, src += 4, dst += 3) { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; }
            }
            char name[32];
            snprintf(name, sizeof name, "frame_%06d.png", j.index);
            std::string path = dir_ + "/" + name;
            if (stbi_write_png(path.c_str(), j.w, j.h, 3, rgb.data(), j.w * 3)) ++written_;
            else std::cerr << "record: could not write " << path << std::endl;
        }
    }
};
#endif
