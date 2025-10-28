#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

class Canvas {
public:
    Canvas(HINSTANCE hInst);   // ���캯������ʼ�� GDI+ �ʹ���
    ~Canvas();                 // �����������ͷ���Դ

    HWND getHwnd() const { return hwnd_; } // ��ȡ���ھ��
    void drawArc(float angleDeg);          // ����ָ���ǶȵĻ��κ�����
    void clear();
    void show();                           // ��ʾ����
    void destroy();                        // ���ٴ��ں��ͷ���Դ

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp); // ���ڻص�

    // �û������ò���
    float trailAngleThreshold = 10.0f;      // ��Ӱ������ֵ���ȣ�
    float trailBaseDuration = 0.2f;// ��Ӱ��������ʱ�䣨�룩
    float trailMaxDuration = 1.0f;// ��Ӱ������ʱ�䣨�룩
    Color liveColor = Color(255, 255, 255, 0);   // ʵʱ������ɫ
    Color trailColor = Color(255, 255, 0, 0);    // ��Ӱ��ʼ��ɫ
    float arcSpan = 2.0f;                   // ���߿�ȣ��ȣ�
    Color textColor = Color(255, 255, 255, 0); // ������ɫ��Ĭ�ϰ�ɫ

private:
    void initWindow(HINSTANCE hInst);      // ��ʼ��͸�����Ӵ���

    HWND hwnd_ = nullptr;                  // ���ھ��
    bool hasContent_ = false;              // ��ǰ�Ƿ��л�������
    Bitmap* bmp_ = nullptr;                // ��ͼ Bitmap
    Graphics* g_ = nullptr;                // ��ͼ����
    Pen* pen_ = nullptr;                   // ����ʵʱ���߻���
    Font* font_ = nullptr;                 // ������������
    SolidBrush* brush_ = nullptr;          // �������ֻ�ˢ
    int cachedSize_ = 0;                   // ���� Bitmap �ߴ�
    float radius_ = 0;                     // ���߰뾶
    float penWidth_ = 0;                   // ���ʿ��

    struct ArcTrail {
        float angle;        // ���νǶ�
        ULONGLONG ts;       // ����ʱ�䣨���룩
        float duration;     // ��̬��Ӱ����ʱ��
    };


    std::vector<ArcTrail> arcTrails_;  // �����Ӱ
};

