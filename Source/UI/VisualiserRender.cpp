#include "VisualiserRender.h"
#include "VisualiserShaders.inc"

using namespace juce::gl;

namespace Vis
{
static float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }

//==============================================================================
// Settings

static const float faderDefaults[] = {
   #define X(name, def, label, fmt) def,
    VIS_FADERS (X)
   #undef X
};
static const char* const faderNames[] = {
   #define X(name, def, label, fmt) #name,
    VIS_FADERS (X)
   #undef X
};
static const char* const faderLabels[] = {
   #define X(name, def, label, fmt) label,
    VIS_FADERS (X)
   #undef X
};
static const char* const faderFormats[] = {
   #define X(name, def, label, fmt) #fmt,
    VIS_FADERS (X)
   #undef X
};
static const int choiceDefaults[] = {
   #define Y(name, def) def,
    VIS_CHOICES (Y)
   #undef Y
};
static const char* const choiceNames[] = {
   #define Y(name, def) #name,
    VIS_CHOICES (Y)
   #undef Y
};
static const char* const styleNames[] = { "ink", "liquid", "sand", "bloom", "kaleido", "lotus", "picture" };

const char* faderLabel (int fader) { return faderLabels[fader]; }

// The psychedelic LFO: one swing takes 60 seconds at the bottom of its fader, down to 2 at the top
static float lfoSeconds (float v) { return 60.0f * std::pow (1.0f / 30.0f, v); }

juce::String faderText (int fader, float v)
{
    const juce::String f (faderFormats[fader]);
    if (f == "hue") return juce::String (juce::roundToInt (v * 360.0f)) + juce::String::fromUTF8 ("\xc2\xb0");
    if (f == "seg") return juce::String (juce::roundToInt (3.0f + v * 9.0f));
    if (f == "vort") return juce::String (juce::roundToInt (v * 6.0f));
    if (f == "cnt") return juce::String (4 + juce::roundToInt (v * 8.0f));
    if (f == "lfo") { const float sec = lfoSeconds (v); return (sec < 10.0f ? juce::String (sec, 1) : juce::String (juce::roundToInt (sec))) + " s"; }
    if (f == "ms") { const int m = juce::roundToInt ((v - 0.5f) * 160.0f); return (m > 0 ? "+" : "") + juce::String (m) + " ms"; }
    return juce::String (juce::roundToInt (v * 100.0f)) + "%";
}

void Settings::resetWiring()
{
    for (auto& row : route) row.fill (0.0f);
    route[sBig][dPulse] = 1.0f;  route[sLow][dPulse] = 0.4f;
    route[sBig][dFlash] = 1.0f;  route[sSnare][dFlash] = 0.6f;
    route[sBig][dSpin] = 1.0f;
    route[sBig][dStir] = 1.0f;   route[sLow][dStir] = 1.0f;
    route[sBig][dBurst] = 1.0f;
}

void Settings::resetBands()
{
    bandHz = { { { 35.0f, 140.0f }, { 200.0f, 2000.0f }, { 3000.0f, 12000.0f } } };
}

void Settings::reset()
{
    for (int i = 0; i < F::count; ++i) v[(size_t) i] = faderDefaults[i];
    for (int i = 0; i < C::count; ++i) w[(size_t) i] = choiceDefaults[i];
    style = ink;
    resetWiring();
    resetBands();
}

juce::var Settings::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("style", styleNames[juce::jlimit (0, numStyles - 1, style)]);
    auto* fv = new juce::DynamicObject();
    for (int i = 0; i < F::count; ++i) fv->setProperty (faderNames[i], v[(size_t) i]);
    auto* cv = new juce::DynamicObject();
    for (int i = 0; i < C::count; ++i) cv->setProperty (choiceNames[i], w[(size_t) i]);
    juce::Array<juce::var> wires;
    for (int s = 0; s < numSources; ++s)
        for (int d = 0; d < numDests; ++d)
            if (route[(size_t) s][(size_t) d] > 0.0f)
            {
                juce::Array<juce::var> wire;
                wire.add (s); wire.add (d); wire.add (route[(size_t) s][(size_t) d]);
                wires.add (wire);
            }
    o->setProperty ("faders", juce::var (fv));
    o->setProperty ("choices", juce::var (cv));
    o->setProperty ("wires", wires);
    juce::Array<juce::var> ranges;
    for (auto& b : bandHz) { juce::Array<juce::var> r; r.add (b[0]); r.add (b[1]); ranges.add (r); }
    o->setProperty ("bands", ranges);
    return juce::var (o);
}

void Settings::fromVar (const juce::var& var)
{
    if (! var.isObject()) return;
    reset();
    for (int i = 0; i < numStyles; ++i)
        if (var["style"].toString() == styleNames[i]) style = i;
    for (int i = 0; i < F::count; ++i)
        if (var["faders"].hasProperty (faderNames[i])) v[(size_t) i] = clamp01 ((float) var["faders"][faderNames[i]]);
    for (int i = 0; i < C::count; ++i)
        if (var["choices"].hasProperty (choiceNames[i])) w[(size_t) i] = juce::jlimit (0, 4, (int) var["choices"][choiceNames[i]]);
    if (auto* wires = var["wires"].getArray())
    {
        for (auto& row : route) row.fill (0.0f);
        for (auto& wire : *wires)
            if (wire.size() == 3)
            {
                const int s = wire[0], d = wire[1];
                if (s >= 0 && s < numSources && d >= 0 && d < numDests) route[(size_t) s][(size_t) d] = clamp01 ((float) wire[2]);
            }
    }
    if (auto* ranges = var["bands"].getArray())
        for (int k = 0; k < 3 && k < ranges->size(); ++k)
        {
            const auto& r = ranges->getReference (k);
            if (r.size() != 2) continue;
            const float a = juce::jlimit (lowestHz, highestHz / 1.25f, (float) r[0]);
            bandHz[(size_t) k] = { a, juce::jlimit (a * 1.25f, highestHz, (float) r[1]) };
        }
}

// Kaleidoscope and lotus have no mirror of their own (their mirrors are their form), so they answer with ink's
int Settings::hueKey() const       { static const int k[] = { F::hue, F::lhue, F::shue, F::bhue, F::khue, F::zhue, F::phue };                         return k[style]; }
int Settings::tintKey() const      { static const int k[] = { F::tint, F::ltint, F::stint, F::btint, F::ktint, F::ztint, F::ptint };                  return k[style]; }
int Settings::psyKey() const       { static const int k[] = { F::psy, F::lpsy, F::spsy, F::bpsy, F::kpsy, F::zpsy, F::ppsy };                         return k[style]; }
int Settings::trailsKey() const    { static const int k[] = { F::trails, F::ltrails, F::strails, F::btrails, F::ktrails, F::ztrails, F::ptrails };    return k[style]; }
int Settings::spinKey() const      { static const int k[] = { F::spin, F::lspin, F::sspin, F::bspin, F::kspin, F::zspin, F::pspin };                  return k[style]; }
int Settings::idleKey() const      { static const int k[] = { F::idle, F::lidle, F::sidle, F::bidle, F::kidle, F::zidle, F::pidle };                  return k[style]; }
int Settings::tonesKey() const     { static const int k[] = { C::tones, C::ltones, C::stones, C::btones, C::ktones, C::ztones, C::ptones };           return k[style]; }
int Settings::trailModeKey() const { static const int k[] = { C::trails, C::ltrails, C::strails, C::btrails, C::ktrails, C::ztrails, C::ptrails };    return k[style]; }
int Settings::surfaceKey() const   { static const int k[] = { C::surface, C::lsurface, C::ssurface, C::bsurface, C::ksurface, C::zsurface, C::psurface }; return k[style]; }
int Settings::mirrorKey() const    { static const int k[] = { C::mirror, C::lmirror, C::smirror, C::bmirror, C::mirror, C::mirror, C::pmirror };      return k[style]; }
int Settings::formKey() const      { static const int k[] = { C::form, C::lform, C::sform, C::btype, C::kform, C::zform, C::pform };                  return k[style]; }

static juce::Colour hsl (float h, float s, float l)
{
    h = std::fmod (std::fmod (h, 360.0f) + 360.0f, 360.0f) / 360.0f;
    auto f = [&] (float n)
    {
        const float k = std::fmod (n + h * 12.0f, 12.0f), a = s * juce::jmin (l, 1.0f - l);
        return l - a * juce::jmax (-1.0f, juce::jmin (k - 3.0f, 9.0f - k, 1.0f));
    };
    return juce::Colour::fromFloatRGBA (f (0), f (8), f (4), 1.0f);
}

