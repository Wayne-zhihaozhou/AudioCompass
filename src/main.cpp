#include <windows.h>
#include "AudioCapture.h"
#include "Canvas.h"

Canvas* g_canvas = nullptr;  // 全局 Canvas 对象指针

int WINAPI WinMain(
    _In_ HINSTANCE hInst,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nShowCmd
) {
    // 创建全局互斥量，防止程序多开
    HANDLE _mutex = CreateMutex(nullptr, TRUE, L"MyUniqueAudioCaptureAppMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(nullptr, 
            L"程序已运行，请勿重复启动。"
            L"若需要关闭程序，请打开任务管理器，在后台查找AudioCompass.exe并手动结束。",
            L"提示", MB_OK | MB_ICONWARNING);
        return 0;  // 程序退出
    }

    // 提示用户程序已开启
    MessageBox(
        nullptr,
        L"音频监测程序已启动，将在电脑有声音播放时自动工作。\n\n"
        L"若需要关闭程序，请打开任务管理器，在后台查找AudioCompass.exe并手动结束。",
        L"程序已开启",
        MB_OK | MB_ICONINFORMATION
    );

    /*
    如果想要在任务栏显示窗口,用于手动关闭程序.
    请将 initWindow()函数中的 CreateWindowEx 内参数设置为"WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST",取消掉 WS_EX_TOOLWINDOW 标志.
    */

    // 创建透明叠加窗口 Canvas
    g_canvas = new Canvas(hInst);
    HWND hwnd = g_canvas->getHwnd();  // 获取窗口句柄

    // 初始化音频捕获
    AudioCapture ac;
    ac.setMainWindowHandle(hwnd);
    ac.outputWavFile = "high_freq_audio.wav";
    ac.start();

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_USER + 100) {
            auto event = reinterpret_cast<AudioCapture::AudioEvent*>(msg.lParam);
            if (event->highFreq && g_canvas) {
                g_canvas->drawArc(event->angle);
            }
            delete event;
        }
        else if (msg.message == WM_USER + 101) {
            g_canvas->clear();
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ac.stop();
    delete g_canvas;
    ReleaseMutex(_mutex);  // 释放互斥量
    CloseHandle(_mutex);   // 关闭句柄
    return 0;
}