// Made by tools/visualiser/extract.py from the mockup. Do not edit by hand.
// X (name, default, label, format): the faders.   Y (name, default): the word choices.
#define VIS_FADERS(X) \
    X (react, .55f, "Sensitivity", pct) \
    X (pulse, .4f, "Pulse", pct) \
    X (stir, .5f, "Stir", pct) \
    X (flash, .5f, "Flash", pct) \
    X (speed, .45f, "Speed", pct) \
    X (swirl, .55f, "Swirl", pct) \
    X (drops, .5f, "Ink", pct) \
    X (fade, .6f, "Hold", pct) \
    X (hue, .69f, "Hue", hue) \
    X (tint, .45f, "Tint", pct) \
    X (trails, .55f, "Trails", pct) \
    X (spin, .5f, "Spin", pct) \
    X (idle, 0.0f, "Idle spin", pct) \
    X (psy, 0.0f, "Psychedelic", pct) \
    X (trip, 0.0f, "Trip", pct) \
    X (seg, .34f, "Segments", seg) \
    X (depth, 0.0f, "Depth", pct) \
    X (scale, .4f, "Scale", pct) \
    X (idetail, .3f, "Detail", pct) \
    X (chaos, .3f, "Chaos", pct) \
    X (vort, .35f, "Vortices", vort) \
    X (sym, .4f, "Symmetry", sym) \
    X (detail, .45f, "Detail", pct) \
    X (zoom, .5f, "Zoom", pct) \
    X (spacing, .45f, "Spacing", pct) \
    X (twist, .35f, "Twist", pct) \
    X (stars, .5f, "Stars", pct) \
    X (thue, .52f, "Hue", hue) \
    X (ttint, .5f, "Tint", pct) \
    X (tpsy, 0.0f, "Psychedelic", pct) \
    X (ttrip, 0.0f, "Trip", pct) \
    X (ttrails, .45f, "Trails", pct) \
    X (tspin, .5f, "Spin", pct) \
    X (tidle, .12f, "Idle spin", pct) \
    X (tdepth, 0.0f, "Depth", pct) \
    X (bend, .3f, "Bend", pct) \
    X (lsize, .5f, "Size", pct) \
    X (lcount, .5f, "Blobs", cnt) \
    X (lwob, .5f, "Wobble", pct) \
    X (lflow, .45f, "Flow", pct) \
    X (ldrops, .5f, "Droplets", pct) \
    X (lswell, .5f, "Swell", pct) \
    X (ldepth, .35f, "Depth", pct) \
    X (lhue, .86f, "Hue", hue) \
    X (ltint, .45f, "Tint", pct) \
    X (lpsy, .2f, "Psychedelic", pct) \
    X (ltrails, .5f, "Trails", pct) \
    X (lspin, .4f, "Spin", pct) \
    X (lidle, 0.0f, "Idle spin", pct) \
    X (sgrain, .5f, "Grain", pct) \
    X (scomp, .6f, "Complexity", pct) \
    X (ssharp, .5f, "Sharpness", pct) \
    X (sscatter, .5f, "Scatter", pct) \
    X (schange, .67f, "Change", pct) \
    X (shue, .1f, "Hue", hue) \
    X (stint, .45f, "Tint", pct) \
    X (spsy, 0.0f, "Psychedelic", pct) \
    X (strails, .35f, "Trails", pct) \
    X (sspin, .4f, "Spin", pct) \
    X (sidle, .05f, "Idle spin", pct) \
    X (bspeed, .5f, "Growth", pct) \
    X (bcover, .4f, "Cover", pct) \
    X (bseeds, .5f, "Seeds", pct) \
    X (bfresh, .8f, "Fade", pct) \
    X (bhue, .45f, "Hue", hue) \
    X (btint, .45f, "Tint", pct) \
    X (bpsy, 0.0f, "Psychedelic", pct) \
    X (btrails, .3f, "Trails", pct) \
    X (bspin, .3f, "Spin", pct) \
    X (bidle, 0.0f, "Idle spin", pct) \
    X (back, .55f, "Backdrop", pct) \
    X (gate, .5f, "Loudest lows", pct) \
    X (nudge, .5f, "Sync nudge", ms) \
    X (marble, 0.0f, "Marble", pct) \
    X (bleed, 0.0f, "Bleed", pct) \
    X (breathe, 0.0f, "Breathe", pct) \
    X (lheat, 0.0f, "Heat", pct) \
    X (lmerge, .5f, "Merge", pct) \
    X (lshim, 0.0f, "Shimmer", pct) \
    X (sdens, .5f, "Density", pct) \
    X (sdrift, 0.0f, "Drift", pct) \
    X (sres, 0.0f, "Resonance", pct) \
    X (bbranch, .5f, "Branching", pct) \
    X (brings, 0.0f, "Rings", pct) \
    X (bhalo, 0.0f, "Halo", pct) \
    X (tgate, 0.0f, "Transients", pct) \
    X (lgate, 0.0f, "Low", pct) \
    X (mgate, 0.0f, "Mid", pct) \
    X (hgate, 0.0f, "High", pct) \
    X (kseg, .34f, "Segments", seg) \
    X (ksize, .5f, "Shards", pct) \
    X (kjag, .7f, "Shatter", pct) \
    X (klead, .4f, "Lead", pct) \
    X (ktumble, .45f, "Tumble", pct) \
    X (klight, .6f, "Light", pct) \
    X (kfrost, .45f, "Frost", pct) \
    X (kspark, .2f, "Sparkle", pct) \
    X (khue, .58f, "Hue", hue) \
    X (ktint, .45f, "Tint", pct) \
    X (kpsy, 0.0f, "Psychedelic", pct) \
    X (ktrails, 0.0f, "Trails", pct) \
    X (kspin, .35f, "Spin", pct) \
    X (kidle, .06f, "Idle spin", pct) \
    X (kbreathe, .15f, "Breathe", pct) \
    X (kswell, .4f, "Swell", pct) \
    X (kwash, .45f, "Colour wash", pct) \
    X (kripple, .35f, "Ripple", pct) \
    X (kpsway, .25f, "Sway", pct) \
    X (kplfo, 0.0f, "LFO", pct) \
    X (kprate, .5f, "LFO rate", lfo) \
    X (ktwist, 0.0f, "Twist", pct) \
    X (kbulge, 0.0f, "Bulge", pct) \
    X (klayer, 0.0f, "Layers", pct) \
    X (zspeed, .4f, "Zoom", pct) \
    X (zlunge, .45f, "Lunge", pct) \
    X (zpetals, .5f, "Petals", cnt) \
    X (ztwist, .3f, "Twist", pct) \
    X (zpoint, .5f, "Point", pct) \
    X (zopen, .5f, "Open", pct) \
    X (zlight, .6f, "Light", pct) \
    X (zfrost, .45f, "Frost", pct) \
    X (zlead, .4f, "Lead", pct) \
    X (zhue, .72f, "Hue", hue) \
    X (ztint, .5f, "Tint", pct) \
    X (zpsy, 0.0f, "Psychedelic", pct) \
    X (zplfo, 0.0f, "LFO", pct) \
    X (zprate, .5f, "LFO rate", lfo) \
    X (zpsway, .25f, "Sway", pct) \
    X (zwash, .45f, "Colour wash", pct) \
    X (ztrails, 0.0f, "Trails", pct) \
    X (zspin, .2f, "Spin", pct) \
    X (zidle, .04f, "Idle spin", pct) \
    X (psize, .5f, "Size", pct) \
    X (pamt, .5f, "Amount", pct) \
    X (pvis, .85f, "Picture", pct) \
    X (phaze, .3f, "Haze", pct) \
    X (psmoke, .45f, "Smoke", pct) \
    X (pfeed, .6f, "Feed", pct) \
    X (ppuff, .5f, "Puff", pct) \
    X (prise, .45f, "Rise", pct) \
    X (pswirl, .45f, "Swirl", pct) \
    X (pwisp, .4f, "Wisps", pct) \
    X (pwind, .2f, "Wind", pct) \
    X (phang, .6f, "Hang", pct) \
    X (phue, .72f, "Hue", hue) \
    X (ptint, .5f, "Tint", pct) \
    X (ppsy, 0.0f, "Psychedelic", pct) \
    X (pplfo, 0.0f, "LFO", pct) \
    X (pprate, .5f, "LFO rate", lfo) \
    X (ppsway, 0.0f, "Sway", pct) \
    X (pash, .2f, "Ash", pct) \
    X (ptrails, 0.0f, "Trails", pct) \
    X (pspin, 0.0f, "Spin", pct) \
    X (pidle, 0.0f, "Idle spin", pct) \
    X (poff, .5f, "Offset", pct) \
    X (pdent, .5f, "Depth", pct) \
    X (preach, .25f, "Reach", pct) \

