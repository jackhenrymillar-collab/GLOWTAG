#pragma once

#include "Visualiser.h"
#include "../Engine/VisListener.h"
#include <map>
#include <string>

// The drawing side of the visualiser. Lives on the graphics thread: every frame it reads what the
// listener found, works out the six reactions, and draws the chosen mode.
namespace Vis
{
class Renderer
{
public:
    struct Out { unsigned int framebuffer; int x, y, w, h; float gain; };   // where the finished picture goes, in pixels

    void create (juce::String& error);
    void destroy();
    void frame (const Settings&, VisListener&, double latencySeconds, const Out&, juce::Point<float> drag, Readout&);
    bool ok = false;

private:
    struct Program
    {
        unsigned int id = 0;
        std::map<std::string, int> locations;
        bool build (const char* fragment, juce::String& error);
        int u (const char* name);
    };
    struct Target
    {
        unsigned int tex = 0, fbo = 0;
        int w = 0, h = 0;
        void make (int width, int height);
        void release();
    };
    struct Splat { float x, y, r, amount; float colour[3]; };
    struct Onset { double time; float strength, phase; };
    struct Edge { bool high = false; double time = -9.0; };

    void analyse (float dt, const Settings&, VisListener&, double latency, Readout&);
    void fire (int kind, float rel, const Settings&, Readout&);
    void hitSignal (int source, float strength, const Settings&);
    void burst (float strength, const Settings&);
    void common (Program&, const Settings&, float aspect);
    void trailOut (int W, int H, float dt, float aspect, float amount, bool echo, float psy, float zoom, const Out&);
    void ensureSim (float aspect);
    void ensureTrail (int w, int h);

    Program inkStep, inkShow, trail, liquidProg, sandProg, bloomStep, bloomShow, out;
    Target sim[2], trails[2], fresh;
    int simIndex = 0, trailIndex = 0;
    unsigned int vao = 0, vbo = 0;

    double T = 0.0, lastTime = 0.0, lastTransient = -9.0, sandTime = -9.0, bloomTime = 0.0, levelTime = 0.0;
    float ph = 0.0f, theta = 0.0f, omega = 0.0f, spinRate = 0.0f, fluidRate = 0.0f, twist = 0.0f, echoTime = 0.0f;
    float liquidPhase = 0.0f, liquidLast = -1.0f, bloomSteps = 0.0f;
    int sandA = 0, sandB = 1, sandCount = 0, lastStyle = -1;
    bool seed = true;
    std::array<float, numSources> signal {};
    std::array<float, numDests> reaction {};
    std::array<Edge, numSources> edges;
    float band[3] = {}, peak[3] = { 0.12f, 0.12f, 0.12f }, level = 0.0f;
    VisListener::Bands rawBands { 0.0, 0.0f, 0.0f, 0.0f };
    std::vector<VisListener::Hit> hitQueue;
    std::vector<VisListener::Bands> bandQueue;
    std::vector<Splat> splats;
    std::vector<Onset> onsets;
    juce::Random random;
};
}
