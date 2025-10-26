// overlay.h
#pragma once

#include <Windows.h>
#include <atomic>

/**
 * OverlayWindow
 *
 * ȫ��͸�����Ӵ����ࡣ�ô���ʹ�÷ֲ㴰�ڣ�UpdateLayeredWindow��������Ϊ WS_EX_TRANSPARENT��
 * ��˲�����������¼���click-through�����ڴ������Ļ��ƿ��� SetAngle ���ƵĻ��Ρ�
 *
 * ��Ҫְ��:
 *  - ����/���ٴ���
 *  - �ں�̨��Ⱦ�߳��а�����Ʋ��ύ���ֲ㴰�ڣ�ÿ���� alpha��
 *  - �ṩ�̰߳�ȫ�� SetAngle �ӿ��Ըı仡�γ���
 *
 * ע��:
 *  - ���ڲ�ʹ�� std::thread ������Ⱦѭ��������ʱȷ��ֹͣ�� join �߳�
 *  - angleDeg Ϊ 0-360 �ȣ�0�ȱ�ʾ��Ļ���Ϸ����ɰ���ı䣩
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
    void RenderLoop();            // ��Ⱦ�߳���ѭ��
    bool CreateOverlayWindow();   // ���ڴ���
    void DestroyOverlayWindow();  // ��������
    void DoDraw();                // ִ��һ�λ��Ʋ� UpdateLayeredWindow
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};
