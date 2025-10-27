#include "AudioCapture.h"
#include "Canvas.h"

Canvas* g_canvas = nullptr;

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    g_canvas = new Canvas(hInst);
    HWND hwnd = g_canvas->getHwnd();

    AudioCapture ac;
    ac.setMainWindowHandle(hwnd);
    ac.outputWavFile = "high_freq_audio.wav";
    ac.start();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_USER + 100) {
            auto event = reinterpret_cast<AudioCapture::AudioEvent*>(msg.lParam);
            //// 弹窗显示 angle 值
            //std::wstring angleStr = L"Angle: " + std::to_wstring(event->angle);
            //MessageBox(hwnd, angleStr.c_str(), L"Audio Event", MB_OK);
            g_canvas->drawArc(event->angle);
            //if (event->highFreq && g_canvas) {
               // g_canvas->drawArc(event->angle);
            //}
            delete event;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ac.stop();
    delete g_canvas;
    return 0;
}



