#include "AudioCapture.h"



AudioCapture::AudioCapture() {}

AudioCapture::~AudioCapture() {
    if (pwfx) CoTaskMemFree(pwfx);
}

/**
 * 启动音频捕获与分析线程
 */
void AudioCapture::start() {
    running = true;
    captureThreadHandle = std::thread(&AudioCapture::captureThread, this);

    // 等待 pwfx 初始化
    while (pwfx == nullptr) Sleep(10);

    modelThreadHandle = std::thread(&AudioCapture::myThread, this);
    //saveThreadHandle = std::thread(&AudioCapture::savePcmWavStreaming, this);
}

/**
 * 停止捕获音频
 */
void AudioCapture::stop() {
    running = false;
    modelCV.notify_all();
    saveCV.notify_all();

    if (captureThreadHandle.joinable()) captureThreadHandle.join();
    if (modelThreadHandle.joinable()) modelThreadHandle.join();
    if (saveThreadHandle.joinable()) saveThreadHandle.join();
}

/**
 * 简单 FFT 实现
 */
void AudioCapture::simpleFFT(const std::vector<float>& in, std::vector<std::complex<float>>& out) {
    size_t N = in.size();
    out.resize(N);
    const float PI2 = 2.0f * 3.14159265359f;
    for (size_t k = 0; k < N; ++k) {
        std::complex<float> sum(0.0f, 0.0f);
        for (size_t n = 0; n < N; ++n) {
            float angle = -PI2 * k * n / N;
            sum += std::complex<float>(in[n] * std::cos(angle), in[n] * std::sin(angle));
        }
        out[k] = sum;
    }
}

/**
 * 判断是否有高频内容
 */
bool AudioCapture::hasHighFreqContent(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx) {
    std::vector<float> mono(numFrames);

    if (pwfx->wBitsPerSample == 16) {
        const int16_t* src = reinterpret_cast<const int16_t*>(pData);
        for (uint32_t i = 0; i < numFrames; ++i)
            mono[i] = src[i * pwfx->nChannels] / 32768.0f;
    }
    else if (pwfx->wBitsPerSample == 32) {
        const float* src = reinterpret_cast<const float*>(pData);
        for (uint32_t i = 0; i < numFrames; ++i)
            mono[i] = src[i * pwfx->nChannels];
    }

    std::vector<std::complex<float>> spectrum;
    simpleFFT(mono, spectrum);

    float freqStep = static_cast<float>(pwfx->nSamplesPerSec) / numFrames;
    size_t highFreqCount = 0;
    size_t aboveThresholdCount = 0;

    for (size_t i = 0; i < spectrum.size() / 2; ++i) {
        float freq = i * freqStep;
        if (freq >= highFreqMin) {
            ++highFreqCount;
            float mag = std::abs(spectrum[i]);
            if (mag > highFreqEpsilon) ++aboveThresholdCount;
        }
    }

    if (highFreqCount == 0) return false;

    float ratio = static_cast<float>(aboveThresholdCount) / highFreqCount;
    return (ratio >= highFreqRatio);
}

/**
 * 捕获线程
 */
void AudioCapture::captureThread() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return;

    IMMDeviceEnumerator* pEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnumerator));
    if (FAILED(hr)) return;

    IMMDevice* pDevice = nullptr;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) return;

    IAudioClient* pAudioClient = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(&pAudioClient));
    if (FAILED(hr)) return;

    hr = pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) return;

    hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        0, 0, pwfx, nullptr);
    if (FAILED(hr)) return;

    IAudioCaptureClient* pCaptureClient = nullptr;
    hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&pCaptureClient));
    if (FAILED(hr)) return;

    hr = pAudioClient->Start();
    if (FAILED(hr)) return;

    while (running) {
        UINT32 packetLength = 0;
        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) break;

        while (packetLength != 0) {
            BYTE* pData = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            hr = pCaptureClient->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            if (numFrames > 0 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                if (hasHighFreqContent(pData, numFrames, pwfx)) {
                    AudioFrame frame;
                    frame.data.resize(numFrames * pwfx->nBlockAlign);
                    memcpy(frame.data.data(), pData, numFrames * pwfx->nBlockAlign);

                    {
                        std::lock_guard<std::mutex> lock(modelMutex);
                        modelQueue.push(frame);
                    }
                    modelCV.notify_one();

                    {
                        std::lock_guard<std::mutex> lock(saveMutex);
                        saveQueue.push(frame);
                    }
                    saveCV.notify_one();
                }
            }

            hr = pCaptureClient->ReleaseBuffer(numFrames);
            if (FAILED(hr)) break;

            hr = pCaptureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;
        }
        Sleep(1);
    }

    pAudioClient->Stop();
    pCaptureClient->Release();
    pAudioClient->Release();
    pDevice->Release();
    pEnumerator->Release();
    CoUninitialize();
}

/**
 * 模型线程：分析高频 & 方位角
 */
void AudioCapture::myThread() {
    while (running || !modelQueue.empty()) {
        std::unique_lock<std::mutex> lock(modelMutex);
        modelCV.wait(lock, [this] { return !modelQueue.empty() || !running; });

        if (!modelQueue.empty()) {
            auto frame = std::move(modelQueue.front());
            modelQueue.pop();
            lock.unlock();

            AudioEvent event;
            event.data = frame.data;
            event.highFreq = hasHighFreqContent(event.data.data(),
                static_cast<uint32_t>(event.data.size() / pwfx->nBlockAlign),
                pwfx);
            event.angle = getGunshotAngle(event.data.data(),
                static_cast<uint32_t>(event.data.size() / pwfx->nBlockAlign),
                pwfx);

            if (event.highFreq) {
                PostMessage(mainWindowHandle, WM_USER + 100, 0, reinterpret_cast<LPARAM>(new AudioEvent(event)));
            }
        }
    }
}

void AudioCapture::setMainWindowHandle(HWND hwnd) {
    mainWindowHandle = hwnd;
}

/**
 * 根据左右声道能量计算方位
 */
float AudioCapture::getGunshotAngle(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx) {
    if (!pData || !pwfx || pwfx->nChannels < 2 || numFrames == 0) return 0.0f;

    double sumSqLeft = 0.0, sumSqRight = 0.0;
    if (pwfx->wBitsPerSample == 16) {
        const int16_t* src = reinterpret_cast<const int16_t*>(pData);
        for (uint32_t i = 0; i < numFrames; ++i) {
            float l = static_cast<float>(src[i * pwfx->nChannels + 0]) / 32768.0f;
            float r = static_cast<float>(src[i * pwfx->nChannels + 1]) / 32768.0f;
            sumSqLeft += l * l;
            sumSqRight += r * r;
        }
    }
    else if (pwfx->wBitsPerSample == 32) {
        const float* src = reinterpret_cast<const float*>(pData);
        for (uint32_t i = 0; i < numFrames; ++i) {
            float l = src[i * pwfx->nChannels + 0];
            float r = src[i * pwfx->nChannels + 1];
            sumSqLeft += l * l;
            sumSqRight += r * r;
        }
    }

    double rmsLeft = std::sqrt(sumSqLeft / numFrames);
    double rmsRight = std::sqrt(sumSqRight / numFrames);
    if (rmsLeft < 1e-6 && rmsRight < 1e-6) return 0.0f;

    double dbDiff = 20.0 * std::log10((rmsRight + 1e-9) / (rmsLeft + 1e-9));
    double maxDb = 20.0;
    double angle = (dbDiff / maxDb) * 90.0;
    angle = angle = (angle < -90.0) ? -90.0 : ((angle > 90.0) ? 90.0 : angle);
    return static_cast<float>(angle);
}