// One hue makes the whole palette: shades of it, or it with its complement, a split pair or a triad
std::array<juce::Colour, 3> Settings::palette (float shift) const
{
    const float h = v[(size_t) hueKey()] * 360.0f + shift, t = v[(size_t) tintKey()];
    switch (w[(size_t) tonesKey()])
    {
        case 0:  return { hsl (h, 0.95f, 0.5f), hsl (h - 60.0f * t, 0.9f, 0.58f), hsl (h + 22.0f * t, 0.95f, 0.46f) };
        case 2:  return { hsl (h, 0.92f, 0.56f), hsl (h + 150.0f, 0.92f, 0.55f), hsl (h + 210.0f, 0.92f, 0.58f) };
        case 3:  return { hsl (h, 0.92f, 0.56f), hsl (h + 120.0f, 0.92f, 0.55f), hsl (h + 240.0f, 0.92f, 0.58f) };
        default: return { hsl (h, 0.92f, 0.56f), hsl (h + 180.0f, 0.92f, 0.55f), hsl (h - 30.0f * t, 0.92f, 0.6f) };
    }
}

//==============================================================================
// Drawing

// The last step: the trails and the fresh picture together, darker towards the corners, dimmed when it sits behind the app
static const char* const outShader = R"GLSL(#version 330 core
in vec2 uv;out vec4 o;uniform sampler2D tex,cur;uniform float gain;
void main(){vec2 cc=uv-.5;o=vec4(max(texture(tex,uv).rgb,texture(cur,uv).rgb)*(1.-.55*dot(cc,cc))*gain,1.);})GLSL";

bool Renderer::Program::build (const char* fragment, juce::String& error)
{
    auto compile = [&] (GLenum type, const char* source) -> GLuint
    {
        const GLuint sh = glCreateShader (type);
        glShaderSource (sh, 1, &source, nullptr);
        glCompileShader (sh);
        GLint built = 0;
        glGetShaderiv (sh, GL_COMPILE_STATUS, &built);
        if (! built)
        {
            char log[1024] = {};
            glGetShaderInfoLog (sh, sizeof (log) - 1, nullptr, log);
            error << log << "\n";
        }
        return sh;
    };
    const GLuint vs = compile (GL_VERTEX_SHADER, VisShaders::VS), fs = compile (GL_FRAGMENT_SHADER, fragment);
    id = glCreateProgram();
    glAttachShader (id, vs);
    glAttachShader (id, fs);
    glBindAttribLocation (id, 0, "p");
    glLinkProgram (id);
    GLint linked = 0;
    glGetProgramiv (id, GL_LINK_STATUS, &linked);
    glDeleteShader (vs);
    glDeleteShader (fs);
    locations.clear();
    return linked != 0;
}

GLint Renderer::Program::u (const char* name)
{
    auto it = locations.find (name);
    if (it != locations.end()) return it->second;
    const GLint loc = glGetUniformLocation (id, name);
    locations[name] = loc;
    return loc;
}

void Renderer::Target::make (int width, int height)
{
    release();
    w = width; h = height;
    glGenTextures (1, &tex);
    glBindTexture (GL_TEXTURE_2D, tex);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glGenFramebuffers (1, &fbo);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glClearColor (0, 0, 0, 1);
    glClear (GL_COLOR_BUFFER_BIT);
}

void Renderer::Target::release()
{
    if (tex != 0) glDeleteTextures (1, &tex);
    if (fbo != 0) glDeleteFramebuffers (1, &fbo);
    tex = fbo = 0; w = h = 0;
}

void Renderer::create (juce::String& error)
{
    glGenVertexArrays (1, &vao);
    glBindVertexArray (vao);
    glGenBuffers (1, &vbo);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    static const float tri[] = { -1, -1, 3, -1, -1, 3 };
    glBufferData (GL_ARRAY_BUFFER, sizeof (tri), tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    ok = inkStep.build (VisShaders::INK_STEP, error);
    ok = inkShow.build (VisShaders::INK_SHOW, error) && ok;
    ok = trail.build (VisShaders::TRAIL, error) && ok;
    ok = liquidProg.build (VisShaders::LIQ, error) && ok;
    ok = sandProg.build (VisShaders::SAND, error) && ok;
    ok = bloomStep.build (VisShaders::BL_STEP, error) && ok;
    ok = bloomShow.build (VisShaders::BL_SHOW, error) && ok;
    ok = kalProg.build (VisShaders::KAL, error) && ok;
    ok = lotusProg.build (VisShaders::LOTUS, error) && ok;
    ok = picStep.build (VisShaders::PIC_STEP, error) && ok;
    ok = picShow.build (VisShaders::PIC_SHOW, error) && ok;
    ok = out.build (outShader, error) && ok;
    lastTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    seed = true;
}

void Renderer::destroy()
{
    for (auto* p : { &inkStep, &inkShow, &trail, &liquidProg, &sandProg, &bloomStep, &bloomShow, &kalProg, &lotusProg, &picStep, &picShow, &out })
        if (p->id != 0) { glDeleteProgram (p->id); p->id = 0; }
    for (auto* t : { &sim[0], &sim[1], &trails[0], &trails[1], &fresh }) t->release();
    if (picTex != 0) glDeleteTextures (1, &picTex);
    if (picDistTex != 0) glDeleteTextures (1, &picDistTex);
    picTex = picDistTex = 0;
    picture.reset();
    if (vbo != 0) glDeleteBuffers (1, &vbo);
    if (vao != 0) glDeleteVertexArrays (1, &vao);
    vbo = vao = 0;
}

void Renderer::ensureSim (float aspect)
{
    const int h = 300, w = juce::jmax (8, juce::roundToInt (300.0f * aspect));
    if (sim[0].w == w && sim[0].h == h) return;
    sim[0].make (w, h);
    sim[1].make (w, h);
    simIndex = 0;
    seed = true;
}

void Renderer::clearSim()
{
    for (auto& t : sim)
    {
        if (t.fbo == 0) continue;
        glBindFramebuffer (GL_FRAMEBUFFER, t.fbo);
        glClearColor (0, 0, 0, 1);
        glClear (GL_COLOR_BUFFER_BIT);
    }
}

// The picture goes to the graphics card once, when it changes: its colours (with the shape in the fourth
// channel) at several sizes, so the shaders can read it soft or sharp, and the distance from the shape's outline
void Renderer::usePicture (const std::shared_ptr<const Picture>& p)
{
    if (p == picture || p == nullptr) return;
    picture = p;
    auto make = [] (GLuint& tex, bool sizes)
    {
        if (tex == 0) glGenTextures (1, &tex);
        glBindTexture (GL_TEXTURE_2D, tex);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sizes ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
    make (picTex, true);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, Picture::size, Picture::size, 0, GL_RGBA, GL_UNSIGNED_BYTE, p->pixels.data());
    glGenerateMipmap (GL_TEXTURE_2D);
    make (picDistTex, false);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RG16F, Picture::grid, Picture::grid, 0, GL_RG, GL_FLOAT, p->distance.data());
    glBindTexture (GL_TEXTURE_2D, 0);
}

void Renderer::Slow::follow (float dt, const float* band, float levelNow)
{
    auto ease = [dt] (float a, float b, float up, float down) { return a + (b - a) * (1.0f - std::exp (-dt * (b > a ? up : down))); };
    for (int i = 0; i < 3; ++i) sb[i] = ease (sb[i], band[i], 2.2f, 0.8f);
    level = ease (level, levelNow, 2.0f, 0.8f);
    hit *= std::exp (-dt * 1.1f);
    hitEased = ease (hitEased, hit, 3.0f, 1.2f);
}

// Psychedelic moves two ways: a slow triangle LFO swings it at a steady pace, evenly above and below the
// fader, and sway lets the chosen part of the music push it up
float Renderer::Slow::psy (float dt, float base, float lfoDepth, float rate, float sway, int swayWith)
{
    lfo = std::fmod (lfo + dt * juce::MathConstants<float>::twoPi / lfoSeconds (rate), juce::MathConstants<float>::twoPi);
    const float with[] = { sb[0], sb[1], sb[2], hitEased };
    psyNow = juce::jlimit (-1.0f, 1.0f, base + lfoDepth * std::asin (std::sin (lfo)) * 0.63662f + sway * with[juce::jlimit (0, 3, swayWith)] * 1.4f);
    psyPhase += dt * 0.5f * psyNow;
    return psyNow;
}

// Colour changes: swap counts the hits and eases from one to the next; flow drifts faster when the music is louder
void Renderer::Slow::colours (float dt, float wash)
{
    if (swaps - swapEased > 2.0f) swaps = std::floor (swapEased) + 2.0f;
    swapEased += juce::jmin (swaps - swapEased, dt * 1.5f);
    flow += dt * wash * (0.03f + level * 0.9f);
}

