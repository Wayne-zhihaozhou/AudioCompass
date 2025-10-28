#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <complex>
#include <string>
#include <cstdint>

// 保存单帧音频数据
struct AudioFrame {
    std::vector<uint8_t> data;  // 音频原始字节数据
};

// 音频捕获、分析与保存类，使用 WASAPI Loopback 捕获系统音频
class AudioCapture {
public:
    // 高频检测参数
    float highFreqMin = 10000.0f;        // 高频起始频率阈值
    float highFreqEpsilon = 0.001f;    // 高频幅度判断阈值
    float highFreqRatio = 0.1f;      // 高频占比阈值
    std::string outputWavFile = "captured_audio.wav";  // 输出 WAV 文件名

    // 高频音事件结构
    struct AudioEvent {
        std::vector<uint8_t> data;  // 音频帧原始数据
        bool highFreq = false;       // 是否检测到高频
        float angle = 0.0f;          // 枪声方位角度 [-90, +90]
    };

    HWND mainWindowHandle = nullptr; // 主窗口句柄，用于 PostMessage

    AudioCapture();
    ~AudioCapture();

    void setMainWindowHandle(HWND hwnd);  // 设置主窗口句柄
    void start();  // 启动音频捕获与分析线程
    void stop();   // 停止音频捕获与分析线程

private:
    WAVEFORMATEX* pwfx = nullptr;  // 音频格式信息
    bool running = false;           // 捕获线程运行标志

    std::queue<AudioFrame> modelQueue;  // 待分析音频帧队列
    std::mutex modelMutex;              // 分析队列互斥锁
    std::condition_variable modelCV;    // 分析队列条件变量

    std::queue<AudioFrame> saveQueue;   // 待保存音频帧队列
    std::mutex saveMutex;               // 保存队列互斥锁
    std::condition_variable saveCV;     // 保存队列条件变量

    std::thread captureThreadHandle;    // 音频捕获线程
    std::thread modelThreadHandle;      // 高频分析线程
    std::thread saveThreadHandle;       // 音频保存线程

    void captureThread();  // 捕获音频数据线程
    void myThread();       // 分析高频与方位角线程
    void savePcmWavStreaming();  // 保存音频为 WAV 文件

    void simpleFFT(const std::vector<float>& in, std::vector<std::complex<float>>& out);  // 简单 FFT 计算
    bool hasHighFreqContent(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx); // 高频检测
    float getGunshotAngle(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx);   // 根据左右声道计算方位
};
