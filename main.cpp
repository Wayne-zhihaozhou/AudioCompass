#include "AudioCapture.h"
#include "Canvas.h"

Canvas* g_canvas = nullptr;  // 全局 Canvas 对象指针，用于绘制弧形

// 主窗口回调函数
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);  // 窗口销毁时退出消息循环
		return 0;
	default:
		return DefWindowProc(hwnd, msg, wp, lp);  // 默认消息处理
	}
}

// 程序入口
int WINAPI WinMain(
	_In_ HINSTANCE hInst,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd
) {
	// 创建透明叠加窗口 Canvas
	g_canvas = new Canvas(hInst);
	HWND hwnd = g_canvas->getHwnd();  // 获取窗口句柄

	// 初始化音频捕获
	AudioCapture ac;
	ac.setMainWindowHandle(hwnd);        // 设置主窗口句柄，用于发送消息
	ac.outputWavFile = "high_freq_audio.wav";  // 输出音频文件
	ac.start();                          // 开始捕获音频

	// 消息循环
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		if (msg.message == WM_USER + 100) {  // 自定义音频事件消息
			auto event = reinterpret_cast<AudioCapture::AudioEvent*>(msg.lParam);
			if (event->highFreq && g_canvas) {
				// 检测到高频音，绘制弧形
				g_canvas->drawArc(event->angle);
			}
			delete event;  // 释放消息数据
		}
		TranslateMessage(&msg);  // 翻译按键消息
		DispatchMessage(&msg);   // 分发消息
	}

	ac.stop();          // 停止音频捕获
	delete g_canvas;    // 释放 Canvas 对象
	return 0;
}


