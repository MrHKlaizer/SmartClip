#pragma once
#include <windows.h>

// ── DirectComposition backdrop blur ──────────────────────────────────────────
// Создаёт два чистых Win32-окна (без Qt) с DComp-визуалами,
// которые показывают размытый фон строго под левой и правой колонками.
// Центр между ними остаётся абсолютно прозрачным.

struct BlurRects {
    int screenW, screenH;
    int leftX,  topY;
    int leftW,  rightX, rightW;
    int colH;
};

bool  dcompBlurInit  (HWND mainHwnd, const BlurRects &r);
void  dcompBlurShow  (HWND mainHwnd, const BlurRects &r);
void  dcompBlurHide  ();
void  dcompBlurShutdown();
