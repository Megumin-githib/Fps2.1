#include "particle_background.h"
#include <cstdlib>
#include <cmath>

using namespace Gdiplus;

namespace {
float RandRange(float lo, float hi) {
    return lo + (hi - lo) * (float)rand() / (float)RAND_MAX;
}
}

void ParticleBackground::Init(int width, int height) {
    m_width = width;
    m_height = height;
    srand(GetTickCount());
    Seed();
}

void ParticleBackground::Resize(int width, int height) {
    m_width = width;
    m_height = height;
    Seed(); // re-seed so blobs/particles stay within the new bounds
}

void ParticleBackground::Seed() {
    m_fog.clear();
    const int fogCount = 3;
    for (int i = 0; i < fogCount; ++i) {
        Fog f;
        f.x = RandRange(0.0f, (float)m_width);
        f.y = RandRange(0.0f, (float)m_height);
        f.radius = RandRange(m_width * 0.25f, m_width * 0.5f);
        f.driftX = RandRange(-0.06f, 0.06f);
        f.driftY = RandRange(-0.04f, 0.04f);
        m_fog.push_back(f);
    }

    m_particles.clear();
    const int particleCount = 48;
    for (int i = 0; i < particleCount; ++i) {
        Particle p;
        p.x = RandRange(0.0f, (float)m_width);
        p.y = RandRange(0.0f, (float)m_height);
        p.vx = RandRange(-0.12f, 0.12f);
        p.vy = RandRange(-0.18f, -0.03f); // gentle upward drift, like embers
        p.radius = RandRange(1.0f, 2.6f);
        p.phase = RandRange(0.0f, 6.2831853f);
        m_particles.push_back(p);
    }
}

void ParticleBackground::Update(float deltaSeconds) {
    if (deltaSeconds < 0.0f) deltaSeconds = 0.0f;
    if (deltaSeconds > 0.25f) deltaSeconds = 0.25f;
    const float scale = deltaSeconds / 0.016f;
    m_t += deltaSeconds;

    for (auto& f : m_fog) {
        f.x += f.driftX * scale;
        f.y += f.driftY * scale;
        if (f.x < -f.radius) f.x = (float)m_width + f.radius;
        if (f.x > m_width + f.radius) f.x = -f.radius;
        if (f.y < -f.radius) f.y = (float)m_height + f.radius;
        if (f.y > m_height + f.radius) f.y = -f.radius;
    }

    for (auto& p : m_particles) {
        p.x += p.vx * scale;
        p.y += p.vy * scale;
        if (p.y < -5) { p.y = (float)m_height + 5; p.x = RandRange(0.0f, (float)m_width); }
        if (p.x < -5) p.x = (float)m_width + 5;
        if (p.x > m_width + 5) p.x = -5;
    }
}

void ParticleBackground::Draw(Gdiplus::Graphics& g) const {
    // Base: a near-black vertical gradient with a faint cold-purple tint,
    // deepest at the top and bottom, slightly lighter through the middle -
    // reads as "mysterious" rather than flat black.
    Rect full(0, 0, m_width, m_height);
    LinearGradientBrush baseBrush(full,
        Color(255, 6, 6, 12),
        Color(255, 14, 10, 24),
        LinearGradientModeVertical);
    Gdiplus::REAL stops[3] = {0.0f, 0.5f, 1.0f};
    Color colors[3] = {
        Color(255, 5, 5, 10),
        Color(255, 16, 12, 28),
        Color(255, 4, 4, 9)
    };
    baseBrush.SetInterpolationColors(colors, stops, 3);
    g.FillRectangle(&baseBrush, full);

    // Soft drifting fog blobs - large, very low alpha radial gradients.
    for (auto& f : m_fog) {
        GraphicsPath path;
        path.AddEllipse(f.x - f.radius, f.y - f.radius, f.radius * 2, f.radius * 2);
        PathGradientBrush pgb(&path);
        pgb.SetCenterColor(Color(26, 90, 70, 150));
        Color surround[1] = {Color(0, 90, 70, 150)};
        INT count = 1;
        pgb.SetSurroundColors(surround, &count);
        g.FillPath(&pgb, &path);
    }

    // Glowing particles ("dust"/"embers"), pulsing gently.
    for (auto& p : m_particles) {
        float pulse = 0.5f + 0.5f * sinf(m_t * 1.3f + p.phase);
        BYTE alpha = (BYTE)(40 + pulse * 110);
        float r = p.radius * (0.8f + pulse * 0.6f);

        GraphicsPath path;
        path.AddEllipse(p.x - r * 2, p.y - r * 2, r * 4, r * 4);
        PathGradientBrush glow(&path);
        glow.SetCenterColor(Color(alpha, 170, 190, 255));
        Color surround[1] = {Color(0, 170, 190, 255)};
        INT count = 1;
        glow.SetSurroundColors(surround, &count);
        g.FillPath(&glow, &path);

        SolidBrush core(Color(alpha, 220, 230, 255));
        g.FillEllipse(&core, p.x - r, p.y - r, r * 2, r * 2);
    }
}
