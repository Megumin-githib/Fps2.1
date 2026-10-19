#include "particle_background.h"
#include <cstdlib>
#include <cmath>

using namespace Gdiplus;

namespace {
float RandRange(float lo, float hi) {
    return lo + (hi - lo) * (float)rand() / (float)RAND_MAX;
}

void SetAlphaMatrix(ColorMatrix& matrix, float alpha) {
    matrix = {};
    matrix.m[0][0] = 1.0f;
    matrix.m[1][1] = 1.0f;
    matrix.m[2][2] = 1.0f;
    matrix.m[3][3] = alpha;
    matrix.m[4][4] = 1.0f;
}
} // namespace

void ParticleBackground::Init(int width, int height) {
    m_width = width;
    m_height = height;
    srand(GetTickCount());
    Seed();
    CreateGlowTextures();
}

void ParticleBackground::Resize(int width, int height) {
    m_width = width;
    m_height = height;
    Seed(); // re-seed so blobs/particles stay within the new bounds
}

void ParticleBackground::CreateGlowTextures() {
    ReleaseResources();

    // These tiny radial textures replace dozens of GDI+ paths and gradient
    // brushes that were previously constructed and destroyed on every frame.
    m_fogTexture = new Bitmap(128, 128, PixelFormat32bppPARGB);
    if (m_fogTexture && m_fogTexture->GetLastStatus() == Ok) {
        Graphics g(m_fogTexture);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        GraphicsPath path;
        path.AddEllipse(1.0f, 1.0f, 126.0f, 126.0f);
        PathGradientBrush brush(&path);
        brush.SetCenterColor(Color(26, 90, 70, 150));
        Color surround[1] = { Color(0, 90, 70, 150) };
        INT count = 1;
        brush.SetSurroundColors(surround, &count);
        g.FillPath(&brush, &path);
    } else {
        delete m_fogTexture;
        m_fogTexture = nullptr;
    }

    m_particleTexture = new Bitmap(64, 64, PixelFormat32bppPARGB);
    if (m_particleTexture && m_particleTexture->GetLastStatus() == Ok) {
        Graphics g(m_particleTexture);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        GraphicsPath path;
        path.AddEllipse(1.0f, 1.0f, 62.0f, 62.0f);
        PathGradientBrush brush(&path);
        brush.SetCenterColor(Color(150, 170, 190, 255));
        Color surround[1] = { Color(0, 170, 190, 255) };
        INT count = 1;
        brush.SetSurroundColors(surround, &count);
        g.FillPath(&brush, &path);
    } else {
        delete m_particleTexture;
        m_particleTexture = nullptr;
    }
}

void ParticleBackground::ReleaseResources() {
    delete m_fogTexture;
    m_fogTexture = nullptr;
    delete m_particleTexture;
    m_particleTexture = nullptr;
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
    // Base: near-black vertical gradient with a faint cold-purple tint.
    Rect full(0, 0, m_width, m_height);
    LinearGradientBrush baseBrush(full,
        Color(255, 6, 6, 12),
        Color(255, 14, 10, 24),
        LinearGradientModeVertical);
    Gdiplus::REAL stops[3] = { 0.0f, 0.5f, 1.0f };
    Color colors[3] = {
        Color(255, 5, 5, 10),
        Color(255, 16, 12, 28),
        Color(255, 4, 4, 9)
    };
    baseBrush.SetInterpolationColors(colors, stops, 3);
    g.FillRectangle(&baseBrush, full);

    // Soft drifting fog. Reuse a pre-rendered radial sprite at varying sizes.
    if (m_fogTexture) {
        g.SetInterpolationMode(InterpolationModeBilinear);
        for (const auto& f : m_fog) {
            RectF dest(f.x - f.radius, f.y - f.radius, f.radius * 2.0f, f.radius * 2.0f);
            g.DrawImage(m_fogTexture, dest);
        }
    } else {
        for (const auto& f : m_fog) {
            GraphicsPath path;
            path.AddEllipse(f.x - f.radius, f.y - f.radius, f.radius * 2.0f, f.radius * 2.0f);
            PathGradientBrush brush(&path);
            brush.SetCenterColor(Color(26, 90, 70, 150));
            Color surround[1] = { Color(0, 90, 70, 150) };
            INT count = 1;
            brush.SetSurroundColors(surround, &count);
            g.FillPath(&brush, &path);
        }
    }

    // The cached radial sprite preserves the glow without per-frame path allocation.
    SolidBrush core(Color(40, 220, 230, 255));
    ImageAttributes particleAttrs;
    ColorMatrix matrix{};
    if (m_particleTexture) g.SetInterpolationMode(InterpolationModeBilinear);
    for (const auto& p : m_particles) {
        const float pulse = 0.5f + 0.5f * sinf(m_t * 1.3f + p.phase);
        const BYTE alpha = (BYTE)(40 + pulse * 110);
        const float r = p.radius * (0.8f + pulse * 0.6f);

        if (m_particleTexture) {
            SetAlphaMatrix(matrix, (float)alpha / 150.0f);
            particleAttrs.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
            RectF dest(p.x - r * 2.0f, p.y - r * 2.0f, r * 4.0f, r * 4.0f);
            g.DrawImage(m_particleTexture, dest, 0.0f, 0.0f, 64.0f, 64.0f,
                        UnitPixel, &particleAttrs);
        } else {
            GraphicsPath path;
            path.AddEllipse(p.x - r * 2.0f, p.y - r * 2.0f, r * 4.0f, r * 4.0f);
            PathGradientBrush glow(&path);
            glow.SetCenterColor(Color(alpha, 170, 190, 255));
            Color surround[1] = { Color(0, 170, 190, 255) };
            INT count = 1;
            glow.SetSurroundColors(surround, &count);
            g.FillPath(&glow, &path);
        }

        core.SetColor(Color(alpha, 220, 230, 255));
        g.FillEllipse(&core, p.x - r, p.y - r, r * 2.0f, r * 2.0f);
    }
}
