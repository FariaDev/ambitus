#!/usr/bin/env python3
"""Gera variantes autocontidas (fontes em base64) do protótipo para o render do T3.
Uso: python3 build.py <saída> tema=light acento=graphite tela=biblioteca capa=contorno"""
import base64, pathlib, re, sys
here = pathlib.Path(__file__).parent
html = (here / "index.html").read_text()
def inline(m):
    data = base64.b64encode((here / m.group(1)).read_bytes()).decode()
    return f'url("data:font/woff2;base64,{data}")'
html = re.sub(r'url\("(fonts/[^"]+)"\)', inline, html)
opts = dict(a.split("=", 1) for a in sys.argv[2:])
for key, attr in [("tema", "data-theme"), ("acento", "data-accent"), ("tela", "data-screen"), ("capa", "data-cover")]:
    if key in opts:
        html = re.sub(f'{attr}="[^"]*"', f'{attr}="{opts[key]}"', html, count=1)
pathlib.Path(sys.argv[1]).write_text(html)
