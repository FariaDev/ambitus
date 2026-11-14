#!/usr/bin/env python3
"""Gera uma cópia autocontida da revisão (imagens e fontes em base64) para o render do T3.
Uso: python3 build.py <saída.html>"""
import base64, pathlib, re, sys
here = pathlib.Path(__file__).parent
html = (here / "index.html").read_text()
def data(path, mime):
    return f"data:{mime};base64," + base64.b64encode((here / path).read_bytes()).decode()
html = re.sub(r'url\("(\.\./r1/fonts/[^"]+)"\)', lambda m: f'url("{data(m.group(1), "font/woff2")}")', html)
html = re.sub(r'src="(img/[^"]+\.png)"', lambda m: f'src="{data(m.group(1), "image/png")}"', html)
pathlib.Path(sys.argv[1]).write_text(html)
