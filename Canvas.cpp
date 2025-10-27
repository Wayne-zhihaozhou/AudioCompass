#include "Canvas.h"
#include <string>
#include <gdiplus.h>

using namespace Gdiplus;

// 构造函数，初始化 GDI+ 和窗口
Canvas::Canvas(HINSTANCE hInst) {
    static bool gdiInit = false;
    static ULONG_PTR gdiToken;
    if (!gdiInit) {
        GdiplusStartupInput gi;
        GdiplusStartup(&gdiToken, &gi, nullptr);
        gdiInit = true;
    }

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    radius_ = min(w, h) * 0.25f;
    penWidth_ = max(2.0f, radius_ * 0.02f);

    initWindow(hInst);
}

// 析构函数，释放资源
Canvas::~Canvas() {
    destroy();
}

// 初始化透明叠加窗口
void Canvas::initWindow(HINSTANCE hInst) {
    const wchar_t CLASS_NAME[] = L"OverlayCanvasWindow";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    hwnd_ = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        CLASS_NAME, L"AudioOverlay",
        WS_POPUP,
        0, 0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd_) {
        DWORD err = GetLastError();
        MessageBox(nullptr, L"Failed to create window", std::to_wstring(err).c_str(), MB_OK);
        return;
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

// 销毁窗口和释放资源
void Canvas::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    delete bmp_;
    delete g_;
    delete pen_;
    delete font_;
    delete brush_;

    bmp_ = nullptr;
    g_ = nullptr;
    pen_ = nullptr;
    font_ = nullptr;
    brush_ = nullptr;
}

// 显示窗口
void Canvas::show() {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

// 窗口回调函数
LRESULT CALLBACK Canvas::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

// 绘制指定角度的弧形和文字
void Canvas::drawArc(float angleDeg) {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    const float arcSpan = 5.0f;                  // 弧线跨度
    const float cx = w * 0.5f;                   // 屏幕中心 X
    const float cy = h * 0.5f;                   // 屏幕中心 Y

    float gdiCenterAngle = 270.0f + angleDeg;
    float startAngle = gdiCenterAngle - arcSpan / 2.f;

    int size = static_cast<int>(radius_ + penWidth_) * 2;

    // 创建或更新 Bitmap/Graphics
    if (!bmp_ || cachedSize_ != size) {
        delete bmp_;
        delete g_;
        bmp_ = new Bitmap(size, size, PixelFormat32bppPARGB);
        g_ = new Graphics(bmp_);
        cachedSize_ = size;
    }

    g_->SetSmoothingMode(SmoothingModeAntiAlias);
    g_->Clear(Color(0, 0, 0, 0));

    // 初始化画笔
    if (!pen_) {
        pen_ = new Pen(Color(255, 255, 0, 0), penWidth_);
        pen_->SetLineJoin(LineJoinRound);
        pen_->SetStartCap(LineCapRound);
        pen_->SetEndCap(LineCapRound);
    }

    // 初始化文字画刷
    if (!brush_) brush_ = new SolidBrush(Color(255, 255, 255, 255));

    // 初始化字体
    if (!font_) {
        FontFamily fontFamily(L"Arial");
        font_ = new Font(&fontFamily, radius_ * 0.08f, FontStyleBold, UnitPixel);
    }

    // 绘制弧形
    RectF rect(penWidth_ / 2, penWidth_ / 2, radius_ * 2, radius_ * 2);
    g_->DrawArc(pen_, rect, startAngle, arcSpan);

    // 绘制文字
    std::wstring angleText = L"Angle: " + std::to_wstring(static_cast<int>(angleDeg)) + L"°";
    PointF textPos(rect.Width * 0.5f - 40, 10);
    g_->DrawString(angleText.c_str(), -1, font_, textPos, brush_);

    // 更新透明叠加窗口
    HBITMAP hb;
    bmp_->GetHBITMAP(Color(0, 0, 0, 0), &hb);
    HDC wndDC = GetDC(0);
    HDC memDC = CreateCompatibleDC(wndDC);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, hb));

    POINT ptDst = { static_cast<LONG>(cx - size / 2), static_cast<LONG>(cy - size / 2) };
    SIZE sz = { size, size };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd_, wndDC, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    // 清理 GDI 对象
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(hwnd_, wndDC);
    DeleteObject(hb);
}

