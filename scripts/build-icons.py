"""Convert the approved logo to a multi-resolution Windows icon.

Requires Pillow. The source artwork is preserved without redrawing or recoloring.
Run from any folder: python scripts/build-icons.py
"""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parent.parent
image = Image.open(root / "assets/radial-logo.png").convert("RGBA")
image.save(root / "assets/radial.ico", format="ICO",
           sizes=[(n, n) for n in (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)])
