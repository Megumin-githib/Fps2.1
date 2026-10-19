#pragma once
#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>
#include <vector>

// A dark, atmospheric animated background: a near-black gradient, a few
// large slow-drifting "fog" blobs, and small glowing drifting particles
// ("embers"/"dust"). Purely decorative - used behind the full-screen
// launcher menu to give the exe a "mysterious", game-like opening screen.
// Deterministic-free (uses rand()), cheap enough to redraw every tick.
class ParticleBackground {
public:
    void Init(int width, int height);
    void Resize(int width, int height);
    void Update(float deltaSeconds = 0.016f); // time-based step; animation speed independent of FPS cap
    void Draw(Gdiplus::Graphics& g) const;

private:
    struct Fog {
        float x, y, radius, driftX, driftY;
    };
    struct Particle {
        float x, y, vx, vy, radius, phase;
    };

    void Seed();

    int m_width = 0, m_height = 0;
    std::vector<Fog> m_fog;
    std::vector<Particle> m_particles;
    float m_t = 0.0f; // animation clock, advances each Update()
};
