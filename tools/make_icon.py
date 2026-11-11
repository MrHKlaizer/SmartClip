# Иконка Master Clipboard (MCB): рисует в 1024 px (mcb_1024.png) и превью размеров.
#   python tools/make_icon.py <папка_вывода>
# По умолчанию — утверждённый вариант (2026-10-30): лужица «Б», плоская, без тени,
# краска только на рамке. Необязательные аргументы (для проб):
#   [вишня R,G,B] [бордо R,G,B] [лужица A|B|C] [тень none|dr|r|soft] [clip|noclip]
# Из 1024 .ico собирается со всеми размерами 16–256 (см. assets/icons/masterclipboard.ico).
import sys, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

OUT = sys.argv[1]
S = 4                      # 256 -> 1024
def sc(v): return int(round(v * S))
CHERRY = tuple(int(x) for x in (sys.argv[2] if len(sys.argv) > 2 else '196,72,102').split(','))
BURG   = tuple(int(x) for x in (sys.argv[3] if len(sys.argv) > 3 else '150,36,66').split(','))
BLUE   = (80, 160, 255)
DARK   = (28, 28, 38)
TEXT   = (232, 242, 255)
N = 1024

img = Image.new('RGBA', (N, N), (0, 0, 0, 0))
# 1. Фон: скруглённый квадрат с лёгким градиентом сверху вниз
grad = Image.new('RGBA', (N, N))
gd = ImageDraw.Draw(grad)
for y in range(N):
    t = y / N
    c = tuple(int(a + (b - a) * t) for a, b in zip((42, 44, 60), (29, 29, 40)))
    gd.line([(0, y), (N, y)], fill=c + (255,))
mask = Image.new('L', (N, N), 0)
ImageDraw.Draw(mask).rounded_rectangle([sc(8), sc(8), sc(247.9), sc(247.9)], radius=sc(44), fill=255)
img.paste(grad, (0, 0), mask)
d = ImageDraw.Draw(img)

# 2. Обводка планшета — вишнёвая
d.rounded_rectangle([sc(29), sc(40), sc(226.9), sc(230.9)], radius=sc(36), outline=CHERRY, width=sc(14))
# 3. Зажим сверху — синий, с тёмным «вырезом» вокруг
d.rounded_rectangle([sc(84), sc(21), sc(171.9), sc(66)], radius=sc(16), fill=DARK)
d.rounded_rectangle([sc(95), sc(31), sc(160.9), sc(61.9)], radius=sc(11), fill=BLUE)
d.rounded_rectangle([sc(104), sc(38.5), sc(151.9), sc(50)], radius=sc(5), fill=DARK)
# 4. Линия под буквами — бордовая
d.rounded_rectangle([sc(92), sc(186), sc(163.9), sc(195.9)], radius=sc(5), fill=BURG)

