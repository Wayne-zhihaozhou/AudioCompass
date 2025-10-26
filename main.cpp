#include "AudioCapture.h"
#include <iostream>


HWND createTransparentOverlay(HINSTANCE hInst) {
    const wchar_t CLASS_NAME[] = L"TransparentOverlayClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST, // 扩展样式：透明、置顶、鼠标穿透
        CLASS_NAME,
        L"Audio Overlay",
        WS_POPUP,  // 无边框窗口
        0, 0,
        GetSystemMetrics(SM_CXSCREEN),  // 全屏宽度
        GetSystemMetrics(SM_CYSCREEN),  // 全屏高度
        nullptr,
        nullptr,
        hInst,
        nullptr);

    // 设置透明度（255 = 不透明, 0 = 完全透明）
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    return hwnd;
}


int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // 创建透明叠加窗口
    HWND hwnd = createTransparentOverlay(hInst);

    AudioCapture ac;
    ac.setMainWindowHandle(hwnd);  // 👈 设置窗口句柄，用于 DrawOverlayArc 绘制
    ac.highFreqMin = 10000.0f;
    ac.highFreqEpsilon = 0.001f;
    ac.highFreqRatio = 0.1f;
    ac.outputWavFile = "high_freq_audio.wav";

    ac.start();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_USER + 100) {
            auto event = reinterpret_cast<AudioCapture::AudioEvent*>(msg.lParam);
            if (event->highFreq) {
                ac.DrawOverlayArc(event->angle );  // 绘制到透明层上
            }
            delete event;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ac.stop();
    return 0;
}


