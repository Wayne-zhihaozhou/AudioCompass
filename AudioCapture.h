#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <complex>
#include <string>

/**
 * @struct AudioFrame
 * 保存单帧音频数据
 */
struct AudioFrame {
    std::vector<uint8_t> data;
};

/**
 * @class AudioCapture
 * 音频捕获、处理和保存类，使用 WASAPI Loopback 捕获系统音频
 */
class AudioCapture {
public:
    // 用户可设置参数
    float highFreqMin = 0;// 10000.0f;       // 高频阈值
    float highFreqEpsilon = 0;// 0.001f;     // 高频幅度判断阈值
    float highFreqRatio = 0;// 0.1f;         // 高频占比阈值
    std::string outputWavFile = "captured_audio.wav"; // 输出 WAV 文件名

    struct AudioEvent {
        std::vector<uint8_t> data;   // 原始音频帧
        bool highFreq;               // 是否检测到高频
        float angle;                 // 枪声方位角度 [-90, +90]
    };

    HWND mainWindowHandle = nullptr; // 主窗口句柄，用于 PostMessage
    AudioCapture();
    ~AudioCapture();

    void setMainWindowHandle(HWND hwnd);
    void start();
    void stop();

private:
    WAVEFORMATEX* pwfx = nullptr;
    bool running = false;

    std::queue<AudioFrame> modelQueue;
    std::mutex modelMutex;
    std::condition_variable modelCV;

    std::queue<AudioFrame> saveQueue;
    std::mutex saveMutex;
    std::condition_variable saveCV;

    std::thread captureThreadHandle;
    std::thread modelThreadHandle;
    std::thread saveThreadHandle;

    void captureThread();
    void myThread();
    void savePcmWavStreaming();

    void simpleFFT(const std::vector<float>& in, std::vector<std::complex<float>>& out);
    bool hasHighFreqContent(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx);
    float getGunshotAngle(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx);
};
