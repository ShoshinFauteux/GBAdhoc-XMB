#!/usr/bin/env python3
"""Bake GBAdhoc XMB's fixed art into gen/xmb_art.h (RGBA8888, power-of-two padded).

    python tools/bake_art.py          (from psp/; needs headless Chrome: CHROME=path)

- The GBAdhoc logo (desktop/ui/assets/logo-dark@2x.png, the project's own) at the
  sizes the design draws it: 66x24 (top bar), 176x64 (boot), 104x38 (the
  GBAdhoc item in the XMB preview).  Area-averaged in premultiplied alpha.
- The XMB category glyphs from tools/glyphs.js (GLYPHS: set/photo/music/video/game/net)
  at 22 and 34 px.  They are SVG, so they are rasterised by headless Chrome
  (exact, anti-aliased) -- the art is the design's own vector drawing.
"""
import base64, io, json, os, re, socket, subprocess, sys, time, urllib.request
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REPO = os.path.abspath(os.path.join(ROOT, '..'))
OUT = os.path.join(ROOT, 'gen', 'xmb_art.h')
CHROME = os.environ.get('CHROME', 'chrome')


def premul_resize(im, size):
    im = im.convert('RGBA')
    r, g, b, a = im.split()
    from PIL import ImageChops
    pm = Image.merge('RGBA', [ImageChops.multiply(c, a) for c in (r, g, b)] + [a])
    pm = pm.resize(size, Image.BOX if size[0] < im.size[0] / 2 else Image.LANCZOS)
    px = pm.load()
    for y in range(pm.size[1]):
        for x in range(pm.size[0]):
            R, G, B, A = px[x, y]
            if A:
                px[x, y] = (min(255, R * 255 // A), min(255, G * 255 // A), min(255, B * 255 // A), A)
    return pm


def glyph_svgs(path):
    src = open(path, encoding='utf-8').read()
    m = re.search(r'const GLYPHS = \{(.*?)\n  \};', src, re.S)
    out = {}
    for k, v in re.findall(r"(\w+): '(<svg.*?</svg>)'", m.group(1)):
        out[k] = v
    return out


def chrome_raster(svgs, sizes):
    """{(name,size): Image} rendered by headless Chrome on a transparent page."""
    import websocket
    s = socket.socket(); s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]; s.close()
    import tempfile; prof = tempfile.mkdtemp(prefix='bake-art-')
    ch = subprocess.Popen([CHROME, '--headless=new', f'--remote-debugging-port={port}',
                           f'--user-data-dir={os.path.abspath(prof)}', '--no-first-run', 'about:blank'],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                tabs = json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/json')); break
            except Exception:
                time.sleep(0.2)
        ws = websocket.create_connection([t for t in tabs if t['type'] == 'page'][0]['webSocketDebuggerUrl'],
                                         suppress_origin=True)
        n = [0]

        def call(method, **p):
            n[0] += 1
            ws.send(json.dumps({'id': n[0], 'method': method, 'params': p}))
            while True:
                r = json.loads(ws.recv())
                if r.get('id') == n[0]:
                    return r.get('result', {})
        call('Emulation.setDefaultBackgroundColorOverride', color={'r': 0, 'g': 0, 'b': 0, 'a': 0})
        call('Emulation.setDeviceMetricsOverride', width=400, height=400, deviceScaleFactor=1, mobile=False)
        res = {}
        for name, svg in svgs.items():
            for sz in sizes:
                html = (f'<html><body style="margin:0;background:transparent">'
                        f'<div style="width:{sz}px;height:{sz}px">{svg.replace("<svg ", f"<svg width={sz} height={sz} ")}</div></body></html>')
                call('Page.navigate', url='data:text/html;base64,' + base64.b64encode(html.encode()).decode())
                time.sleep(0.25)
                img = call('Page.captureScreenshot', format='png', clip={'x': 0, 'y': 0, 'width': sz, 'height': sz, 'scale': 1})
                res[(name, sz)] = Image.open(io.BytesIO(base64.b64decode(img['data']))).convert('RGBA')
        return res
    finally:
        ch.terminate()


def pow2(n):
    p = 1
    while p < n:
        p *= 2
    return max(p, 4)


def emit(o, name, im):
    w, h = im.size
    tw, th = pow2(w), pow2(h)
    pad = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
    pad.paste(im, (0, 0))
    raw = pad.tobytes()
    o.write(f'/* {name}: {w}x{h} in {tw}x{th} */\n')
    o.write(f'static const unsigned int __attribute__((aligned(16))) xart_{name}[{tw * th}] = {{\n')
    words = [raw[i] | raw[i + 1] << 8 | raw[i + 2] << 16 | raw[i + 3] << 24 for i in range(0, len(raw), 4)]
    for i in range(0, len(words), 12):
        o.write('  ' + ','.join(f'0x{x:08x}' for x in words[i:i + 12]) + ',\n')
    o.write('};\n')
    return (name, w, h, tw, th)


def main():
    glyphs = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'glyphs.js')
    logo = Image.open(os.path.join(REPO, 'desktop', 'ui', 'assets', 'logo-dark@2x.png'))
    arts = [('logo_top', premul_resize(logo, (66, 24))),
            ('logo_boot', premul_resize(logo, (176, 64))),
            ('logo_item', premul_resize(logo, (104, 38)))]
    gl = chrome_raster(glyph_svgs(glyphs), (22, 34))
    for (name, sz), im in sorted(gl.items()):
        arts.append((f'cat_{name}_{sz}', im))
    o = io.StringIO()
    o.write('/* generated by tools/bake_art.py -- do not edit */\n#ifndef XMB_ART_H\n#define XMB_ART_H\n\n')
    o.write('typedef struct { const unsigned int *px; int w, h, tw, th; } xart;\n\n')
    meta = [emit(o, n, im) for n, im in arts]
    o.write('\nenum {\n' + ''.join(f'  XART_{m[0].upper()},\n' for m in meta) + '  XART_COUNT\n};\n')
    o.write('static const xart xart_table[XART_COUNT] = {\n' +
            ''.join(f'  {{xart_{m[0]},{m[1]},{m[2]},{m[3]},{m[4]}}},\n' for m in meta) + '};\n\n#endif\n')
    open(OUT, 'w').write(o.getvalue())
    print(len(meta), 'images ->', OUT)


if __name__ == '__main__':
    main()