# 5. Буквы MCB
font = ImageFont.truetype(r'C:\Windows\Fonts\seguibl.ttf', sc(52))
text = 'MCB'
bb = d.textbbox((0, 0), text, font=font)
tw, th = bb[2] - bb[0], bb[3] - bb[1]
cx, cy = sc(118), sc(128)
x0 = cx - tw // 2 - bb[0]
y0 = cy - th // 2 - bb[1]
# «MC» — обычные; «B» — отдельной маской, чтобы недописать
mc_w = d.textbbox((0, 0), 'MC', font=font)[2]
d.text((x0, y0), 'MC', font=font, fill=TEXT)
bmask = Image.new('L', (N, N), 0)
ImageDraw.Draw(bmask).text((x0 + mc_w, y0), 'B', font=font, fill=255)
bx = bmask.getbbox()
# Недописано: нижняя часть нижней «петли» справа — по дуге от центра петли
bcx = bx[0] + (bx[2] - bx[0]) * 0.55
bcy = bx[1] + (bx[3] - bx[1]) * 0.74
cut = Image.new('L', (N, N), 0)
cd = ImageDraw.Draw(cut)
R = (bx[2] - bx[0]) * 1.2
a0, a1 = 40, 180   # Сектор, который «ещё не написан» (градусы, 0 — вправо, по часовой вниз)
pts = [(bcx, bcy)] + [(bcx + R * math.cos(math.radians(a)), bcy + R * math.sin(math.radians(a))) for a in range(a0, a1 + 1, 3)]
cd.polygon(pts, fill=255)
# Палочку «B» не трогаем — она уже написана целиком (без «хвостика» снизу)
stem_right = bx[0]
for xx in range(bx[0], bx[2]):   # где кончается палочка: по середине высоты пусто справа от неё
    if bmask.getpixel((xx, (bx[1] + bx[3]) // 2 + (bx[3] - bx[1]) // 8)) < 128: stem_right = xx; break
ImageDraw.Draw(cut).rectangle([0, 0, stem_right - 1, N], fill=0)
bdrawn = ImageChops.subtract(bmask, cut)
img.paste(Image.new('RGBA', (N, N), TEXT + (255,)), (0, 0), bdrawn)

# 6. Перо: кончик — там, где обрывается штрих (край сектора a0 на внешнем контуре)
ang = math.radians(a0)
# найти внешнюю точку штриха вдоль луча a0
tip = None
for r in range(int(R), 0, -1):
    x = bcx + r * math.cos(ang); y = bcy + r * math.sin(ang)
    if 0 <= int(x) < N and 0 <= int(y) < N and bmask.getpixel((int(x), int(y))) > 128:
        tip = (x, y); break
inner = None
for r in range(0, int(R)):
    x = bcx + r * math.cos(ang); y = bcy + r * math.sin(ang)
    if 0 <= int(x) < N and 0 <= int(y) < N and bmask.getpixel((int(x), int(y))) > 128:
        inner = (x, y); break
# Кончик пера — посередине толщины штриха, прямо на его конце
tx, ty = ((tip[0] + inner[0]) / 2, (tip[1] + inner[1]) / 2) if inner else tip
# Перо лежит по диагонали вверх-вправо от кончика
L = sc(84)                          # длина пера
dirx, diry = math.cos(math.radians(-36)), math.sin(math.radians(-36))
nx, ny = -diry, dirx                # перпендикуляр
end = (tx + dirx * L, ty + diry * L)
feather = Image.new('RGBA', (N, N), (0, 0, 0, 0))
fd = ImageDraw.Draw(feather)
# Опахало: вытянутый «лист» от 25% до 100% длины, шире к концу
def pt(t, w):
    return (tx + dirx * L * t + nx * w, ty + diry * L * t + ny * w)
left = [pt(t / 20, sc(15) * math.sin(math.pi * min(1, (t / 20 - 0.22) / 0.78)) ** 0.8 if t / 20 > 0.22 else 0) for t in range(0, 21)]
right = [pt(t / 20, -sc(11) * math.sin(math.pi * min(1, (t / 20 - 0.3) / 0.7)) ** 0.8 if t / 20 > 0.3 else 0) for t in range(20, -1, -1)]
fd.polygon(left + right, fill=(240, 236, 228, 255), outline=(40, 40, 52, 255))
# Бородки — тонкие косые линии
for i in range(5, 19):
    t = i / 20
    a = pt(t, 0); b = pt(t + 0.07, sc(13) * math.sin(math.pi * min(1, (t - 0.22) / 0.78)) ** 0.8 if t > 0.22 else 0)
    fd.line([a, b], fill=(170, 168, 175, 255), width=sc(1))
# Стержень и кончик
fd.line([pt(0.06, 0), pt(1.0, 0)], fill=(90, 90, 105, 255), width=sc(2.4))
# Металлический кончик — острый, в белой краске (пишет белым), с прорезью
nib = [pt(0, 0), pt(0.10, sc(3.6)), pt(0.20, sc(3.2)), pt(0.22, 0), pt(0.20, -sc(3.2)), pt(0.10, -sc(3.6))]
fd.polygon(nib, fill=(238, 244, 255, 255), outline=(70, 74, 92, 255))
fd.line([pt(0.015, 0), pt(0.13, 0)], fill=(90, 96, 118, 255), width=max(1, sc(0.9)))
fd.ellipse([pt(0.135, 0)[0] - sc(1.2), pt(0.135, 0)[1] - sc(1.2), pt(0.135, 0)[0] + sc(1.2), pt(0.135, 0)[1] + sc(1.2)],
           fill=(90, 96, 118, 255))
img.alpha_composite(feather)

# Капля белой краски падает с пера, внизу — лужица на конце бордовой линии
INK = TEXT + (255,)
d = ImageDraw.Draw(img)
ux1, uy0, uy1 = sc(163.9), sc(186), sc(195.9)          # Правый конец линии
# Кончик бордовой линии испачкан белым — аккуратно, без размазни
end = Image.new('L', (N, N), 0)
ImageDraw.Draw(end).rounded_rectangle([sc(92), uy0, ux1, uy1], radius=sc(5), fill=255)
ImageDraw.Draw(end).rectangle([0, 0, bx[0] - 1, N], fill=0)   # Белая — на всю ширину буквы «B»
img.paste(Image.new('RGBA', (N, N), INK), (0, 0), end)
# Лужица — на нижней части вишнёвой обводки (капля падает на неё с пера)
fy = sc(217)                     # Верхний край нижней обводки
px_ = (bx[0] + bx[2]) / 2        # Ровно под буквой «B»
VAR = sys.argv[4] if len(sys.argv) > 4 else 'B'
SHADE = (176, 190, 214, 255)     # Тень краски
HI = (255, 255, 255, 255)        # Блик
if VAR == 'A':
    # Выпуклая лужица: тело, тень по низу, блик сверху
    d.ellipse([px_ - sc(15), fy - sc(7), px_ + sc(15), fy + sc(5)], fill=SHADE)
    d.ellipse([px_ - sc(15), fy - sc(8), px_ + sc(15), fy + sc(3)], fill=INK)
    d.ellipse([px_ + sc(10), fy - sc(4), px_ + sc(20), fy + sc(3)], fill=SHADE)
    d.ellipse([px_ + sc(10), fy - sc(5), px_ + sc(20), fy + sc(2)], fill=INK)
    d.ellipse([px_ - sc(9), fy - sc(6.5), px_ - sc(1), fy - sc(3.5)], fill=HI)
    d.ellipse([px_ + sc(22), fy - sc(7), px_ + sc(25.5), fy - sc(3.5)], fill=INK)
elif VAR == 'B':
    # Плоская краска: лужица на рамке + подтёк по лицевой стороне рамки.
    # Объём — только тенью на рамку (SH: none / dr — вниз-вправо / r — вправо /
    # soft — мягкая вниз / in — тень внутрь, от рамки на лужицу не падает)
    SH = sys.argv[5] if len(sys.argv) > 5 else 'none'
    paint = Image.new('L', (N, N), 0)
    pd = ImageDraw.Draw(paint)
    pd.ellipse([px_ - sc(21), fy - sc(6), px_ + sc(21), fy + sc(3)], fill=255)   # Шире в стороны
    pd.rectangle([px_ + sc(3.5), fy - sc(2), px_ + sc(8.5), fy + sc(9.9)], fill=255)   # Как первый (толщина 5), но длиннее
    pd.ellipse([px_ + sc(3.5), fy + sc(7.4), px_ + sc(8.5), fy + sc(12.4)], fill=255)   # Кончик — целиком на рамке
    pd.rectangle([px_ - sc(9.5), fy - sc(1), px_ - sc(4.5), fy + sc(6.75)], fill=255)   # Ровное тельце — шириной с капельку
    pd.ellipse([px_ - sc(9.5), fy + sc(4.5), px_ - sc(4.5), fy + sc(9)], fill=255)
    if not (len(sys.argv) > 6 and sys.argv[6] == 'noclip'):
        # Краска — только на самой рамке: не торчит внутрь и не свисает наружу
        frame = Image.new('L', (N, N), 0)
        ImageDraw.Draw(frame).rounded_rectangle([sc(29), sc(40), sc(226.9), sc(230.9)], radius=sc(36), outline=255, width=sc(14))
        paint = ImageChops.multiply(paint, frame)
    if SH != 'none':
        off = {'dr': (sc(2.5), sc(2.5)), 'r': (sc(3), 0), 'soft': (sc(1), sc(3)), 'in': (sc(2), sc(2))}[SH]
        sh = Image.new('L', (N, N), 0)
        sh.paste(paint, off)
        if SH == 'soft':
            sh = sh.filter(ImageFilter.GaussianBlur(sc(1.6)))
        sh = ImageChops.subtract(sh, paint)            # Тень видна только вокруг краски
        # Затемняем то, что под тенью (рамку и фон), а не красим чёрным
        dark = img.copy()
        r_, g_, b_, a_ = dark.split()
        k = 0.55 if SH != 'soft' else 0.6
        dark = Image.merge('RGBA', [c.point(lambda v: int(v * k)) for c in (r_, g_, b_)] + [a_])
        img.paste(dark, (0, 0), sh)
    img.paste(Image.new('RGBA', (N, N), INK), (0, 0), paint)
    d = ImageDraw.Draw(img)
else:
    # Всплеск: лужица и «корона» брызг вверх
    d.ellipse([px_ - sc(14), fy - sc(5), px_ + sc(14), fy + sc(4)], fill=SHADE)
    d.ellipse([px_ - sc(14), fy - sc(6), px_ + sc(14), fy + sc(2)], fill=INK)
    for ang_, ln in ((-160, 9), (-125, 12), (-90, 13), (-55, 12), (-20, 9)):
        ra = math.radians(ang_)
        x1 = px_ + math.cos(ra) * sc(10); y1 = fy - sc(4) + math.sin(ra) * sc(4)
        x2 = px_ + math.cos(ra) * sc(10 + ln * 0.6); y2 = fy - sc(4) + math.sin(ra) * sc(ln)
        d.line([(x1, y1), (x2, y2)], fill=INK, width=sc(2.6))
        d.ellipse([x2 - sc(2.2), y2 - sc(2.2), x2 + sc(2.2), y2 + sc(2.2)], fill=INK)
    d.ellipse([px_ - sc(7), fy - sc(4.5), px_ + sc(1), fy - sc(2.5)], fill=HI)

# Капля — летит с пера вниз, на полпути (мимо конца бордовой линии)
dx, dy = tx, (ty + uy0) / 2 + sc(3)       # Прямо под кончиком пера, на полпути до линии
rr = sc(3.8)
d.ellipse([dx - rr, dy - rr, dx + rr, dy + rr], fill=INK)
d.polygon([(dx - rr * 0.82, dy - rr * 0.55), (dx + rr * 0.82, dy - rr * 0.55), (dx, dy - rr * 2.6)], fill=INK)

img.save(OUT + r'\mcb_1024.png')
# Превью: все размеры и «в трее»
sizes = [256, 128, 64, 48, 32, 24, 20, 16]
sheet = Image.new('RGBA', (sum(s + 16 for s in sizes) + 16, 300), (30, 30, 40, 255))
x = 16
for s in sizes:
    sm = img.resize((s, s), Image.LANCZOS)
    sheet.alpha_composite(sm, (x, 16))
    x += s + 16
sheet.save(OUT + r'\mcb_sizes.png')
# Увеличенные мелкие — как видно в трее (пикселями)
row = Image.new('RGBA', (5 * 140, 140), (30, 30, 40, 255))
for k, s in enumerate([48, 32, 24, 20, 16]):
    sm = img.resize((s, s), Image.LANCZOS).resize((128, 128), Image.NEAREST)
    row.alpha_composite(sm, (k * 140 + 6, 6))
row.save(OUT + r'\mcb_small_zoom.png')
print('tip', tip)