void Renderer::ensureTrail (int w, int h)
{
    if (trails[0].w == w && trails[0].h == h) return;
    trails[0].make (w, h);
    trails[1].make (w, h);
    fresh.make (w, h);
    trailIndex = 0;
}

//==============================================================================
// What the music is doing, turned into the six reactions

void Renderer::burst (float strength, const Settings& S)
{
    if (strength < 0.05f) return;
    if (S.style == sand)
    {
        static const int every[] = { 4, 3, 2, 1 };
        if (++sandCount % every[juce::jlimit (0, 3, juce::roundToInt (S.v[F::schange] * 3.0f))] == 0)
        {
            const int max = 2 + juce::roundToInt (S.v[F::scomp] * 10.0f);
            int next;
            do { next = random.nextInt (max); } while (next == sandB);
            sandA = sandB; sandB = next; sandTime = T;
        }
    }
    if (S.style == kaleido)
    {
        // Every burst turns the tube a notch, so the shards tumble into a new picture; now and then it turns back the other way
        ++kal.count;
        kal.s.swaps += 1.0f;
        kal.s.hit = juce::jmin (1.0f, kal.s.hit + 0.45f + strength * 0.4f);
        if (kal.count % 7 == 0) kal.dir = -kal.dir;
        kal.turnTo += (0.3f + strength * 0.9f) * S.v[F::ktumble] * 1.5f * kal.dir;
    }
    else if (S.style == lotus)
    {
        // A hit pushes you deeper for a moment: the speed follows the push up and falls back, a lunge and never a jump
        lot.push += strength * S.v[F::zlunge] * 1.2f;
        lot.s.swaps += 1.0f;
        lot.s.hit = juce::jmin (1.0f, lot.s.hit + 0.45f + strength * 0.4f);
    }
    else if (S.style == pictureStyle)
    {
        pic.s.hit = juce::jmin (1.0f, pic.s.hit + 0.45f + strength * 0.4f);
        pic.puff = juce::jmin (1.6f, pic.puff + 0.4f + strength);
        pic.beatSpeed += (0.4f + strength) * 11.0f;
    }
    onsets.insert (onsets.begin(), { T, strength, ph });
    if (onsets.size() > 6) onsets.resize (6);
    if (S.style == ink && sim[0].h > 0)
    {
        // With a mirror the middle of the screen is the corner of the ink, so bursts start near the fold
        const int m = S.w[C::mirror];
        const float a = (float) sim[0].w / (float) sim[0].h;
        const float x = m > 0 ? random.nextFloat() * a * 0.35f : a * (0.3f + random.nextFloat() * 0.4f);
        const float y = m >= 2 ? random.nextFloat() * 0.35f : 0.3f + random.nextFloat() * 0.4f;
        const int ch = random.nextInt (2);
        splats.push_back ({ x, y, 0.09f + 0.12f * strength, (0.5f + strength) * (0.35f + S.v[F::drops]), { ch == 0 ? 1.0f : 0.0f, ch == 1 ? 1.0f : 0.0f, 0.0f } });
        if (strength > 0.5f) splats.push_back ({ x + 0.05f, y + 0.05f, 0.04f, 0.6f * S.v[F::drops] + 0.2f, { 0.0f, 0.0f, 1.0f } });
    }
}

void Renderer::hitSignal (int source, float strength, const Settings& S)
{
    signal[(size_t) source] = juce::jmax (signal[(size_t) source], strength);
    if (source == sTrans) lastTransient = T;
    for (int d = 0; d < numDests; ++d)
    {
        const float amount = S.route[(size_t) source][(size_t) d];
        if (amount <= 0.0f) continue;
        if (d == dSpin) omega += S.v[(size_t) S.spinKey()] * (0.4f + strength) * 2.6f * amount;
        if (d == dBurst) burst (strength * amount, S);
    }
}

// One found hit can feed several sounds at once: kick, loudest lows and transients are separate
void Renderer::fire (int kind, float rel, const Settings& S, Readout& readout)
{
    const float strength = clamp01 (rel * (0.55f + S.v[F::react] * 0.8f));
    const bool big = kind == VisListener::kick && rel >= S.v[F::gate] - 0.001f;
    if (kind == VisListener::kick)
    {
        const juce::SpinLock::ScopedLockType lock (readout.lock);
        if (readout.numKicks == (int) readout.kicks.size())
        {
            std::move (readout.kicks.begin() + 1, readout.kicks.end(), readout.kicks.begin());
            --readout.numKicks;
        }
        readout.kicks[(size_t) readout.numKicks++] = { T, rel, big };
    }
    // Every sharp hit is a transient; only those at or above its line get through
    const bool sharp = rel >= S.v[F::tgate] - 0.001f;
    {
        const juce::SpinLock::ScopedLockType lock (readout.lock);
        if (readout.numSharps == (int) readout.sharps.size())
        {
            std::move (readout.sharps.begin() + 1, readout.sharps.end(), readout.sharps.begin());
            --readout.numSharps;
        }
        readout.sharps[(size_t) readout.numSharps++] = { T, rel, sharp };
    }
    static const int sourceOf[] = { sKick, sSnare, sHat };
    if (kind != VisListener::tone) hitSignal (sourceOf[kind], strength, S);
    if (big) hitSignal (sBig, strength, S);
    if (sharp && (T - lastTransient > 0.05 || strength > signal[sTrans])) hitSignal (sTrans, strength, S);
}

void Renderer::analyse (float dt, const Settings& S, VisListener& ear, double latency, Readout& readout)
{
    {
        VisListener::Hit hits[64];
        const int n = ear.readHits (hits, 64);
        hitQueue.insert (hitQueue.end(), hits, hits + n);
        VisListener::Bands bands[256];
        const int nb = ear.readBands (bands, 256);
        bandQueue.insert (bandQueue.end(), bands, bands + nb);
        if (bandQueue.size() > 2048) bandQueue.erase (bandQueue.begin(), bandQueue.end() - 1024);
    }
    // A sound is heard `latency` after the engine made it; the picture is drawn a touch ahead so it lands together
    const double now = ear.audioNow() - latency + 0.012 - (double) (S.v[F::nudge] - 0.5f) * 0.16;
    size_t used = 0;
    while (used < bandQueue.size() && bandQueue[used].time <= now) rawBands = bandQueue[used++];
    bandQueue.erase (bandQueue.begin(), bandQueue.begin() + (std::ptrdiff_t) used);
    if (used == 0 && ear.audioNow() - rawBands.time > 0.25) rawBands = { rawBands.time, 0.0f, 0.0f, 0.0f };   // no sound arriving

    const float gain = 0.4f + S.v[F::react] * 1.3f;
    const float raw[] = { rawBands.low, rawBands.mid, rawBands.high };
    for (int k = 0; k < 3; ++k)
    {
        peak[k] = juce::jmax (raw[k], peak[k] * (1.0f - dt * 0.12f), 0.12f);
        const float v = clamp01 (std::pow (raw[k] / peak[k], 1.6f) * gain);
        band[k] += (v - band[k]) * (1.0f - std::exp (-dt * (v > band[k] ? 45.0f : 7.0f)));
    }
    level = (band[0] + band[1] + band[2]) / 3.0f;

    signal[sKick] *= std::exp (-dt * 5.5f);  signal[sBig] *= std::exp (-dt * 5.5f);
    signal[sSnare] *= std::exp (-dt * 7.0f); signal[sHat] *= std::exp (-dt * 12.0f);
    signal[sTrans] *= std::exp (-dt * 16.0f);
    for (size_t i = 0; i < hitQueue.size();)
    {
        if (hitQueue[i].time <= now)
        {
            const auto h = hitQueue[i];
            hitQueue.erase (hitQueue.begin() + (std::ptrdiff_t) i);
            fire (h.kind, h.rel, S, readout);
        }
        else ++i;
    }
    // A level only reaches the wiring while it is at or above its line (the meters and the picture's own
    // use of low, mid and high are not held back)
    static const int lines[] = { F::lgate, F::mgate, F::hgate };
    for (int k = 0; k < 3; ++k) signal[(size_t) (sLow + k)] = band[k] >= S.v[(size_t) lines[k]] ? band[k] : 0.0f;
    signal[sLevel] = level;
    if (T - levelTime > 0.033)
    {
        levelTime = T;
        const juce::SpinLock::ScopedLockType lock (readout.lock);
        if (readout.numLevels == (int) readout.levels.size())
        {
            std::move (readout.levels.begin() + 1, readout.levels.end(), readout.levels.begin());
            --readout.numLevels;
        }
        readout.levels[(size_t) readout.numLevels++] = { T, { band[0], band[1], band[2] } };
    }

    reaction.fill (0.0f);
    for (int s = 0; s < numSources; ++s)
        for (int d = 0; d < numDests; ++d)
        {
            const float amount = S.route[(size_t) s][(size_t) d];
            if (amount <= 0.0f) continue;
            const float v = signal[(size_t) s] * amount;
            reaction[(size_t) d] += v;
            if (s <= sTrans) continue;                                   // hits already acted when they landed
            if (d == dSpin) omega += S.v[(size_t) S.spinKey()] * v * dt * 5.0f;
            if (d == dBurst)
            {
                auto& e = edges[(size_t) s];
                if (v > 0.6f && ! e.high && T - e.time > 0.25) { e.high = true; e.time = T; burst (v, S); }
                else if (v < 0.45f) e.high = false;
            }
        }
    for (auto& r : reaction) r = juce::jmin (1.5f, r);
    while (! onsets.empty() && T - onsets.back().time > 6.0) onsets.pop_back();

    // The beat turns the whole picture; the far past of the turn lags behind and reads as a twist
    omega *= std::exp (-dt * 5.0f);
    const float idle = S.v[(size_t) S.idleKey()];
    spinRate = omega + idle * idle * 1.6f + idle * 0.08f;
    theta += spinRate * dt;
    fluidRate += (spinRate - fluidRate) * (1.0f - std::exp (-dt / 2.5f));
    twist = juce::jlimit (-7.0f, 7.0f, (twist - ((spinRate - fluidRate) + spinRate * 0.25f) * dt * 1.6f) * std::exp (-dt / 3.5f));
    ph += dt * (0.25f + S.v[F::speed] * 1.6f) * (0.7f + S.v[F::stir] * reaction[dStir] * 1.6f);

    for (int i = 0; i < numSources; ++i) readout.signal[(size_t) i].store (signal[(size_t) i]);
    for (int i = 0; i < numDests; ++i) readout.reaction[(size_t) i].store (reaction[(size_t) i]);
    for (int i = 0; i < 3; ++i) readout.band[(size_t) i].store (band[i]);
    readout.clock.store (T);
    readout.turn.store (theta);
    readout.flow.store (ph);
}

