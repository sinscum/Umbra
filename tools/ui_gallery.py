#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""UMBRA: the UI test's screens (tests/ui_render.c -> build/ui_new) as a page to review, published with the emulator.
  tools/ui_gallery.py build/ui_new OUT_DIR
OUT_DIR gets each palette's contact sheet and the GREY screens, and index.html listing them (UMBRA's own screens,
named chain_*, first)."""
import html
import shutil
import sys
from pathlib import Path


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    sheets = sorted(src.glob("sheet_*.png"))
    for s in sheets:
        shutil.copy(s, out / s.name)
    grey = sorted((src / "GREY").glob("*.png"), key=lambda p: (not p.stem.startswith("chain_"), p.stem))
    (out / "GREY").mkdir(exist_ok=True)
    for g in grey:
        shutil.copy(g, out / "GREY" / g.name)
    cells = "\n".join(f'<figure><img src="GREY/{html.escape(g.name)}" width="240" height="240" alt="">'
                      f'<figcaption>{html.escape(g.stem)}</figcaption></figure>' for g in grey)
    links = " · ".join(f'<a href="{html.escape(s.name)}">{html.escape(s.stem[6:])}</a>' for s in sheets)
    (out / "index.html").write_text(f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>UMBRA screens</title>
<style>
:root {{ --bg: #111; --fg: #ddd; --mid: #888; }}
body {{ background: var(--bg); color: var(--fg); font: 14px/1.4 system-ui, sans-serif; margin: 0; padding: 16px; }}
main {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: 16px; }}
figure {{ margin: 0; }} img {{ image-rendering: pixelated; max-width: 100%; height: auto; }}
figcaption {{ color: var(--mid); font-size: 12px; }} a {{ color: var(--fg); }}
</style></head><body>
<h1>UMBRA screens</h1>
<p>The UI test's renders of this build (GREY palette; UMBRA's chain screens first). Every palette's sheet: {links}.
<a href="../">Back to the emulator</a>.</p>
<main>
{cells}
</main></body></html>
""")
    print(f"ui gallery: {len(grey)} screens, {len(sheets)} sheets -> {out}")


if __name__ == "__main__":
    main()
