import sys, glob
from PIL import Image
pattern, out, start, count, cols = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
fs = sorted(glob.glob(pattern))[start:start + count]
W, H = 186, 180
rows = (len(fs) + cols - 1) // cols
sheet = Image.new('RGB', (W * cols, (H + 14) * rows))
from PIL import ImageDraw
d = ImageDraw.Draw(sheet)
for i, f in enumerate(fs):
    im = Image.open(f).convert('RGB')
    if im.height > 600: im = im.crop((8, 31, im.width - 8, im.height - 8))
    im = im.resize((W, H))
    x, y = (i % cols) * W, (i // cols) * (H + 14)
    sheet.paste(im, (x, y + 14))
    d.text((x + 2, y), f.split('\\')[-1].split('/')[-1], fill=(255, 255, 0))
sheet.save(out)