//==============================================================================

void Renderer::common (Program& p, const Settings& S, float aspect)
{
    glUseProgram (p.id);
    const auto c = S.palette (reaction[dHue] * 90.0f);
    glUniform1f (p.u ("t"), (float) std::fmod (T, 3600.0));
    glUniform1f (p.u ("asp"), aspect);
    glUniform1f (p.u ("glow"), 1.0f);
    glUniform1f (p.u ("flash"), reaction[dFlash] * S.v[F::flash] * 1.6f);
    glUniform1f (p.u ("low"), band[0]);
    glUniform1f (p.u ("mid"), band[1]);
    glUniform1f (p.u ("high"), band[2]);
    glUniform1f (p.u ("lvl"), level);
    glUniform1f (p.u ("ph"), ph);
    glUniform1f (p.u ("pz"), S.v[F::pulse] * reaction[dPulse] * 0.24f);
    for (int i = 0; i < 3; ++i)
    {
        static const char* names[] = { "c1", "c2", "c3" };
        glUniform3f (p.u (names[i]), c[(size_t) i].getFloatRed(), c[(size_t) i].getFloatGreen(), c[(size_t) i].getFloatBlue());
    }
}

// Trails: the last frames linger and drift outward. Echo stamps a ghost about eight times a second instead.
void Renderer::trailOut (int W, int H, float dt, float aspect, float amount, bool echo, float psy, float zoom, const Out& o)
{
    auto& from = trails[trailIndex];
    auto& to = trails[1 - trailIndex];
    float cap = 1.0f;
    if (echo) { echoTime += dt; if (echoTime >= 0.12f) echoTime = 0.0f; else cap = 0.0f; }
    glUseProgram (trail.id);
    glBindFramebuffer (GL_FRAMEBUFFER, to.fbo);
    glViewport (0, 0, W, H);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, fresh.tex); glUniform1i (trail.u ("cur"), 0);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, from.tex);  glUniform1i (trail.u ("trail"), 1);
    glUniform1f (trail.u ("tdec"), amount < 0.02f ? 0.0f : std::pow (0.82f + 0.172f * amount, dt * 60.0f));
    glUniform1f (trail.u ("tzoom"), 1.0f - (0.001f + 0.0035f * amount) * zoom * (1.0f + reaction[dPulse]));
    glUniform1f (trail.u ("psy"), psy);
    glUniform1f (trail.u ("cap"), cap);
    glUniform1f (trail.u ("asp"), aspect);
    glUniform1f (trail.u ("trot"), spinRate * dt * 0.7f);
    glDrawArrays (GL_TRIANGLES, 0, 3);
    trailIndex = 1 - trailIndex;

    glUseProgram (out.id);
    glBindFramebuffer (GL_FRAMEBUFFER, o.framebuffer);
    glViewport (o.x, o.y, o.w, o.h);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, fresh.tex); glUniform1i (out.u ("cur"), 1);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, to.tex);    glUniform1i (out.u ("tex"), 0);
    glUniform1f (out.u ("gain"), o.gain);
    glDrawArrays (GL_TRIANGLES, 0, 3);
}

