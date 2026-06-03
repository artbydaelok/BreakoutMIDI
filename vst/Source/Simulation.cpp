#include "Simulation.h"
#include <algorithm>
#include <cmath>

namespace
{
    template <typename T> T clampv (T v, T lo, T hi) { return std::max (lo, std::min (hi, v)); }
}

Simulation::Simulation()
{
    config.slots.push_back ({ 1, 60, 60.0f, true, 0, 1 });
    config.slots.push_back ({ 2, 64, 50.0f, true, 0, 1 });
    config.slots.push_back ({ 3, 67, 40.0f, true, 0, 1 });
    config.slots.push_back ({ 4, 72, 30.0f, true, 0, 1 });
    reset();
}

float Simulation::rand01()
{
    return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng);
}

float Simulation::brickBounds (const Slot& s) const
{
    if (s.shape == Circle)  return s.shapeR;
    if (s.shape == Polygon) return s.shapeSize;
    return std::max (s.shapeW, s.shapeH) * 0.5f;
}

Simulation::Ball Simulation::makeBall()
{
    Ball b;
    b.r = 7.0f;
    const float s = config.params.ballSpeed;

    if (config.params.mode == Hail)
    {
        b.x  = b.r + rand01() * (config.width - 2 * b.r);
        b.y  = -b.r;
        b.vx = (rand01() - 0.5f) * s * 0.4f;
        b.vy = s * 0.6f + rand01() * s * 0.4f;
        return b;
    }

    const float a = rand01() * 6.2831853f;
    b.x  = config.width  * 0.5f + (rand01() - 0.5f) * 80.0f;
    b.y  = config.height * 0.5f + (rand01() - 0.5f) * 80.0f;
    b.vx = std::cos (a) * s;
    b.vy = std::sin (a) * s;
    return b;
}

void Simulation::syncBallCount()
{
    const int want = std::max (0, config.params.numBalls);
    while ((int) balls.size() < want) balls.push_back (makeBall());
    while ((int) balls.size() > want) balls.pop_back();
}

Simulation::Brick Simulation::makeBrick (const Slot& s, float cx, float cy)
{
    Brick b;
    b.slot     = s;
    b.shape    = s.shape;
    b.cx       = cx;
    b.cy       = cy;
    b.hitsLeft = std::max (1, s.durability);
    b.alive    = true;
    b.flash    = 0;

    if (s.shape == Circle)
    {
        b.r = s.shapeR;
    }
    else if (s.shape == Polygon)
    {
        b.size = s.shapeSize;
        const int sides = std::max (3, s.shapeSides);
        for (int i = 0; i < sides; ++i)
        {
            const float a = ((float) i / sides) * 6.2831853f - 1.5707963f;
            b.verts.push_back ({ std::cos (a) * b.size, std::sin (a) * b.size });
        }
    }
    else // rect
    {
        b.w = s.shapeW;
        b.h = s.shapeH;
    }
    return b;
}

const Simulation::Slot* Simulation::weightedRandomSlot()
{
    float total = 0.0f;
    for (const auto& s : config.slots)
        if (s.enabled && s.prob > 0.0f) total += s.prob;

    if (total <= 0.0f) return nullptr;

    float r = rand01() * total;
    for (const auto& s : config.slots)
    {
        if (! s.enabled || s.prob <= 0.0f) continue;
        r -= s.prob;
        if (r <= 0.0f) return &s;
    }
    return nullptr;
}

void Simulation::spawnBrick()
{
    if ((int) bricks.size() >= config.params.maxBricks) return;

    const Slot* slot = weightedRandomSlot();
    if (slot == nullptr) return;

    const float pad = brickBounds (*slot) + 6.0f;
    const float cx  = pad + rand01() * (config.width  - pad * 2);
    const float cy  = pad + rand01() * (config.height * 0.75f - pad * 2);

    for (const auto& b : bricks)
    {
        const float dx = cx - b.cx, dy = cy - b.cy;
        if (std::sqrt (dx * dx + dy * dy) < pad + brickBounds (b.slot))
            return;
    }

    bricks.push_back (makeBrick (*slot, cx, cy));
}

int Simulation::computeVelocity (int velLock, float impactSpeed) const
{
    if (velLock > 0) return clampv (velLock, 1, 127);
    if (config.params.speedToVel)
        return clampv ((int) std::lround ((impactSpeed / 20.0f) * 107.0f + 20.0f), 1, 127);
    return 100;
}

