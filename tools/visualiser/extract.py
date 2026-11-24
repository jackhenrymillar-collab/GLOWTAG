#!/usr/bin/env python3
"""Turns the approved visualiser mockup (mockup.html) into what the app compiles in:
the picture shaders and the list of settings with their defaults.
Run from the repository root:  python3 tools/visualiser/extract.py"""
import re, pathlib
root = pathlib.Path(__file__).resolve().parents[2]
html = (root / 'tools/visualiser/mockup.html').read_text()
out = root / 'Source/UI'

names = ['VS', 'HEAD', 'INK_STEP', 'INK_SHOW', 'TRAIL', 'COPY', 'LIQ', 'SAND', 'BL_STEP', 'BL_SHOW']
src = {}
for n in names:
    m = re.search(r'const %s=(HEAD\+)?`(.*?)`;' % n, html, re.S)
    assert m, n
    src[n] = m.group(2)
head = src['HEAD'].replace('#version 300 es', '#version 330 core')
lines = ['// Made by tools/visualiser/extract.py from the mockup. Do not edit by hand.', 'namespace VisShaders', '{']
for n in names:
    if n == 'HEAD':
        continue
    body = src[n]
    if n == 'VS':
        body = body.replace('#version 300 es', '#version 330 core').replace('in vec2 p;', 'layout(location=0) in vec2 p;')
    else:
        body = head + body
    assert ')GLSL"' not in body
    lines.append('static const char* const %s = R"GLSL(%s)GLSL";' % (n, body))
lines.append('}')
(out / 'VisualiserShaders.inc').write_text('\n'.join(lines) + '\n')

p = re.search(r'const P=\{(.*?)\n\};', html, re.S).group(1)
params = re.findall(r"(\w+):\[([-.\d]+),'([^']+)','(\w+)'\]", p)
w = re.search(r"const S=\{.*?w:\{(.*?)\}", html, re.S).group(1)
words = re.findall(r'(\w+):(\d+)', w)
lines = ['// Made by tools/visualiser/extract.py from the mockup. Do not edit by hand.',
         '// X (name, default, label, format): the faders.   Y (name, default): the word choices.',
         '#define VIS_FADERS(X) \\']
lines += ['    X (%s, %sf, "%s", %s) \\' % (k, d if '.' in d else d + '.0', lab, fmt) for k, d, lab, fmt in params]
lines += ['', '#define VIS_CHOICES(Y) \\']
lines += ['    Y (%s, %s) \\' % (k, d) for k, d in words]
lines += ['']
(out / 'VisualiserParams.inc').write_text('\n'.join(lines) + '\n')
print(len(params), 'faders,', len(words), 'choices')

# The picture buttons (look and form of each mode): the mockup's line drawings, worked out by
# running its own drawing code, so the app shows exactly the same pictures.
import json, subprocess, tempfile
helpers = [re.search(r'^const %s=.*$' % n, html, re.M).group(0) for n in ('ring', 'hexP', 'dots', 'rrPts')]
icon = re.search(r'const ICON=\{.*?\n\};', html, re.S).group(0)
order = [[['smoke', 'oil', 'neon'], ['blot', 'petals', 'rays', 'tiles']],
         [['l_lamp', 'l_glass', 'l_neon'], ['l_drift', 'l_lava', 'l_orbit', 'l_mirror']],
         [['s_grains', 's_dust', 's_bulbs'], ['s_square', 's_round', 's_ripple']],
         [['b_glow', 'b_lines', 'b_relief'], ['b_coral', 'b_cells', 'b_maze']]]
js = '\n'.join(helpers) + '\n' + icon + '\nconst order=%s;\nconsole.log(JSON.stringify(order.map(m=>m.map(r=>r.map(n=>ICON[n](0,0))))));' % json.dumps(order)
with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as f:
    f.write(js)
icons = json.loads(subprocess.run(['node', f.name], capture_output=True, text=True, check=True).stdout)
lines = ['// Made by tools/visualiser/extract.py from the mockup. Do not edit by hand.',
         '// icons[mode][0 look, 1 form][choice]: lines around (0, 0), about 34 units across.',
         'static const std::vector<std::vector<juce::Point<float>>> icons[4][2][4] = {']
for mode in icons:
    lines.append('  {')
    for row in mode:
        lines.append('    {')
        for pic in row:
            lines.append('      { ' + ', '.join('{ ' + ', '.join('{ %.2ff, %.2ff }' % (x, y) for x, y in pl) + ' }' for pl in pic) + ' },')
        lines.append('    },')
    lines.append('  },')
lines.append('};')
(out / 'VisualiserIcons.inc').write_text('\n'.join(lines) + '\n')
print(sum(len(r) for m in icons for r in m), 'picture buttons')
