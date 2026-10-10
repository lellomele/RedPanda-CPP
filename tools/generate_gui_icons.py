"""Rebuild color variants from the repository's GPL New Look SVG artwork.
Copyright (C) 2021-2026 Roy Qu (base artwork).
SPDX-License-Identifier: GPL-3.0-or-later
"""
from pathlib import Path
import colorsys
import json
import re

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'RedPandaIDE/resources/iconsets'
# Keep stable identifiers so existing user preferences remain valid.
STYLES = {
    'fluent-soft': ('Fluent Color', 'Fluent Color', 0.005, 1.06, 1.02),
    'adwaita-symbolic': ('Adwaita Color', 'Adwaita Color', -0.02, 0.96, 0.94),
    'breeze-lines': ('Breeze Color', 'Breeze Color', 0.025, 0.92, 1.03),
    'cupertino-compact': ('Cupertino Color', 'Cupertino Color', -0.006, 0.76, 1.06),
}

def color_variant(match, style):
    value = match.group(0)[1:]
    if len(value) == 3:
        value = ''.join(c * 2 for c in value)
    red, green, blue = (int(value[i:i + 2], 16) / 255 for i in (0, 2, 4))
    hue, saturation, brightness = colorsys.rgb_to_hsv(red, green, blue)
    # Neutral foregrounds adapt to the active light/dark palette at render time.
    if brightness < 0.10:
        return '#334155'
    if saturation < 0.08:
        return '#' + value.lower()
    shift, saturation_scale, brightness_scale = style[2:]
    hue = (hue + shift) % 1
    saturation = min(1, saturation * saturation_scale)
    brightness = min(0.98, brightness * brightness_scale)
    channels = colorsys.hsv_to_rgb(hue, saturation, brightness)
    return '#' + ''.join(f'{round(c * 255):02x}' for c in channels)

def generate():
    originals = sorted((BASE / 'newlook').rglob('*.svg'))
    for identifier, style in STYLES.items():
        directory = BASE / identifier
        directory.mkdir(exist_ok=True)
        info = dict(name=style[0], name_it_IT=style[1], version='2.0',
                    author='Roy Qu; Red Panda C++ fork contributors', adaptive=True,
                    license='GPL-3.0-or-later',
                    origin='Color variants of repository New Look artwork; desktop-inspired palettes.')
        (directory / 'info.json').write_text(json.dumps(info, indent=4), encoding='utf-8')
        for original in originals:
            svg = original.read_text(encoding='utf-8')
            svg = re.sub(r'#[0-9a-fA-F]{6}\b|#[0-9a-fA-F]{3}\b',
                         lambda match: color_variant(match, style), svg)
            svg = svg.replace('<svg\n', '<svg data-redpanda-adaptive="1"\n', 1)
            svg = svg.replace('<!-- Created with Inkscape',
                              '<!-- Derived from Roy Qu New Look artwork, GPL-3.0-or-later. -->\n<!-- Created with Inkscape', 1)
            target = directory / original.relative_to(BASE / 'newlook')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(svg, encoding='utf-8')
        print(identifier, len(originals), 'color SVG icons')

if __name__ == '__main__':
    generate()
