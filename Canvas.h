#pragma once
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

class Canvas {
public:
    Canvas(HINSTANCE hInst);   // 构造函数，初始化 GDI+ 和窗口
    ~Canvas();                 // 析构函数，释放资源

    HWND getHwnd() const { return hwnd_; } // 获取窗口句柄
    void drawArc(float angleDeg);          // 绘制指定角度的弧形和文字
    void show();                           // 显示窗口
    void destroy();                        // 销毁窗口和释放资源

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp); // 窗口回调

private:
    void initWindow(HINSTANCE hInst);      // 初始化透明叠加窗口

    HWND hwnd_ = nullptr;                  // 窗口句柄
    Bitmap* bmp_ = nullptr;                // 绘图 Bitmap
    Graphics* g_ = nullptr;                // 绘图对象
    Pen* pen_ = nullptr;                   // 绘制弧线画笔
    Font* font_ = nullptr;                 // 绘制文字字体
    SolidBrush* brush_ = nullptr;          // 绘制文字画刷
    int cachedSize_ = 0;                   // 缓存 Bitmap 尺寸
    float radius_ = 0;                     // 弧线半径
    float penWidth_ = 0;                   // 画笔宽度
};
