// overlay.h
#pragma once

#include <Windows.h>
#include <atomic>

/**
 * OverlayWindow
 *
 * 全屏透明叠加窗口类。该窗口使用分层窗口（UpdateLayeredWindow）并设置为 WS_EX_TRANSPARENT，
 * 因此不会拦截鼠标事件（click-through）。在窗口中心绘制可由 SetAngle 控制的弧段。
 *
 * 主要职责:
 *  - 创建/销毁窗口
 *  - 在后台渲染线程中按需绘制并提交到分层窗口（每像素 alpha）
 *  - 提供线程安全的 SetAngle 接口以改变弧段朝向
 *
 * 注意:
 *  - 类内部使用 std::thread 控制渲染循环，析构时确保停止并 join 线程
 *  - angleDeg 为 0-360 度，0度表示屏幕正上方（可按需改变）
 */
class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    /**
     * Initialize and show the overlay.
     *
     * @returns true on success, false on failure.
     */
    bool Initialize(HINSTANCE hInstance);

    /**
     * Stop rendering and destroy window.
     */
    void Shutdown();

    /**
     * Set the current angle (degrees) that controls arc orientation.
     *
     * @param angleDeg Angle in degrees (0..360). Values outside will be normalized.
     */
    void SetAngle(float angleDeg);

    /**
     * Request a one-time redraw (thread-safe).
     */
    void Invalidate();

private:
    // No copying
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    HWND hwnd_;
    std::atomic<bool> running_;
    std::atomic<float> angleDeg_;
    std::atomic<bool> needRedraw_;

    HINSTANCE hInstance_;
    void RenderLoop();            // 渲染线程主循环
    bool CreateOverlayWindow();   // 窗口创建
    void DestroyOverlayWindow();  // 窗口销毁
    void DoDraw();                // 执行一次绘制并 UpdateLayeredWindow
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
