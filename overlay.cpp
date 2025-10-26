// overlay.cpp
#include "overlay.h"

#include <windowsx.h>
#include <gdiplus.h>
#include <thread>
#include <chrono>
#include <cassert>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {
    // Helper: normalize degrees to [0,360)
    static float NormalizeAngle(float a) {
        while (a < 0.f) a += 360.f;
        while (a >= 360.f) a -= 360.f;
        return a;
    }

    // Create a 32bpp ARGB Gdiplus::Bitmap and return pointer
    static Bitmap* CreateBitmapForSize(int w, int h) {
        return new Bitmap(w, h, PixelFormat32bppPARGB);
    }

    // Helper to convert degrees to radians
    static inline float DegToRad(float d) { return d * 3.14159265358979323846f / 180.f; }
}

OverlayWindow::OverlayWindow()
    : hwnd_(NULL),
    running_(false),
    angleDeg_(0.f),
    needRedraw_(false),
    hInstance_(GetModuleHandle(NULL)) {
}

OverlayWindow::~OverlayWindow() {
    Shutdown();
}

bool OverlayWindow::Initialize(HINSTANCE hInstance) {
    hInstance_ = hInstance;
    if (!CreateOverlayWindow()) return false;

    running_.store(true);
    // Start render thread
    std::thread([this]() { this->RenderLoop(); }).detach();
    return true;
}

void OverlayWindow::Shutdown() {
    running_.store(false);
    // Ensure window destroyed
    DestroyOverlayWindow();
}

void OverlayWindow::SetAngle(float angleDeg) {
    angleDeg_.store(NormalizeAngle(angleDeg));
    needRedraw_.store(true);
}

void OverlayWindow::Invalidate() {
    needRedraw_.store(true);
}

bool OverlayWindow::CreateOverlayWindow() {
    // Register class
    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWindow::WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(void*);
    wc.hInstance = hInstance_;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"HighPerfOverlayClass";

    RegisterClassEx(&wc);

    // Create full-screen window on primary monitor
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"OverlayWindow",
        WS_POPUP,
        0, 0, sx, sy,
        NULL,
        NULL,
        hInstance_,
        this
    );

    if (!hwnd_) return false;

    // Associate pointer for WndProc
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, (LONG_PTR)this);

    // Show without activating (so it won't steal focus)
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    // Make layered but initially fully transparent
    // We'll use UpdateLayeredWindow to present the content; still set a default alpha.
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);

    return true;
}

void OverlayWindow::DestroyOverlayWindow() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = NULL;
    }
    UnregisterClass(L"HighPerfOverlayClass", hInstance_);
}

void OverlayWindow::RenderLoop() {
    // Initialize GDI+ (one per process ideally; we init here)
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Ok) {
        return;
    }

    // Target refresh rate: try to limit to 60 FPS to be performant
    const int targetFps = 60;
    const std::chrono::milliseconds frameMs(1000 / targetFps);

    while (running_.load()) {
        if (needRedraw_.load()) {
            DoDraw();
            needRedraw_.store(false);
        }
        // Sleep a bit (also allow angle updates)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Optionally throttle to frame rate if continuous invalidation
        // (not necessary because we draw only on demand)
    }

    GdiplusShutdown(gdiplusToken);
}

void OverlayWindow::DoDraw() {
    if (!hwnd_) return;

    RECT rc;
    GetWindowRect(hwnd_, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    // Create an ARGB bitmap to draw into
    std::unique_ptr<Bitmap> bmp(CreateBitmapForSize(w, h));
    if (!bmp) return;

    {
        // Create graphics and draw arc
        std::unique_ptr<Graphics> g(Graphics::FromImage(bmp.get()));
        if (!g) return;
        // High quality
        g->SetSmoothingMode(SmoothingModeAntiAlias);
        g->Clear(Color(0, 0, 0, 0)); // fully transparent background

        // compute arc parameters
        const float angle = angleDeg_.load();
        // arc length degrees
        const float arcSpan = 40.0f; // angular span of arc (modify as needed)
        // radius
        const float radius = min(w, h) * 0.25f;
        // center
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;

        // convert to GDI+ arc rectangle (GDI+ uses degrees clockwise from x-axis).
        // We want 0 deg = up; so convert accordingly:
        // ourZero (up) => GDI+ zero (right) => GDIDegree = 90 - angle
        float gdiCenterAngle = 90.f - angle;
        float startAngle = gdiCenterAngle - arcSpan * 0.5f;
        float sweep = arcSpan;

        // Pen for arc: width adjustable
        const float penWidth = max(2.0f, radius * 0.02f);
        Pen pen(Color(255, 255, 0, 0), penWidth); // red opaque
        pen.SetLineJoin(LineJoinRound);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);

        // Build bounding rectangle for the arc
        RectF rect(cx - radius, cy - radius, radius * 2.f, radius * 2.f);

        // Draw arc
        g->DrawArc(&pen, rect, startAngle, sweep);

        // Optionally draw small marker at arc center
        float midAngle = startAngle + sweep * 0.5f;
        float rad = DegToRad(midAngle);
        float mx = cx + radius * cos(rad);
        float my = cy - radius * sin(rad);
        SolidBrush brush(Color(255, 255, 255, 255));
        float markerR = penWidth * 1.2f;
        g->FillEllipse(&brush, mx - markerR / 2, my - markerR / 2, markerR, markerR);
    }

    // Convert Gdiplus::Bitmap to HBITMAP (with alpha)
    HBITMAP hBitmap = NULL;
    Color bg(0, 0, 0, 0);
    if (bmp->GetHBITMAP(bg, &hBitmap) != Ok || !hBitmap) {
        return;
    }

    // Use UpdateLayeredWindow to present the ARGB HBITMAP
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    HGDIOBJ oldBmp = SelectObject(memDC, hBitmap);

    SIZE sizeWindow = { w, h };
    POINT ptZero = { 0, 0 };
    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255; // use source alpha
    blend.AlphaFormat = AC_SRC_ALPHA;

    POINT topPos = { rc.left, rc.top };
    // UpdateLayeredWindow: updates the whole window using the provided ARGB bitmap
    BOOL ok = UpdateLayeredWindow(hwnd_, screenDC, &topPos, &sizeWindow, memDC, &ptZero, 0, &blend, ULW_ALPHA);

    // cleanup
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
    DeleteObject(hBitmap);

    (void)ok; // ignore return - could log in debug
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        // tell OS we handle background (do nothing)
        return 1;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}



