#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

class Canvas {
public:
    Canvas(HINSTANCE hInst);   // 构造函数，初始化 GDI+ 和窗口
    ~Canvas();                 // 析构函数，释放资源

    HWND getHwnd() const { return hwnd_; } // 获取窗口句柄
    void drawArc(float angleDeg);          // 绘制指定角度的弧形和文字
    void clear();
    void show();                           // 显示窗口
    void destroy();                        // 销毁窗口和释放资源

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp); // 窗口回调

    // 用户可配置参数
    float trailAngleThreshold = 10.0f;      // 残影触发阈值（度）
    float trailBaseDuration = 0.2f;// 残影基础持续时间（秒）
    float trailMaxDuration = 1.0f;// 残影最大持续时间（秒）
    Color liveColor = Color(255, 255, 255, 0);   // 实时弧形颜色
    Color trailColor = Color(255, 255, 0, 0);    // 残影初始颜色
    float arcSpan = 2.0f;                   // 弧线跨度（度）
    Color textColor = Color(255, 255, 255, 0); // 文字颜色，默认白色

private:
    void initWindow(HINSTANCE hInst);      // 初始化透明叠加窗口

    HWND hwnd_ = nullptr;                  // 窗口句柄
    bool hasContent_ = false;              // 当前是否有绘制内容
    Bitmap* bmp_ = nullptr;                // 绘图 Bitmap
    Graphics* g_ = nullptr;                // 绘图对象
    Pen* pen_ = nullptr;                   // 绘制实时弧线画笔
    Font* font_ = nullptr;                 // 绘制文字字体
    SolidBrush* brush_ = nullptr;          // 绘制文字画刷
    int cachedSize_ = 0;                   // 缓存 Bitmap 尺寸
    float radius_ = 0;                     // 弧线半径
    float penWidth_ = 0;                   // 画笔宽度

    struct ArcTrail {
        float angle;        // 弧形角度
        ULONGLONG ts;       // 创建时间（毫秒）
        float duration;     // 动态残影持续时间
    };


    std::vector<ArcTrail> arcTrails_;  // 保存残影
};

