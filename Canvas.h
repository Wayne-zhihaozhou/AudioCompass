#pragma once
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

/**
 * @class Canvas
 * @brief 管理透明叠加窗口和绘制逻辑
 */
class Canvas {
public:
    Canvas(HINSTANCE hInst);
    ~Canvas();

    HWND getHwnd() const { return hwnd_; }
    void drawArc(float angleDeg);
    void show();
    void destroy();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    void initWindow(HINSTANCE hInst);

    HWND hwnd_ = nullptr;
    Bitmap* bmp_ = nullptr;
    Graphics* g_ = nullptr;
    Pen* pen_ = nullptr;
    Font* font_ = nullptr;
    SolidBrush* brush_ = nullptr;
    int cachedSize_ = 0;
    float radius_ = 0;
    float penWidth_ = 0;
};
