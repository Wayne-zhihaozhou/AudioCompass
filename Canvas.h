#pragma once
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

class Canvas {
public:
    Canvas(HINSTANCE hInst);   // ���캯������ʼ�� GDI+ �ʹ���
    ~Canvas();                 // �����������ͷ���Դ

    HWND getHwnd() const { return hwnd_; } // ��ȡ���ھ��
    void drawArc(float angleDeg);          // ����ָ���ǶȵĻ��κ�����
    void show();                           // ��ʾ����
    void destroy();                        // ���ٴ��ں��ͷ���Դ

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp); // ���ڻص�

private:
    void initWindow(HINSTANCE hInst);      // ��ʼ��͸�����Ӵ���

    HWND hwnd_ = nullptr;                  // ���ھ��
    Bitmap* bmp_ = nullptr;                // ��ͼ Bitmap
    Graphics* g_ = nullptr;                // ��ͼ����
    Pen* pen_ = nullptr;                   // ���ƻ��߻���
    Font* font_ = nullptr;                 // ������������
    SolidBrush* brush_ = nullptr;          // �������ֻ�ˢ
    int cachedSize_ = 0;                   // ���� Bitmap �ߴ�
    float radius_ = 0;                     // ���߰뾶
    float penWidth_ = 0;                   // ���ʿ��
};
