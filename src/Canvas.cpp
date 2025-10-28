﻿#include "Canvas.h"
#include <string>
#include <gdiplus.h>
#include <chrono>
#include <cmath>

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
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
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

// 绘制弧形和残影
// 绘制弧形和残影
void Canvas::drawArc(float angleDeg) {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;

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

    // 实时弧形画笔
    Pen livePen(liveColor);
    livePen.SetWidth(penWidth_);
    livePen.SetLineJoin(LineJoinRound);
    livePen.SetStartCap(LineCapRound);
    livePen.SetEndCap(LineCapRound);

    // 初始化文字画刷和字体
    if (!brush_) brush_ = new SolidBrush(textColor);
    if (!font_) {
        FontFamily fontFamily(L"Arial");
        font_ = new Font(&fontFamily, radius_ * 0.08f, FontStyleBold, UnitPixel);
    }

    // 当前时间
    ULONGLONG now = GetTickCount64();

    // 只在角度超过阈值时计算残影持续时间并加入队列
    if (fabs(angleDeg) > trailAngleThreshold) {
        float normalizedAngle = min(fabs(angleDeg), 90.0f) / 90.0f; // 0~1
        float dynamicTrailDuration = trailBaseDuration + normalizedAngle * (trailMaxDuration - trailBaseDuration);
        arcTrails_.push_back({ angleDeg, now, dynamicTrailDuration });
    }

    // 绘制残影（红色 + alpha 衰减）
    for (auto it = arcTrails_.begin(); it != arcTrails_.end();) {
        float age = static_cast<float>(now - it->ts) / 1000.0f;
        if (age > it->duration) {
            it = arcTrails_.erase(it);
            continue;
        }

        float alpha = 255.0f * (1.0f - age / it->duration);
        Pen trailPen(Color(static_cast<BYTE>(alpha),
            trailColor.GetRed(),
            trailColor.GetGreen(),
            trailColor.GetBlue()), penWidth_);
        trailPen.SetLineJoin(LineJoinRound);
        trailPen.SetStartCap(LineCapRound);
        trailPen.SetEndCap(LineCapRound);

        float gdiAngle = 270.0f + it->angle;
        RectF rect(penWidth_ / 2, penWidth_ / 2, radius_ * 2, radius_ * 2);
        g_->DrawArc(&trailPen, rect, gdiAngle - arcSpan / 2.f, arcSpan);

        ++it;
    }

    // 绘制实时弧形
    float gdiCenterAngle = 270.0f + angleDeg;
    RectF rect(penWidth_ / 2, penWidth_ / 2, radius_ * 2, radius_ * 2);
    g_->DrawArc(&livePen, rect, gdiCenterAngle - arcSpan / 2.f, arcSpan);

    // 绘制文字
    std::wstring angleText = L"Angle: " + std::to_wstring(static_cast<int>(angleDeg)) + L"°";
    PointF textPos(rect.Width * 0.5f - 40, 10);
    g_->DrawString(angleText.c_str(), -1, font_, textPos, brush_);

    // 更新透明叠加窗口
    HBITMAP hb = nullptr;
    if (bmp_->GetHBITMAP(Color(0, 0, 0, 0), &hb) != Ok || hb == nullptr) {
        OutputDebugStringW(L"[Canvas] GetHBITMAP failed or returned null\n");
        return;
    }

    // 使用屏幕 DC（GetDC(NULL)）并配对 ReleaseDC(NULL,...)
    HDC wndDC = GetDC(NULL);
    if (!wndDC) {
        OutputDebugStringW(L"[Canvas] GetDC(NULL) failed\n");
        DeleteObject(hb);
        return;
    }

    HDC memDC = CreateCompatibleDC(wndDC);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, hb));

    POINT ptDst = { static_cast<LONG>(cx - size / 2), static_cast<LONG>(cy - size / 2) };
    SIZE sz = { size, size };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    BOOL ok = UpdateLayeredWindow(hwnd_, wndDC, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);
    if (!ok) {
        DWORD err = GetLastError();
        wchar_t buf[128];
        swprintf_s(buf, L"[Canvas] UpdateLayeredWindow failed in drawArc: %u\n", err);
        OutputDebugStringW(buf);
        // 不把 hasContent_ 置 true，表示上传失败
    }
    else {
        hasContent_ = true; // 只有在成功上传后才认为屏幕有内容
    }

    // cleanup
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, wndDC);
    DeleteObject(hb);
}


// 清空屏幕（只在当前确实有内容时执行上传透明位图）
void Canvas::clear() {
    if (!hwnd_) {
        wchar_t buf[128];
        swprintf_s(buf, 128, L"highFreq = %d, angle = %.2f", hwnd_, hwnd_);
        MessageBox(nullptr, buf, L"hwnd_()", MB_OK | MB_ICONINFORMATION);
        return;
    }
       

    // 如果没有内容（上次已经是空），则无需再次清理
    if (!hasContent_ && arcTrails_.empty()) {
        return;
    }

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    int size = static_cast<int>(radius_ + penWidth_) * 2;
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;

    // 确保 bitmap/graphics 存在
    if (!bmp_ || cachedSize_ != size) {
        delete bmp_;
        delete g_;
        bmp_ = new Bitmap(size, size, PixelFormat32bppPARGB);
        g_ = new Graphics(bmp_);
        cachedSize_ = size;
    }

    // 清空 Bitmap 内容（全透明）
    g_->SetSmoothingMode(SmoothingModeNone);
    g_->Clear(Color(0, 0, 0, 0));

    // 生成透明位图上传
    HBITMAP hb = nullptr;
    if (bmp_->GetHBITMAP(Color(0, 0, 0, 0), &hb) != Ok || hb == nullptr) {
        OutputDebugStringW(L"[Canvas] GetHBITMAP failed in clear\n");
        return;
    }

    HDC wndDC = GetDC(NULL);
    if (!wndDC) {
        OutputDebugStringW(L"[Canvas] GetDC(NULL) failed in clear\n");
        DeleteObject(hb);
        return;
    }

    HDC memDC = CreateCompatibleDC(wndDC);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, hb));

    POINT ptDst = { static_cast<LONG>(cx - size / 2), static_cast<LONG>(cy - size / 2) };
    SIZE sz = { size, size };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    BOOL ok = UpdateLayeredWindow(hwnd_, wndDC, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);
    if (!ok) {
        DWORD err = GetLastError();
        wchar_t buf[128];
        swprintf_s(buf, L"[Canvas] UpdateLayeredWindow failed in clear: %u\n", err);
        OutputDebugStringW(buf);
        // 上传失败则保留 hasContent_ 的原值（以便后续重试）
    }
    else {
        // 上传成功：标记为空，并清除轨迹
        hasContent_ = false;
        arcTrails_.clear();
    }

    // cleanup
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, wndDC);
    DeleteObject(hb);
}