void Simulation::emitNote (int note, int velLock, float impactSpeed, std::vector<NoteEvent>& out)
{
    NoteEvent e;
    e.note       = clampv (note, 0, 127);
    e.velocity   = computeVelocity (velLock, impactSpeed);
    e.channel    = clampv (config.params.midiChannel, 1, 16);
    e.durationMs = std::max (1, config.params.noteLen);
    out.push_back (e);
}

void Simulation::resolveHit (Ball& ball, Brick& brick, float nx, float ny, float pen,
                             std::vector<NoteEvent>& out)
{
    ball.x += nx * pen;
    ball.y += ny * pen;
    const float dot = ball.vx * nx + ball.vy * ny;
    if (dot < 0) { ball.vx -= 2 * dot * nx; ball.vy -= 2 * dot * ny; }

    float spd = std::sqrt (ball.vx * ball.vx + ball.vy * ball.vy);
    if (spd > 0)
    {
        ball.vx = ball.vx / spd * config.params.ballSpeed;
        ball.vy = ball.vy / spd * config.params.ballSpeed;
    }

    brick.hitsLeft -= 1;
    brick.flash = 12;
    if (brick.hitsLeft <= 0) brick.alive = false;

    emitNote (brick.slot.note, brick.slot.velLock, spd, out);
}

bool Simulation::ballBrickCollision (Ball& ball, Brick& brick, std::vector<NoteEvent>& out)
{
    if (brick.shape == Circle)
    {
        const float dx = ball.x - brick.cx, dy = ball.y - brick.cy;
        const float dist = std::sqrt (dx * dx + dy * dy);
        const float minD = ball.r + brick.r;
        if (dist >= minD) return false;
        const float nx = dist > 0 ? dx / dist : 0.0f;
        const float ny = dist > 0 ? dy / dist : 1.0f;
        resolveHit (ball, brick, nx, ny, minD - dist, out);
        return true;
    }

    if (brick.shape == Polygon)
    {
        const auto& verts = brick.verts;
        const auto n = verts.size();
        float minPen = 1e30f, bestNx = 0, bestNy = 1;

        const auto testAxis = [&] (float nx, float ny) -> bool
        {
            const float proj = (ball.x - brick.cx) * nx + (ball.y - brick.cy) * ny;
            float maxP = -1e30f;
            for (const auto& v : verts) maxP = std::max (maxP, v.first * nx + v.second * ny);
            const float pen = maxP + ball.r - proj;
            if (pen <= 0) return false;            // separating axis -> no hit
            if (pen < minPen) { minPen = pen; bestNx = nx; bestNy = ny; }
            return true;
        };

        for (size_t i = 0; i < n; ++i)
        {
            const auto& a = verts[i];
            const auto& b = verts[(i + 1) % n];
            const float ex = b.first - a.first, ey = b.second - a.second;
            const float len = std::sqrt (ex * ex + ey * ey); const float l = len > 0 ? len : 1.0f;
            if (! testAxis (ey / l, -ex / l)) return false;
        }
        for (const auto& v : verts)
        {
            const float dx = (ball.x - brick.cx) - v.first, dy = (ball.y - brick.cy) - v.second;
            const float d = std::sqrt (dx * dx + dy * dy); const float dd = d > 0 ? d : 1.0f;
            if (! testAxis (dx / dd, dy / dd)) return false;
        }

        resolveHit (ball, brick, bestNx, bestNy, minPen, out);
        return true;
    }

    // rect
    const float left = brick.cx - brick.w * 0.5f, top = brick.cy - brick.h * 0.5f;
    const float cpx = clampv (ball.x, left, left + brick.w);
    const float cpy = clampv (ball.y, top,  top  + brick.h);
    const float dx = ball.x - cpx, dy = ball.y - cpy;
    if (dx * dx + dy * dy >= ball.r * ball.r) return false;

    const float oL = (ball.x + ball.r) - left;
    const float oR = (left + brick.w) - (ball.x - ball.r);
    const float oT = (ball.y + ball.r) - top;
    const float oB = (top + brick.h) - (ball.y - ball.r);
    const float m = std::min (std::min (oL, oR), std::min (oT, oB));
    float nx = 0, ny = 0;
    if      (m == oL) nx = -1;
    else if (m == oR) nx =  1;
    else if (m == oT) ny = -1;
    else              ny =  1;
    resolveHit (ball, brick, nx, ny, m, out);
    return true;
}

