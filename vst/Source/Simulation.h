#pragma once

#include <vector>
#include <random>
#include <cstdint>

// ============================================================================
// BreakoutMIDI physics simulation — a faithful, JUCE-free port of the engine
// that used to live in index.html. The plugin processor owns one of these and
// steps it on the audio thread, so the simulation keeps running (and emitting
// MIDI) whether or not the editor window is open.
//
// Coordinates are in the same pixel space the WebView canvas renders, so the
// UI can draw ball/brick positions directly. JS resolves colour/shape from the
// slot id, so render snapshots stay compact.
// ============================================================================
class Simulation
{
public:
    enum Shape { Rect = 0, Circle = 1, Polygon = 2 };
    enum Mode  { Billiards = 0, Hail = 1 };

    struct Slot
    {
        int   id        = 0;
        int   note      = 60;
        float prob      = 50.0f;   // weight for spawning
        bool  enabled   = true;
        int   velLock   = 0;       // 0 = use speed/fixed velocity
        int   durability = 1;      // hits to destroy
        int   shape     = Rect;
        float shapeW    = 72.0f, shapeH = 22.0f; // rect
        float shapeR    = 20.0f;                 // circle
        int   shapeSides = 6;       float shapeSize = 28.0f; // polygon
    };

    struct Edge { int note = 48; bool enabled = false; int velLock = 0; };

    // A brick placed by the level editor. spec.durability < 0 => permanent.
    struct LevelBrick { Slot spec; float x = 0, y = 0; };

    enum BrickSource { Random = 0, Level = 1 };

    enum MouseMode { MouseOff = 0, ForceField = 1, Cage = 2, Breakout = 3 };

    struct Params
    {
        float ballSpeed = 6.0f;
        float gravity   = 0.3f;
        int   numBalls  = 1;
        float spawnRate = 1.0f / 0.7f; // seconds between spawns
        int   maxBricks = 24;
        int   noteLen   = 200;         // ms
        int   midiChannel = 1;
        int   mode      = Billiards;
        bool  speedToVel = true;

        // mouse interaction
        int   mouseMode     = MouseOff;
        float forceStrength = 30.0f;   // 0..100
        bool  forceAttract  = false;   // false = repel
        float cageRadius    = 120.0f;
        float paddleSize    = 90.0f;
    };

    struct Config
    {
        Params            params;
        std::vector<Slot> slots;
        Edge              edges[4];   // top, right, bottom, left
        float             width  = 1000.0f;
        float             height = 600.0f;

        int                     brickSource = Random;
        std::vector<LevelBrick> level;          // placed bricks (level mode)
        int                     levelVersion = 0; // bumped by the editor on change
    };

    struct NoteEvent { int note = 0; int velocity = 0; int channel = 1; int durationMs = 200; };

    // Compact per-frame render state for the WebView.
    struct RenderBall  { float x = 0, y = 0; };
    struct RenderBrick { int slotId = 0; float cx = 0, cy = 0; bool alive = true; int hitsLeft = 1; int flash = 0; };
    struct RenderState
    {
        bool playing = false;
        std::vector<RenderBall>  balls;
        std::vector<RenderBrick> bricks;
    };

    Simulation();

    // Replace the whole configuration (called when the UI changes anything).
    // Preserves live balls/bricks where sensible; resizes ball count to match.
    void applyConfig (const Config& cfg);

    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing; }

    // Cursor position (sim/canvas pixels). Pushed every frame by the processor.
    void setMouse (float x, float y, bool active) { mouseX = x; mouseY = y; mouseActive = active; }

    void reset();                                   // rebuild balls, clear bricks

    // Advance by dtSeconds (fixed 1/60 steps are fed by the processor). Any
    // notes triggered are appended to outNotes.
    void step (double dtSeconds, std::vector<NoteEvent>& outNotes);

    // Fills dest in place (clears but keeps capacity) so the audio thread can
    // publish a snapshot without allocating. Reserve dest's vectors up front.
    void writeSnapshot (RenderState& dest) const;

private:
    struct Ball { float x = 0, y = 0, vx = 0, vy = 0, r = 7.0f; };
    struct Brick
    {
        Slot  slot;
        int   shape = Rect;
        float cx = 0, cy = 0;
        float w = 72, h = 22;       // rect
        float r = 20;               // circle
        float size = 28;            // polygon circumradius
        std::vector<std::pair<float,float>> verts; // polygon, relative to centre
        bool  alive = true;
        int   hitsLeft = 1;
        int   flash = 0;
        bool  permanent = false; // level bricks that never break
    };

    Config config;
    std::vector<Ball>  balls;
    std::vector<Brick> bricks;
    bool   playing = false;
    double spawnTimer = 0.0;
    std::mt19937 rng { 0x9E3779B9u };

    float mouseX = 0.0f, mouseY = 0.0f;
    bool  mouseActive = false;
    int   appliedLevelVersion = -1;
    void  buildLevel();

    void applyMouseForce (Ball& b);                                   // force field (per step)
    void applyCage       (Ball& b);                                   // cage wall (per substep)
    void applyPaddles    (Ball& b, std::vector<NoteEvent>& outNotes); // breakout (per substep)
    bool paddleHit (Ball& b, float left, float top, float w, float h); // circle/rect reflect

    float rand01();
    float brickBounds (const Slot& s) const;
    Ball  makeBall();
    void  syncBallCount();
    Brick makeBrick (const Slot& s, float cx, float cy);
    void  spawnBrick();
    const Slot* weightedRandomSlot();

    int  computeVelocity (int velLock, float impactSpeed) const;
    void emitNote (int note, int velLock, float impactSpeed, std::vector<NoteEvent>& outNotes);
    void resolveHit (Ball& ball, Brick& brick, float nx, float ny, float pen,
                     std::vector<NoteEvent>& outNotes);
    bool ballBrickCollision (Ball& ball, Brick& brick, std::vector<NoteEvent>& outNotes);
};
