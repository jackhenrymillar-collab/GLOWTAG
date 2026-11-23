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
