#pragma once

#include "globals.h"
#include "physics_object.hpp"
// #include "boundary.hpp"
#include "boundary.hpp"
#include "threadpool.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <set>


struct PhysicSolver
{
    std::array<PhysicsObject,MAX_OBJECTS> objects;
    std::array<std::mutex,MAX_OBJECTS> object_locks;
    std::array<bool,MAX_OBJECTS> has_obj;
    std::set<int> no_obj;
    

    std::vector<Boundary*> boundary;

    // glm::vec4                   gravity = {0.0f, -20.0f, 0.0f, 0.0f};
    glm::vec4                   gravity = {0.0f, 0.0f, 0.0f, 0.0f};

    // Simulation solving pass count
    uint32_t        sub_steps;

    tp::ThreadPool& thread_pool;

    PhysicSolver(tp::ThreadPool& tp): sub_steps{8}, thread_pool{tp}
    {
        for(int i=0;i<MAX_OBJECTS;i++) {
            no_obj.insert(i);
        }
    }

    PhysicSolver(tp::ThreadPool& tp, Boundary *bound): sub_steps{8}, thread_pool{tp}
    {
        for(int i=0;i<MAX_OBJECTS;i++) {
            no_obj.insert(i);
        }
        boundary.push_back(bound);
    }

    // Checks if two atoms are colliding and if so create a new contact
    void solveContact(uint32_t atom_1_idx, uint32_t atom_2_idx)
    {
        PhysicsObject& obj_1 = objects[atom_1_idx];
        PhysicsObject& obj_2 = objects[atom_2_idx];

        if (obj_1.hidden || obj_2.hidden) return;
        if (!obj_1.dynamic && !obj_2.dynamic) return;

        const glm::vec4 o2_o1 = obj_1.position - obj_2.position;
        const float dist2 = glm::dot(o2_o1, o2_o1);

        const float combined_radius = obj_1.radius + obj_2.radius;

        if (dist2 < combined_radius * combined_radius && dist2 > EPS) {
            const float dist = std::sqrt(dist2);
            const float penetration = (combined_radius - dist);// / combined_radius;

            if (penetration > 0.0f) {
                const float w1 = obj_1.dynamic ? obj_1.radius*obj_1.radius*obj_1.radius*obj_1.radius : 0.0f;
                const float w2 = obj_2.dynamic ? obj_2.radius*obj_2.radius*obj_2.radius*obj_2.radius : 0.0f;

                const float delta = RESPONSE_COEF * penetration;
                obj_1.position += o2_o1 * (RESPONSE_COEF * penetration * w2) / ((w1+w2)*dist);
                obj_2.position -= o2_o1 * (RESPONSE_COEF * penetration * w1) / ((w1+w2)*dist);

                // if (obj_1.just_spawned || obj_2.just_spawned){
                //     obj_1.last_position = obj_1.position;
                //     obj_2.last_position = obj_2.position;
                // }
            }
        }
    }

    void solveContactSafe(uint32_t i, uint32_t j)
    {
        if (i == j) return;
        if (!has_obj[i] || !has_obj[j]) return;

        // Lock consistently to avoid deadlocks
        uint32_t first = std::min(i, j);
        uint32_t second = std::max(i, j);

        std::scoped_lock lock1(object_locks[first]);
        std::scoped_lock lock2(object_locks[second]);

        solveContact(i, j); // Your original logic
    }

    // Find colliding atoms
    void solveCollisions()
    {
        const uint32_t N = static_cast<uint32_t>(objects.size());
        const uint32_t thread_count = thread_pool.m_thread_count;

        // Total number of (i,j) pairs where i < j
        const uint64_t total_pairs = (static_cast<uint64_t>(N) * (N - 1)) / 2;
        const uint64_t pairs_per_thread = (total_pairs + thread_count - 1) / thread_count;

        std::atomic<uint64_t> pair_index{0};

        for (uint32_t t = 0; t < thread_count; ++t) {
            thread_pool.addTask([&, t] {
                uint64_t start_idx = t * pairs_per_thread;
                uint64_t end_idx = std::min(start_idx + pairs_per_thread, total_pairs);

                for (uint64_t k = start_idx; k < end_idx; ++k) {
                    // Map linear index k to (i, j) using triangular number math
                    uint32_t i = static_cast<uint32_t>(
                        N - 2 - static_cast<uint32_t>(std::floor(std::sqrt(-8.0 * k + 4.0 * N * (N - 1) - 7) / 2.0 - 0.5))
                    );
                    uint32_t j = static_cast<uint32_t>(k + i + 1 - (static_cast<uint64_t>(N) * (N - 1) / 2 - static_cast<uint64_t>((N - i) * (N - i - 1)) / 2));

                    solveContactSafe(i, j);
                }
            });
        }

        thread_pool.waitForCompletion();
    }

    // Add a new object to the solver
    void addObject(const PhysicsObject& object)
    {
        int i = *no_obj.begin();
        if (no_obj.begin()!=no_obj.end()) {
            no_obj.erase(no_obj.begin());
        }
        objects[i] = object;
        has_obj[i] = true;
    }
    void removeObject(int i)
    {
        // no_obj.erase(no_obj.find(i));
        auto it = no_obj.find(i);
        if (it != no_obj.end()) {
            no_obj.erase(it);
        }
        objects[i].disable();
        has_obj[i] = false;
    }

    // // Add a new object to the solver
    // uint64_t createObject(glm::vec4 pos)
    // {
    //     objects.emplace_back(pos);
    //     object_locks.emplace_back();
    //     return objects.size() - 1;
    // }

    void update(float dt)
    {
        // Perform the sub steps
        const float sub_dt = dt / static_cast<float>(sub_steps);
        for (uint32_t i(sub_steps); i--;) {
            solveCollisions();
            updateBoundary_multi(sub_dt);
        }
    }

    void updateBoundary_multi(float dt)
    {
        thread_pool.dispatch(static_cast<uint32_t>(objects.size()), [&](uint32_t start, uint32_t end) {
            for (uint32_t i = start; i < end; ++i) {
                objects[i].acceleration += gravity;
                objects[i].update(dt);
            }
        });
        for (const auto& bound_obj : boundary) {
            thread_pool.dispatch(static_cast<uint32_t>(objects.size()), [&](uint32_t start, uint32_t end) {
                for (uint32_t i = start; i < end; ++i) {
                    bound_obj->enforce(objects[i]);
                }
            });
        }
    }
};