void Simulation::applyMouseForce (Ball& b)
{
    const float dx = mouseX - b.x, dy = mouseY - b.y;
    const float dist = std::sqrt (dx * dx + dy * dy);
    if (dist < 1.0f) return;
    const float dir     = config.params.forceAttract ? 1.0f : -1.0f;
    const float falloff = 1.0f / (1.0f + dist / 140.0f);
    const float f       = config.params.forceStrength * 0.05f * falloff * dir;
    b.vx += (dx / dist) * f;
    b.vy += (dy / dist) * f;
    const float s    = std::sqrt (b.vx * b.vx + b.vy * b.vy);
    const float maxS = config.params.ballSpeed * 2.2f;
    if (s > maxS) { b.vx = b.vx / s * maxS; b.vy = b.vy / s * maxS; }
}

void Simulation::applyCage (Ball& b)
{
    const float dx = b.x - mouseX, dy = b.y - mouseY;
    float dist = std::sqrt (dx * dx + dy * dy);
    if (dist < 1.0e-3f) dist = 1.0e-3f;
    const float nx = dx / dist, ny = dy / dist;
    const float R = config.params.cageRadius;

    if (dist < R) // inside: keep trapped
    {
        const float maxD = R - b.r;
        if (dist > maxD)
        {
            b.x = mouseX + nx * maxD; b.y = mouseY + ny * maxD;
            const float dot = b.vx * nx + b.vy * ny;
            if (dot > 0) { b.vx -= 2 * dot * nx; b.vy -= 2 * dot * ny; }
        }
    }
    else          // outside: keep out
    {
        const float minD = R + b.r;
        if (dist < minD)
        {
            b.x = mouseX + nx * minD; b.y = mouseY + ny * minD;
            const float dot = b.vx * nx + b.vy * ny;
            if (dot < 0) { b.vx -= 2 * dot * nx; b.vy -= 2 * dot * ny; }
        }
    }
}

bool Simulation::paddleHit (Ball& b, float left, float top, float w, float h)
{
    const float cpx = clampv (b.x, left, left + w);
    const float cpy = clampv (b.y, top,  top  + h);
    const float dx = b.x - cpx, dy = b.y - cpy;
    if (dx * dx + dy * dy >= b.r * b.r) return false;

    const float oL = (b.x + b.r) - left;
    const float oR = (left + w) - (b.x - b.r);
    const float oT = (b.y + b.r) - top;
    const float oB = (top + h) - (b.y - b.r);
    const float m = std::min (std::min (oL, oR), std::min (oT, oB));
    float nx = 0, ny = 0;
    if      (m == oL) nx = -1;
    else if (m == oR) nx =  1;
    else if (m == oT) ny = -1;
    else              ny =  1;
    b.x += nx * m; b.y += ny * m;
    const float dot = b.vx * nx + b.vy * ny;
    if (dot < 0) { b.vx -= 2 * dot * nx; b.vy -= 2 * dot * ny; }
    return true;
}

void Simulation::applyPaddles (Ball& b, std::vector<NoteEvent>& out)
{
    const float W = config.width, H = config.height;
    const float len = config.params.paddleSize, half = len * 0.5f;
    const float thick = 10.0f, inset = 16.0f;
    const float mx = clampv (mouseX, half, W - half);
    const float my = clampv (mouseY, half, H - half);

    struct P { float left, top, w, h; int edge; };
    const P paddles[4] = {
        { mx - half,                  inset - thick * 0.5f, len,   thick, 0 }, // top   (mouse X)
        { W - inset - thick * 0.5f,   my - half,            thick, len,   1 }, // right (mouse Y)
        { mx - half,                  H - inset - thick * 0.5f, len, thick, 2 }, // bottom (mouse X)
        { inset - thick * 0.5f,       my - half,            thick, len,   3 }  // left  (mouse Y)
    };

    for (const auto& p : paddles)
        if (paddleHit (b, p.left, p.top, p.w, p.h))
            if (config.edges[p.edge].enabled)
                emitNote (config.edges[p.edge].note, config.edges[p.edge].velLock,
                          std::sqrt (b.vx * b.vx + b.vy * b.vy), out);
}

void Simulation::applyConfig (const Config& cfg)
{
    const int   prevMode  = config.params.mode;
    const float prevSpeed = config.params.ballSpeed;
    config = cfg;

    // a mode change re-seeds balls so they fit the new layout
    if (cfg.params.mode != prevMode) { reset(); return; }

    syncBallCount();

    // ball speed is per-ball state, so apply a change to every live ball now
    // (otherwise a ball only picks up the new speed on its next collision)
    if (std::abs (cfg.params.ballSpeed - prevSpeed) > 1.0e-4f)
    {
        for (auto& b : balls)
        {
            const float s = std::sqrt (b.vx * b.vx + b.vy * b.vy);
            if (s > 0) { b.vx = b.vx / s * cfg.params.ballSpeed; b.vy = b.vy / s * cfg.params.ballSpeed; }
            else       { b.vx = cfg.params.ballSpeed; b.vy = 0.0f; }
        }
    }
}

