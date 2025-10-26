
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
 * ���浥֡��Ƶ����
 */
struct AudioFrame {
    std::vector<uint8_t> data;
};

/**
 * @class AudioCapture
 * ��Ƶ���񡢴���ͱ����࣬ʹ�� WASAPI Loopback ����ϵͳ��Ƶ
 */
class AudioCapture {
public:
    // �û������ò���
    float highFreqMin = 10000.0f;       // ��Ƶ��ֵ
    float highFreqEpsilon = 0.001f;     // ��Ƶ�����ж���ֵ
    float highFreqRatio = 0.1f;         // ��Ƶռ����ֵ
    std::string outputWavFile = "captured_audio.wav"; // ��� WAV �ļ���

    struct AudioEvent {
        std::vector<uint8_t> data;   // ԭʼ��Ƶ֡
        bool highFreq;               // �Ƿ��⵽��Ƶ
        float angle;                 // ǹ����λ�Ƕ� [-90, +90]
    };

    AudioCapture();
    ~AudioCapture();

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
    float getGunshotAngle(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx);
    void modelThread();
    void savePcmWavStreaming();

    void simpleFFT(const std::vector<float>& in, std::vector<std::complex<float>>& out);
    bool hasHighFreqContent(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx);
};