void Renderer::frame (const Settings& S, VisListener& ear, double latency, const Out& o, juce::Point<float> drag, Readout& readout, const std::shared_ptr<const Picture>& image)
{
    if (! ok || o.w < 8 || o.h < 8) return;
    const double wall = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) juce::jlimit (0.001, 0.05, wall - lastTime);
    lastTime = wall;
    T += dt;
    if (S.style != lastStyle) { lastStyle = S.style; seed = true; onsets.clear(); splats.clear(); }
    usePicture (image);
    analyse (dt, S, ear, latency, readout);

    GLint oldVao = 0;
    glGetIntegerv (GL_VERTEX_ARRAY_BINDING, &oldVao);
    glBindVertexArray (vao);
    glDisable (GL_BLEND);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_SCISSOR_TEST);
    glDisable (GL_CULL_FACE);

    // The picture is worked out at a moderate size and stretched: soft shapes lose nothing by it.
    // Liquid has crisp rims, so it is worked out a little larger than the window and brought down, which
    // smooths them (twice the size cost frames on a 100 Hz screen for little more smoothness).
    const float shrink = S.style == liquid ? juce::jmin (1.15f, 1500.0f / (float) o.h, 2800.0f / (float) o.w)
                                           : juce::jmin (1.0f, 900.0f / (float) o.h);
    const int W = juce::jmax (8, juce::roundToInt ((float) o.w * shrink)), H = juce::jmax (8, juce::roundToInt ((float) o.h * shrink));
    const float aspect = (float) W / (float) H;
    const bool dragging = drag.x >= 0.0f;
    const float psy = S.v[(size_t) S.psyKey()], trailAmount = S.v[(size_t) S.trailsKey()];
    const bool echo = S.w[(size_t) S.trailModeKey()] == 1;
    auto rnd = [this] { return random.nextFloat(); };

    if (S.style == ink)
    {
        const int m = S.w[C::mirror];
        ensureSim (m == 1 ? aspect / 2.0f : aspect);
        const float sw = (float) sim[0].w / (float) sim[0].h;
        if (seed)
        {
            seed = false;
            for (int i = 0; i < 9; ++i)
                splats.push_back ({ rnd() * sw, rnd(), 0.08f + rnd() * 0.12f, 0.7f, { i % 3 == 0 ? 1.0f : 0.0f, i % 3 == 1 ? 1.0f : 0.0f, i % 3 == 2 ? 1.0f : 0.0f } });
        }
        // Three drifting droppers feed ink with the low, middle and high sound
        const float d = S.v[F::drops] * dt * 2.2f, chaos = S.v[F::chaos];
        const float t = (float) std::fmod (T, 3600.0);
        for (int i = 0; i < 3; ++i)
        {
            const float b = band[i], wv = 0.13f + (float) i * 0.07f;
            const float x = sw * (0.5f + 0.42f * std::cos (t * wv + (float) i * 2.1f + chaos * 1.5f * std::sin (t * 1.1f + (float) i)));
            const float y = 0.5f + 0.42f * std::sin (t * wv * 1.3f + (float) i + chaos * 1.5f * std::cos (t * 0.9f + (float) i * 2.0f));
            if (b > 0.15f) splats.push_back ({ x, y, 0.04f + 0.07f * b, d * b * (i == 0 ? 2.2f : 1.5f), { i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f } });
        }
        if (dragging)
        {
            // The pointer is folded the way the picture is, so the ink lands under it
            float rx = (drag.x - 0.5f) * aspect, ry = drag.y - 0.5f;
            if (m > 0) rx = std::abs (rx);
            if (m == 2) ry = std::abs (ry);
            if (m > 2)
            {
                const float seg = juce::MathConstants<float>::twoPi / (m == 3 ? 3.0f : 4.0f), len = std::hypot (rx, ry);
                float an = std::fmod (std::atan2 (rx, ry) + seg * 0.5f, seg);
                if (an < 0.0f) an += seg;
                an = std::abs (an - seg * 0.5f);
                rx = len * std::sin (an); ry = len * std::cos (an);
            }
            splats.push_back ({ m > 0 ? rx * 2.0f / aspect * sw : drag.x * sw, m >= 2 ? ry * 2.0f : drag.y, 0.05f, dt * 6.0f, { 0.0f, 1.0f, 0.0f } });
        }

        float sp[32] = {}, sc[24] = {};
        const int use = juce::jmin (8, (int) splats.size());
        for (int i = 0; i < use; ++i)
        {
            const auto& s = splats[(size_t) i];
            sp[i * 4] = s.x; sp[i * 4 + 1] = s.y; sp[i * 4 + 2] = s.r; sp[i * 4 + 3] = s.amount;
            sc[i * 3] = s.colour[0]; sc[i * 3 + 1] = s.colour[1]; sc[i * 3 + 2] = s.colour[2];
        }
        splats.erase (splats.begin(), splats.begin() + use);
        if (splats.size() > 64) splats.resize (64);

        auto& p = inkStep;
        common (p, S, aspect);
        glUniform1f (p.u ("asp"), sw);
        glUniform1f (p.u ("dt"), dt);
        glUniform1f (p.u ("swirl"), (0.3f + S.v[F::swirl] * 2.4f) * (0.55f + S.v[F::stir] * reaction[dStir] * 1.8f));
        glUniform1f (p.u ("flow"), (0.4f + band[1] * 1.4f) * juce::jmin (1.0f, S.v[F::vort] * 3.0f));
        const int nv = juce::roundToInt (S.v[F::vort] * 6.0f);
        float vx[18] = {};
        for (int i = 0; i < nv; ++i)
        {
            const float fi = (float) i;
            vx[i * 3]     = sw * (0.5f + 0.4f * std::sin (t * (0.07f + fi * 0.023f) + fi * 1.7f + chaos * 0.9f * std::sin (t * 1.3f + fi)));
            vx[i * 3 + 1] = 0.5f + 0.4f * std::cos (t * (0.05f + fi * 0.031f) + fi * 2.9f + chaos * 0.9f * std::cos (t * 1.1f + fi * 3.0f));
            vx[i * 3 + 2] = (i % 2 ? 1.0f : -1.0f) * (0.5f + chaos * 0.9f) * (1.0f + band[1] * 0.6f + reaction[dStir] * chaos * 1.6f);
        }
        glUniform1f (p.u ("swl"), 0.0f);
        glUniform1f (p.u ("cf"), juce::jmin (spinRate * spinRate * 0.02f, 0.12f) / 0.11f);
        glUniform2f (p.u ("ctr"), m > 0 ? 0.0f : sw * 0.5f, m >= 2 ? 0.0f : 0.5f);
        glUniform1f (p.u ("comp"), S.w[C::tones] == 0 ? 0.0f : 0.07f);
        glUniform3fv (p.u ("vx"), 6, vx);
        glUniform1i (p.u ("nv"), nv);
        glUniform1f (p.u ("scale"), 0.6f + S.v[F::scale] * 2.4f);
        glUniform1f (p.u ("detail"), S.v[F::idetail]);
        glUniform1f (p.u ("chaos"), chaos * (1.0f + reaction[dStir] * 0.35f));
        glUniform1f (p.u ("fade"), std::pow (0.982f + S.v[F::fade] * 0.0175f, dt * 60.0f));
        glUniform4fv (p.u ("sp"), 8, sp);
        glUniform3fv (p.u ("sc"), 8, sc);
        glUniform1i (p.u ("ns"), use);
        auto& a = sim[simIndex];
        auto& b = sim[1 - simIndex];
        glBindFramebuffer (GL_FRAMEBUFFER, b.fbo);
        glViewport (0, 0, b.w, b.h);
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, a.tex); glUniform1i (p.u ("prev"), 0);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        simIndex = 1 - simIndex;

        ensureTrail (W, H);
        auto& s = inkShow;
        common (s, S, aspect);
        glBindFramebuffer (GL_FRAMEBUFFER, fresh.fbo);
        glViewport (0, 0, W, H);
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, b.tex); glUniform1i (s.u ("dye"), 0);
        glUniform2f (s.u ("px"), 1.4f / (float) b.w, 1.4f / (float) b.h);
        glUniform1i (s.u ("mir"), m);
        glUniform1f (s.u ("sharp"), S.w[C::tones] == 0 ? 1.4f : 7.0f);
        glUniform1f (s.u ("mono"), S.w[C::tones] == 0 ? S.v[F::tint] : 0.0f);
        glUniform1f (s.u ("rot"), theta);
        glUniform1i (s.u ("form"), S.w[C::form]);
        glUniform1i (s.u ("surf"), S.w[C::surface]);
        glUniform1f (s.u ("lsp"), S.v[F::depth] * (0.45f + reaction[dPulse] * 0.5f));
        glUniform1f (s.u ("segs"), (float) juce::roundToInt (3.0f + S.v[F::seg] * 9.0f));
        glUniform1f (s.u ("tw"), twist);
        glUniform1f (s.u ("psy"), psy);
        glUniform1f (s.u ("trip"), S.v[F::trip]);
        glUniform1f (s.u ("marble"), S.v[F::marble]);
        glUniform1f (s.u ("bleed"), S.v[F::bleed]);
        glUniform1f (s.u ("breathe"), S.v[F::breathe]);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        trailOut (W, H, dt, aspect, trailAmount, echo, psy, 1.0f, o);
    }
    else if (S.style == liquid)
    {
        auto& p = liquidProg;
        common (p, S, aspect);
        ensureTrail (W, H);
        glBindFramebuffer (GL_FRAMEBUFFER, fresh.fbo);
        glViewport (0, 0, W, H);
        if (liquidLast < 0.0f) liquidLast = ph;
        liquidPhase += (ph - liquidLast) * (0.3f + S.v[F::lflow] * 1.7f);
        liquidLast = ph;
        const int count = 4 + juce::roundToInt (S.v[F::lcount] * 8.0f), form = S.w[C::lform];
        const float size = 0.45f + S.v[F::lsize] * 0.9f, swell = S.v[F::lswell] * 2.0f, lp = liquidPhase, t = (float) std::fmod (T, 3600.0);
        float bl[48] = {};
        int n = 0;
        for (int i = 0; i < count; ++i, ++n)
        {
            const float fi = (float) i, a = fi * 2.399f;
            float bx, by;
            if (form == 1)        // lava lamp: columns that rise and sink
            {
                bx = aspect * 0.84f * (std::fmod (fi * 0.618f + 0.13f, 1.0f) * 2.0f - 1.0f) + 0.07f * std::sin (lp * 0.2f + fi);
                by = 0.86f * std::sin (lp * 0.075f * (1.0f + (float) (i % 4) * 0.22f) + fi * 1.9f);
            }
            else if (form == 2)   // orbit
            {
                const float r = 0.2f + 0.62f * std::fmod (fi * 0.37f + 0.1f, 1.0f), an = lp * 0.11f * (i % 2 ? 1.0f : -1.0f) * (1.25f - r) + a;
                bx = std::cos (an) * r * juce::jmin (aspect, 1.6f);
                by = std::sin (an) * r;
            }
            else
            {
                bx = aspect * 0.78f * std::sin (lp * 0.11f * (1.0f + (float) (i % 5) * 0.13f) + a * 1.3f) + 0.2f * std::sin (t * 0.21f + fi);
                by = 0.78f * std::cos (lp * 0.09f * (1.0f + (float) (i % 4) * 0.17f) + a * 2.1f) + 0.12f * std::cos (t * 0.17f + fi * 2.0f);
            }
            by += S.v[F::lheat] * 0.6f * std::sin (lp * 0.34f * (1.0f + (float) (i % 3) * 0.35f) + fi * 2.3f);    // heat: they rise and fall faster
            bl[n * 3] = bx; bl[n * 3 + 1] = by;
            bl[n * 3 + 2] = (0.17f + 0.11f * (float) ((i * 7) % 5) / 5.0f) * size * (1.0f + (band[0] * 0.3f + reaction[dPulse] * (i % 2 ? 0.45f : 0.25f)) * swell);
        }
        // Each burst spits a droplet out from the middle
        const int drops = juce::jmin ((int) onsets.size(), juce::roundToInt (S.v[F::ldrops] * 6.0f));
        for (int i = 0; i < drops && n < 16; ++i)
        {
            const auto& on = onsets[(size_t) i];
            const float age = (float) (T - on.time);
            if (age > 2.6f) continue;
            const float d = on.phase * 7.3f;
            bl[n * 3] = std::cos (d) * age * 0.6f; bl[n * 3 + 1] = std::sin (d) * age * 0.6f;
            bl[n * 3 + 2] = 0.2f * size * on.strength * (0.5f + S.v[F::ldrops]) * juce::jmax (0.0f, 1.0f - age / 2.6f);
            ++n;
        }
        if (dragging && n < 16) { bl[n * 3] = (drag.x - 0.5f) * 2.0f * aspect; bl[n * 3 + 1] = (drag.y - 0.5f) * 2.0f; bl[n * 3 + 2] = 0.2f * size; ++n; }
        glUniform3fv (p.u ("bl"), 16, bl);
        glUniform1i (p.u ("nb"), n);
        glUniform1f (p.u ("rot"), theta);
        glUniform1f (p.u ("psy"), psy);
        glUniform1f (p.u ("stirv"), reaction[dStir] * S.v[F::stir]);
        glUniform1i (p.u ("surf"), S.w[C::lsurface]);
        glUniform1i (p.u ("form"), form);
        glUniform1f (p.u ("wob"), S.v[F::lwob]);
        glUniform1f (p.u ("lay"), S.v[F::ldepth]);
        glUniform1i (p.u ("mir"), S.w[C::lmirror]);
        glUniform1f (p.u ("TH"), 1.01f * std::pow (1.9f, (0.5f - S.v[F::lmerge]) * 2.0f));
        glUniform1f (p.u ("shim"), S.v[F::lshim]);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        trailOut (W, H, dt, aspect, trailAmount, echo, psy, 1.6f, o);
    }
    else if (S.style == sand)
    {
        static const float modes[12][2] = { { 1, 2 }, { 2, 3 }, { 1, 4 }, { 3, 4 }, { 2, 5 }, { 1, 6 }, { 3, 5 }, { 4, 5 }, { 2, 7 }, { 3, 7 }, { 5, 6 }, { 4, 7 } };
        auto& p = sandProg;
        common (p, S, aspect);
        ensureTrail (W, H);
        glBindFramebuffer (GL_FRAMEBUFFER, fresh.fbo);
        glViewport (0, 0, W, H);
        const float k = clamp01 ((float) (T - sandTime) / 0.45f), blend = k * k * (3.0f - 2.0f * k);
        glUniform4f (p.u ("md"), modes[sandA][0], modes[sandA][1], modes[sandB][0], modes[sandB][1]);
        glUniform1f (p.u ("bl"), blend);
        glUniform1f (p.u ("rot"), theta);
        glUniform1f (p.u ("psy"), psy);
        glUniform1f (p.u ("grain"), S.v[F::sgrain]);
        glUniform1f (p.u ("shake"), clamp01 ((reaction[dPulse] * 0.55f + reaction[dStir] * 0.12f) * S.v[F::sscatter] * 2.0f + S.v[F::sres] * band[2] * 0.9f));
        glUniform1f (p.u ("dnk"), 0.3f + S.v[F::sdens] * 1.4f);
        glUniform1f (p.u ("drift"), S.v[F::sdrift]);
        glUniform1i (p.u ("mir"), S.w[C::smirror]);
        glUniform1f (p.u ("sharp"), S.v[F::ssharp]);
        glUniform1i (p.u ("surf"), S.w[C::ssurface]);
        glUniform1i (p.u ("form"), S.w[C::sform]);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        trailOut (W, H, dt, aspect, trailAmount, echo, psy, 1.0f, o);
    }
    else if (S.style == kaleido)
    {
        auto& p = kalProg;
        auto& k = kal;
        common (p, S, aspect);
        ensureTrail (W, H);
        glBindFramebuffer (GL_FRAMEBUFFER, fresh.fbo);
        glViewport (0, 0, W, H);
        k.turnTo += reaction[dStir] * S.v[F::stir] * S.v[F::ktumble] * dt * 0.9f * k.dir;
        k.turn += (k.turnTo - k.turn) * (1.0f - std::exp (-dt * 3.5f));
        k.s.follow (dt, band, level);
        {
            const float flashNow = reaction[dFlash] * S.v[F::flash] * 1.6f;
            k.flash += (flashNow - k.flash) * (1.0f - std::exp (-dt * (flashNow > k.flash ? 1.8f : 0.7f)));
        }
        k.s.colours (dt, S.v[F::kwash]);
        const float kpsy = k.s.psy (dt, S.v[F::kpsy], S.v[F::kplfo], S.v[F::kprate], S.v[F::kpsway], S.w[C::kpsyon]);
        k.phase += dt * S.v[F::ktumble] * 2.8f * (0.4f + S.v[F::speed] * 1.4f) * (1.0f + k.s.sb[1] * 1.3f);
        glUniform1f (p.u ("rot"), theta);
        glUniform1f (p.u ("psy"), kpsy);
        glUniform1f (p.u ("segs"), (float) juce::roundToInt (3.0f + S.v[F::kseg] * 9.0f));
        glUniform1f (p.u ("size"), S.v[F::ksize]);
        glUniform1f (p.u ("jag"), S.v[F::kjag]);
        glUniform1f (p.u ("lead"), S.v[F::klead]);
        glUniform1f (p.u ("light"), S.v[F::klight]);
        glUniform1f (p.u ("spark"), S.v[F::kspark]);
        glUniform1f (p.u ("frost"), S.v[F::kfrost]);
        glUniform1f (p.u ("turn"), k.turn);
        glUniform1f (p.u ("tph"), k.phase);
        glUniform1i (p.u ("surf"), S.w[C::ksurface]);
        glUniform1i (p.u ("form"), S.w[C::kform]);
        glUniform1f (p.u ("breathe"), S.v[F::kbreathe]);
        glUniform1f (p.u ("swell"), S.v[F::kswell]);
        glUniform1f (p.u ("wash"), S.v[F::kwash]);
        glUniform1f (p.u ("ripple"), S.v[F::kripple]);
        glUniform1f (p.u ("sfl"), k.flash);
        glUniform3fv (p.u ("sb"), 1, k.s.sb);
        glUniform1f (p.u ("cstep"), k.s.swapEased);
        glUniform1f (p.u ("cflow"), k.s.flow);
        glUniform1i (p.u ("colm"), S.w[C::kcolour]);
        glUniform1i (p.u ("cut"), S.w[C::kcut]);
        glUniform1f (p.u ("twist"), S.v[F::ktwist]);
        glUniform1f (p.u ("bulge"), S.v[F::kbulge]);
        glUniform1f (p.u ("layer"), S.v[F::klayer]);
        glUniform1f (p.u ("pph"), k.s.psyPhase);
        float ages[6] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
        for (size_t i = 0; i < onsets.size() && i < 6; ++i) ages[i] = (float) (T - onsets[i].time);
        glUniform1fv (p.u ("ons"), 6, ages);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        readout.psyNow.store (kpsy);
        trailOut (W, H, dt, aspect, trailAmount, echo, std::abs (kpsy), 1.0f, o);
    }
    else if (S.style == lotus)
    {
        auto& p = lotusProg;
        auto& z = lot;
        common (p, S, aspect);
        ensureTrail (W, H);
        glBindFramebuffer (GL_FRAMEBUFFER, fresh.fbo);
        glViewport (0, 0, W, H);
        z.s.follow (dt, band, level);
        z.push *= std::exp (-dt * 1.6f);
        z.speed += (z.push - z.speed) * (1.0f - std::exp (-dt * 5.0f));
        {
            // How far in we are, counted in rings. Only the part of the depth that matters is kept (the rest
            // repeats), so it can run for hours
            const float N = (float) (4 + juce::roundToInt (S.v[F::zpetals] * 8.0f));
            const float du = dt * ((0.03f + S.v[F::zspeed] * 0.75f) * (0.4f + S.v[F::speed] * 1.4f) + z.speed + reaction[dStir] * S.v[F::stir] * 0.25f) * N / juce::MathConstants<float>::twoPi;
            z.depth += du;
            z.wind = std::fmod (z.wind + S.v[F::ztwist] * 0.6f * du * (1.0f + z.s.sb[1] * 0.6f), N);
            while (z.depth > 1.0f)
            {
                z.depth -= 1.0f;
                z.ring = std::fmod (z.ring + 2.0f, 128.0f);
                z.colour = std::fmod (std::fmod (z.colour - 0.46f, 3.0f) + 3.0f, 3.0f);
            }
        }
        z.s.colours (dt, S.v[F::zwash]);
        const float zpsy = z.s.psy (dt, S.v[F::zpsy], S.v[F::zplfo], S.v[F::zprate], S.v[F::zpsway], S.w[C::zpsyon]);
        glUniform1f (p.u ("rot"), theta);
        glUniform1f (p.u ("psy"), zpsy);
        glUniform1f (p.u ("pph"), z.s.psyPhase);
        glUniform1f (p.u ("zf"), z.depth);
        glUniform1f (p.u ("zr"), z.wind);
        glUniform1f (p.u ("zw"), z.ring);
        glUniform1f (p.u ("zc"), z.colour);
        glUniform1f (p.u ("fa"), S.v[F::zpetals]);
        glUniform1f (p.u ("fb"), S.v[F::ztwist]);
        glUniform1f (p.u ("fc"), S.v[F::zpoint]);
        glUniform1f (p.u ("fo"), S.v[F::zopen]);
        glUniform1f (p.u ("lead"), S.v[F::zlead]);
        glUniform1f (p.u ("frost"), S.v[F::zfrost]);
        glUniform1f (p.u ("light"), S.v[F::zlight]);
        glUniform1f (p.u ("wash"), S.v[F::zwash]);
        glUniform1f (p.u ("cstep"), z.s.swapEased);
        glUniform1f (p.u ("cflow"), z.s.flow);
        glUniform3fv (p.u ("sb"), 1, z.s.sb);
        glUniform1i (p.u ("surf"), S.w[C::zsurface]);
        glUniform1i (p.u ("form"), S.w[C::zform]);
        glUniform1i (p.u ("colm"), S.w[C::zcolour]);
        float ages[6] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
        for (size_t i = 0; i < onsets.size() && i < 6; ++i) ages[i] = (float) (T - onsets[i].time);
        glUniform1fv (p.u ("ons"), 6, ages);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        readout.psyNow.store (zpsy);
        trailOut (W, H, dt, aspect, trailAmount, echo, std::abs (zpsy), 1.0f, o);
    }
    else if (S.style == pictureStyle && picture != nullptr)
    {
        // Picture: the shape of a dropped picture works on smoke. The smoke is the same kind of flowing dye as
        // ink, kept close to the shape; the shape is lit as a rounded 3D form.
        auto& k = pic;
        ensureSim (aspect);
        if (seed) { seed = false; clearSim(); }
        ensureTrail (W, H);
        const float sw = (float) sim[0].w / (float) sim[0].h;
        const int m = S.w[C::pmirror], act = S.w[C::pform];
        k.s.follow (dt, band, level);
        k.puff *= std::exp (-dt * 2.4f);            // the short push each hit gives
        {
            // The beat: a spring the lows lean on and every hit kicks
            const float w = 13.0f, z = 0.32f, target = k.s.sb[0] * S.v[F::pfeed] * 0.9f;
            k.beatSpeed += (-w * w * (k.beat - target) - 2.0f * z * w * k.beatSpeed) * dt;
            k.beat += k.beatSpeed * dt;
        }
        const float ppsy = k.s.psy (dt, S.v[F::ppsy], S.v[F::pplfo], S.v[F::pprate], S.v[F::ppsway], S.w[C::ppsyon]);
        // The picture stands upright: it only turns if spin is given to it, and comes back level when spin is taken away
        if (T - k.lastTime > 0.3) k.lastTheta = theta;
        k.lastTime = T;
        k.angle += theta - k.lastTheta;
        k.lastTheta = theta;
        if (S.v[F::pspin] < 0.005f && S.v[F::pidle] < 0.005f) k.angle = std::atan2 (std::sin (k.angle), std::cos (k.angle)) * std::exp (-dt * 2.0f);
        // With mirrors on, Offset slides the picture off the fold toward the middle of its slice, so the mirrors show whole copies of it
        float cx = 0.0f, cy = 0.0f;
        {
            const float off = S.v[F::poff];
            if (m == 1) cx = off * sw * 0.5f;
            else if (m == 2) { cx = off * sw * 0.5f; cy = off * 0.5f; }
            else if (m > 2)
            {
                const float a = juce::MathConstants<float>::twoPi / (m == 3 ? 3.0f : 4.0f) / 4.0f, r = off * 0.9f;
                cx = std::sin (a) * r; cy = -0.5f + std::cos (a) * r;
            }
        }
        // Dragging on the picture pushes the smoke along with the pointer (found through the same spin and mirror fold as the screen)
        float dp[4] = {};
        if (dragging)
        {
            float dx = (drag.x - 0.5f) * aspect, dy = drag.y - 0.5f;
            const float ca = std::cos (k.angle), sa = std::sin (k.angle);
            const float rx = ca * dx + sa * dy, ry = -sa * dx + ca * dy;
            dx = rx; dy = ry;
            if (m == 1) dx = std::abs (dx);
            else if (m == 2) { dx = std::abs (dx); dy = std::abs (dy); }
            else if (m > 2)
            {
                const float seg = juce::MathConstants<float>::twoPi / (m == 3 ? 3.0f : 4.0f), len = std::hypot (dx, dy);
                float an = std::fmod (std::atan2 (dx, dy) + seg * 0.5f, seg);
                if (an < 0.0f) an += seg;
                an = std::abs (an - seg * 0.5f);
                dx = len * std::sin (an); dy = len * std::cos (an);
            }
            const float qx = dx + sw * 0.5f, qy = dy + (m > 2 ? 0.0f : 0.5f);
            if (k.dragging)
            {
                dp[0] = qx; dp[1] = qy;
                dp[2] = juce::jlimit (-45.0f, 45.0f, (qx - k.dragX) / dt * 9.0f);
                dp[3] = juce::jlimit (-45.0f, 45.0f, (qy - k.dragY) / dt * 9.0f);
            }
            k.dragging = true; k.dragX = qx; k.dragY = qy;
        }
        else k.dragging = false;
        float ages[6] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
        for (size_t i = 0; i < onsets.size() && i < 6; ++i) ages[i] = (float) (T - onsets[i].time);
        auto shared = [&] (Program& p)
        {
            glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, picTex);     glUniform1i (p.u ("pic"), 1);
            glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, picDistTex); glUniform1i (p.u ("pic2"), 2);
            glActiveTexture (GL_TEXTURE0);
            glUniform1f (p.u ("psz"), 0.3f + 0.62f * S.v[F::psize]);
            glUniform1f (p.u ("pasp"), picture->aspect);
            glUniform1f (p.u ("plain"), picture->plain ? 1.0f : 0.0f);
            glUniform1f (p.u ("lifted"), picture->lifted ? 1.0f : 0.0f);
            glUniform1f (p.u ("amt"), S.v[F::pamt]);
            glUniform1i (p.u ("src"), S.w[C::psrc]);
            glUniform1i (p.u ("act"), act);
            glUniform2f (p.u ("pctr"), cx, cy);
            glUniform1f (p.u ("reach"), 0.08f + S.v[F::preach] * 0.9f);
            glUniform1fv (p.u ("ons"), 6, ages);
        };
        auto& p = picStep;
        common (p, S, aspect);
        glUniform1f (p.u ("asp"), sw);
        shared (p);
        glUniform1f (p.u ("dt"), dt);
        glUniform1f (p.u ("fade"), std::pow (0.5f, dt / (0.4f * std::pow (20.0f, S.v[F::phang]))));
        glUniform1f (p.u ("swirl"), (0.5f + S.v[F::pswirl] * 4.0f) * (0.6f + k.s.sb[1] * 1.1f + reaction[dStir] * S.v[F::stir] * 1.2f));
        glUniform1f (p.u ("wisp"), S.v[F::pwisp] * 3.6f * (0.5f + k.s.sb[2] * 1.6f));
        glUniform1f (p.u ("rise"), (0.4f + S.v[F::prise] * 4.6f) * (0.75f + k.s.sb[0] * (0.3f + S.v[F::pfeed] * 0.7f)));
        glUniform1f (p.u ("wind"), S.v[F::pwind] * 2.2f);
        glUniform1f (p.u ("puff"), k.puff * S.v[F::ppuff] * 4.0f);
        glUniform1f (p.u ("emit"), (S.v[F::psmoke] * 1.6f + S.v[F::pfeed] * k.s.sb[0] * 3.2f + k.puff * S.v[F::ppuff] * 1.1f) * 0.45f);
        glUniform1f (p.u ("cph"), (float) std::fmod (T, 3600.0) * 0.03f);
        glUniform4fv (p.u ("dp"), 1, dp);
        glUniform1i (p.u ("dir"), S.w[C::pdir]);
        glUniform1i (p.u ("colm"), S.w[C::pcolour]);
        // cloud: how thick the puff of cloud is (beats). heal: how fast the puff settles back (beats) or the picture re-forms (melts)
        glUniform1f (p.u ("bv"), juce::jlimit (-3.5f, 3.5f, k.beatSpeed * 0.2f));
        glUniform1f (p.u ("cloud"), 0.3f + S.v[F::psmoke]);
        glUniform1f (p.u ("heal"), act == 3 ? 2.0f + S.v[F::psmoke] * 12.0f : 1.5f / (0.8f + S.v[F::phang] * 5.0f));
        glUniform1f (p.u ("blow"), clamp01 (0.05f + k.s.sb[0] * S.v[F::pfeed] * 0.55f + k.puff * S.v[F::ppuff] * 0.5f));
        glUniform1f (p.u ("ringk"), S.v[F::ppuff] * 3.0f);
        glUniform3fv (p.u ("k1"), 1, picture->palette[0].data());
        glUniform3fv (p.u ("k2"), 1, picture->palette[1].data());
        glUniform3fv (p.u ("k3"), 1, picture->palette[2].data());
        auto& a = sim[simIndex];
        auto& b = sim[1 - simIndex];
        glBindFramebuffer (GL_FRAMEBUFFER, b.fbo);
        glViewport (0, 0, b.w, b.h);
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, a.tex); glUniform1i (p.u ("prev"), 0);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        simIndex = 1 - simIndex;

        auto& s = picShow;
        common (s, S, aspect);
        shared (s);
        glBindFramebuffer (GL_FRAMEBUFFER, fresh.fbo);
        glViewport (0, 0, W, H);
        glBindTexture (GL_TEXTURE_2D, b.tex); glUniform1i (s.u ("dye"), 0);
        glUniform2f (s.u ("px"), 1.4f / (float) b.w, 1.4f / (float) b.h);
        glUniform1f (s.u ("rot"), k.angle);
        glUniform1f (s.u ("psy"), ppsy);
        glUniform1f (s.u ("pph"), k.s.psyPhase);
        glUniform1f (s.u ("vis"), S.v[F::pvis]);
        glUniform1f (s.u ("haze"), S.v[F::phaze]);
        glUniform1f (s.u ("ash"), S.v[F::pash]);
        glUniform1i (s.u ("surf"), S.w[C::psurface]);
        glUniform1i (s.u ("mir"), m);
        glUniform1f (s.u ("bx"), k.beat);
        glUniform1f (s.u ("dent"), S.v[F::pdent] * 1.1f);
        glUniform1f (s.u ("ringk"), S.v[F::ppuff] * 0.35f);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        readout.psyNow.store (ppsy);
        trailOut (W, H, dt, aspect, trailAmount, echo, std::abs (ppsy), 1.0f, o);
        glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, 0);
    }
    else if (S.style == bloom)
    {
        // Bloom: growth that forms, glows, dims and dissolves. Big hits plant new growth.
        ensureSim (aspect);
        const float sw = (float) sim[0].w / (float) sim[0].h;
        const int kind = juce::jlimit (0, 2, S.w[C::btype]);
        auto seedSize = [&] { return (kind == 1 ? 0.038f : 0.02f) + rnd() * 0.016f; };
        bool clear = false;
        if (seed)
        {
            seed = false; clear = true; bloomTime = T;
            for (int i = 0; i < 4; ++i) splats.push_back ({ (0.15f + rnd() * 0.7f) * sw, 0.15f + rnd() * 0.7f, seedSize(), 1.0f, {} });
        }
        if (rnd() < band[2] * 0.03f * dt * 60.0f) splats.push_back ({ rnd() * sw, rnd(), 0.01f, 1.0f, {} });
        const double idleGap = 2.8 - (double) S.v[F::bseeds] * 2.0;
        if ((reaction[dBurst] > 0.4f && T - bloomTime > 0.22) || T - bloomTime > idleGap)
        {
            const int n = T - bloomTime > idleGap ? 1 : 1 + juce::roundToInt (reaction[dBurst] * S.v[F::bseeds] * 4.0f);
            bloomTime = T;
            for (int i = 0; i < n; ++i) splats.push_back ({ (0.06f + rnd() * 0.88f) * sw, 0.06f + rnd() * 0.88f, seedSize(), 1.0f, {} });
        }
        if (dragging) splats.push_back ({ drag.x * sw, drag.y, 0.018f, 1.0f, {} });

        static const float types[3][2] = { { 0.055f, 0.062f }, { 0.03f, 0.0625f }, { 0.029f, 0.057f } };
        bloomSteps += (8.0f + (float) juce::roundToInt (S.v[F::bspeed] * 20.0f)) * 60.0f * dt;
        const int steps = juce::jlimit (1, 40, (int) bloomSteps);
        bloomSteps = juce::jmin (bloomSteps - (float) steps, 40.0f);
        auto& p = bloomStep;
        common (p, S, aspect);
        glUniform1f (p.u ("asp"), sw);
        glUniform2f (p.u ("px"), 1.0f / (float) sim[0].w, 1.0f / (float) sim[0].h);
        glUniform1f (p.u ("fd"), types[kind][0] + (level - 0.4f) * 0.0025f);
        glUniform1f (p.u ("kl"), types[kind][1] - (S.v[F::bbranch] - 0.5f) * 0.005f);
        glViewport (0, 0, sim[0].w, sim[0].h);
        glActiveTexture (GL_TEXTURE0);
        glUniform1i (p.u ("prev"), 0);
        const float span = (300.0f + 3000.0f * S.v[F::bcover]) * (kind == 1 ? 1.8f : 1.0f);
        for (int it = 0; it < steps; ++it)
        {
            float sp[32] = {};
            const int use = it > 5 ? 0 : juce::jmin (8, (int) splats.size());
            for (int i = 0; i < use; ++i)
            {
                const auto& s = splats[(size_t) i];
                sp[i * 4] = s.x / sw; sp[i * 4 + 1] = s.y; sp[i * 4 + 2] = s.r; sp[i * 4 + 3] = s.amount;
            }
            splats.erase (splats.begin(), splats.begin() + use);
            glUniform4fv (p.u ("sp"), 8, sp);
            glUniform1i (p.u ("ns"), use);
            glUniform1f (p.u ("clr"), it == 0 && clear ? 1.0f : 0.0f);
            glUniform1f (p.u ("up"), it == 0 ? (float) steps / span : 0.0f);
            glUniform1f (p.u ("dn"), it == 0 ? (float) steps / 900.0f : 0.0f);
            auto& a = sim[simIndex];
            auto& b = sim[1 - simIndex];
            glBindFramebuffer (GL_FRAMEBUFFER, b.fbo);
            glBindTexture (GL_TEXTURE_2D, a.tex);
            glDrawArrays (GL_TRIANGLES, 0, 3);
            simIndex = 1 - simIndex;
        }
        if (splats.size() > 64) splats.resize (64);

        ensureTrail (W, H);
        auto& s = bloomShow;
        common (s, S, aspect);
        glBindFramebuffer (GL_FRAMEBUFFER, fresh.fbo);
        glViewport (0, 0, W, H);
        glBindTexture (GL_TEXTURE_2D, sim[simIndex].tex);
        glUniform1i (s.u ("dye"), 0);
        glUniform2f (s.u ("px"), 1.2f / (float) sim[0].w, 1.2f / (float) sim[0].h);
        glUniform1f (s.u ("rot"), theta);
        glUniform1f (s.u ("psy"), psy);
        glUniform1f (s.u ("fresh"), S.v[F::bfresh]);
        glUniform1i (s.u ("surf"), S.w[C::bsurface]);
        glUniform1i (s.u ("mir"), S.w[C::bmirror]);
        glUniform1f (s.u ("halo"), S.v[F::bhalo]);
        glUniform1f (s.u ("ringk"), S.v[F::brings]);
        float ages[6] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
        for (size_t i = 0; i < onsets.size() && i < 6; ++i) ages[i] = (float) (T - onsets[i].time);
        glUniform1fv (s.u ("ons"), 6, ages);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        trailOut (W, H, dt, aspect, trailAmount, echo, psy, 1.0f, o);
    }

    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, 0);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, 0);
    glUseProgram (0);
    glBindVertexArray ((GLuint) oldVao);
}
}