#define VIS_CHOICES(Y) \
    Y (surface, 0) \
    Y (form, 0) \
    Y (mirror, 2) \
    Y (tones, 0) \
    Y (trails, 0) \
    Y (shape, 0) \
    Y (sides, 3) \
    Y (tform, 0) \
    Y (ttones, 0) \
    Y (ttrails, 0) \
    Y (tsurface, 0) \
    Y (btype, 0) \
    Y (ltones, 2) \
    Y (ltrails, 0) \
    Y (lsurface, 0) \
    Y (lform, 0) \
    Y (stones, 0) \
    Y (strails, 0) \
    Y (ssurface, 0) \
    Y (sform, 0) \
    Y (btones, 0) \
    Y (btrails, 0) \
    Y (bsurface, 0) \
    Y (bmirror, 0) \
    Y (lmirror, 0) \
    Y (smirror, 0) \
    Y (ktones, 3) \
    Y (ktrails, 0) \
    Y (ksurface, 0) \
    Y (kform, 0) \
    Y (kcolour, 0) \
    Y (kpsyon, 0) \
    Y (kcut, 0) \
    Y (ztones, 3) \
    Y (ztrails, 0) \
    Y (zsurface, 0) \
    Y (zform, 0) \
    Y (zcolour, 0) \
    Y (zpsyon, 0) \
    Y (ptones, 3) \
    Y (ptrails, 0) \
    Y (psurface, 0) \
    Y (pform, 0) \
    Y (pcolour, 0) \
    Y (ppsyon, 0) \
    Y (pdir, 0) \
    Y (psrc, 0) \
    Y (pmirror, 0) \