void Simulation::setPlaying (bool shouldPlay)
{
    playing = shouldPlay;
    if (playing && balls.empty()) reset();
}

void Simulation::reset()
{
    balls.clear();
    bricks.clear();
    spawnTimer = 0.0;
    for (int i = 0; i < std::max (0, config.params.numBalls); ++i)
        balls.push_back (makeBall());
}

void Simulation::step (double dt, std::vector<NoteEvent>& out)
{
    if (! playing) return;

    spawnTimer -= dt;
    if (spawnTimer <= 0.0) { spawnBrick(); spawnTimer = config.params.spawnRate; }

    const bool hail = config.params.mode == Hail;
    const float grav = hail ? config.params.gravity : 0.0f;
    const float W = config.width, H = config.height;

    for (int bi = (int) balls.size() - 1; bi >= 0; --bi)
    {
        Ball& ball = balls[(size_t) bi];
        ball.vy += grav * 30.0f * (float) dt;

        if (mouseActive && config.params.mouseMode == ForceField) applyMouseForce (ball);

        const float spd = std::sqrt (ball.vx * ball.vx + ball.vy * ball.vy);
        const int substeps = std::max (1, (int) std::ceil (spd / ball.r));
        const float svx = ball.vx / substeps, svy = ball.vy / substeps;
        bool respawned = false;

        for (int s = 0; s < substeps; ++s)
        {
            ball.x += svx; ball.y += svy;
            const float stepSpd = spd;

            if (hail)
            {
                if (ball.x - ball.r < 0) { ball.x = ball.r; ball.vx = std::abs (ball.vx); if (config.edges[3].enabled) emitNote (config.edges[3].note, config.edges[3].velLock, stepSpd, out); }
                if (ball.x + ball.r > W) { ball.x = W - ball.r; ball.vx = -std::abs (ball.vx); if (config.edges[1].enabled) emitNote (config.edges[1].note, config.edges[1].velLock, stepSpd, out); }
                if (ball.y - ball.r < 0) { ball.y = ball.r; ball.vy = std::abs (ball.vy); if (config.edges[0].enabled) emitNote (config.edges[0].note, config.edges[0].velLock, stepSpd, out); }
                if (ball.y - ball.r > H) { if (config.edges[2].enabled) emitNote (config.edges[2].note, config.edges[2].velLock, stepSpd, out); balls[(size_t) bi] = makeBall(); respawned = true; break; }
            }
            else
            {
                if (ball.x - ball.r < 0) { ball.x = ball.r; ball.vx = std::abs (ball.vx); if (config.edges[3].enabled) emitNote (config.edges[3].note, config.edges[3].velLock, stepSpd, out); }
                if (ball.x + ball.r > W) { ball.x = W - ball.r; ball.vx = -std::abs (ball.vx); if (config.edges[1].enabled) emitNote (config.edges[1].note, config.edges[1].velLock, stepSpd, out); }
                if (ball.y - ball.r < 0) { ball.y = ball.r; ball.vy = std::abs (ball.vy); if (config.edges[0].enabled) emitNote (config.edges[0].note, config.edges[0].velLock, stepSpd, out); }
                if (ball.y + ball.r > H) { ball.y = H - ball.r; ball.vy = -std::abs (ball.vy); if (config.edges[2].enabled) emitNote (config.edges[2].note, config.edges[2].velLock, stepSpd, out); }
            }

            for (auto& brick : bricks)
                if (brick.alive) ballBrickCollision (ball, brick, out);

            if (mouseActive)
            {
                if      (config.params.mouseMode == Cage)     applyCage (ball);
                else if (config.params.mouseMode == Breakout) applyPaddles (ball, out);
            }
        }

        if (respawned) continue;
    }

    // fade + cull dead bricks
    bricks.erase (std::remove_if (bricks.begin(), bricks.end(),
        [] (Brick& b) { b.flash = std::max (0, b.flash - 1); return ! (b.alive || b.flash > 0); }),
        bricks.end());
}

void Simulation::writeSnapshot (RenderState& dest) const
{
    dest.playing = playing;
    dest.balls.clear();
    for (const auto& b : balls) dest.balls.push_back ({ b.x, b.y });
    dest.bricks.clear();
    for (const auto& b : bricks)
        dest.bricks.push_back ({ b.slot.id, b.cx, b.cy, b.alive, b.hitsLeft, b.flash });
}
