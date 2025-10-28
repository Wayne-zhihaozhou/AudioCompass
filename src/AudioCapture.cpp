#include "AudioCapture.h"
#include <fstream>
#include <iostream>

AudioCapture::AudioCapture() {}

// 析构函数，释放 WAVEFORMATEX 内存
AudioCapture::~AudioCapture() {
	if (pwfx) CoTaskMemFree(pwfx);
}

// 启动音频捕获与分析线程
void AudioCapture::start() {
	running = true;
	captureThreadHandle = std::thread(&AudioCapture::captureThread, this);

	// 等待 pwfx 初始化完成
	while (pwfx == nullptr) Sleep(10);

	modelThreadHandle = std::thread(&AudioCapture::myThread, this);
	//saveThreadHandle = std::thread(&AudioCapture::savePcmWavStreaming, this);//暂时关闭
}

// 停止捕获音频
void AudioCapture::stop() {
	running = false;
	modelCV.notify_all();
	saveCV.notify_all();

	if (captureThreadHandle.joinable()) captureThreadHandle.join();
	if (modelThreadHandle.joinable()) modelThreadHandle.join();
	if (saveThreadHandle.joinable()) saveThreadHandle.join();
}

// 简单 FFT 实现
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

// 判断是否有高频内容
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

	// 遍历频谱，统计高频数量
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

// 捕获线程，循环读取音频数据
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

					// 推送到模型分析队列
					{
						std::lock_guard<std::mutex> lock(modelMutex);
						modelQueue.push(frame);
					}
					modelCV.notify_one();

					// 推送到保存队列
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

	// 清理 COM 资源
	pAudioClient->Stop();
	pCaptureClient->Release();
	pAudioClient->Release();
	pDevice->Release();
	pEnumerator->Release();
	CoUninitialize();
}

// 模型线程：分析高频 & 方位角
void AudioCapture::myThread() {
	while (running || !modelQueue.empty()) {
		AudioEvent* eventPtr = nullptr;

		{
			std::unique_lock<std::mutex> lock(modelMutex);
			// 打印队列状态
			wchar_t buf[256];
			swprintf_s(buf, 256, L"[myThread] Queue size before wait: %zu\n", modelQueue.size());
			OutputDebugStringW(buf);

			// 等待队列非空或线程停止
			modelCV.wait(lock, [this] { return !modelQueue.empty() || !running; });

			swprintf_s(buf, 256, L"[myThread] Queue size after wait: %zu\n", modelQueue.size());
			OutputDebugStringW(buf);

			if (!modelQueue.empty()) {
				auto frame = std::move(modelQueue.front());
				modelQueue.pop();

				// 创建事件对象
				eventPtr = new AudioEvent{};
				eventPtr->data = std::move(frame.data);
				eventPtr->highFreq = hasHighFreqContent(
					eventPtr->data.data(),
					static_cast<uint32_t>(eventPtr->data.size() / pwfx->nBlockAlign),
					pwfx
				);
				eventPtr->angle = getGunshotAngle(
					eventPtr->data.data(),
					static_cast<uint32_t>(eventPtr->data.size() / pwfx->nBlockAlign),
					pwfx
				);

				swprintf_s(buf, 256, L"[myThread] Event generated: highFreq=%d, angle=%.2f\n",
					eventPtr->highFreq, eventPtr->angle);
				OutputDebugStringW(buf);
			}
		} // unlock mutex

		// 队列空且线程仍在运行，发送空事件触发清屏
		if (!eventPtr && running) {
			eventPtr = new AudioEvent{};
			eventPtr->highFreq = false;
			eventPtr->angle = 0.0f;
			OutputDebugStringW(L"[myThread] Queue empty, sending clear event\n");
		}

		// 发送消息给主窗口
		if (eventPtr && mainWindowHandle) {
			PostMessage(mainWindowHandle, WM_USER + 100, 0, reinterpret_cast<LPARAM>(eventPtr));

			wchar_t buf[128];
			swprintf_s(buf, 128, L"[myThread] PostMessage sent: highFreq=%d, angle=%.2f\n",
				eventPtr->highFreq, eventPtr->angle);
			OutputDebugStringW(buf);
		}
	}

	// 循环退出前，确保最后一次清屏
	if (mainWindowHandle) {
		AudioEvent* lastEvent = new AudioEvent{};
		lastEvent->highFreq = false;
		lastEvent->angle = 0.0f;
		PostMessage(mainWindowHandle, WM_USER + 100, 0, reinterpret_cast<LPARAM>(lastEvent));

		OutputDebugStringW(L"[myThread] Thread exiting: sent final clear event\n");
	}
}



// 设置主窗口句柄
void AudioCapture::setMainWindowHandle(HWND hwnd) {
	mainWindowHandle = hwnd;
}

// 根据左右声道能量计算方位
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
	angle = (angle < -90.0) ? -90.0 : ((angle > 90.0) ? 90.0 : angle);
	return static_cast<float>(angle);
}

// 保存 WAV 文件（流式写入）
void AudioCapture::savePcmWavStreaming() {
	if (!pwfx) return;

	std::ofstream ofs(outputWavFile, std::ios::binary);
	if (!ofs.is_open()) return;

	uint16_t wFormatTag = pwfx->wFormatTag;
	if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		WAVEFORMATEXTENSIBLE* pExt = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
		if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) wFormatTag = WAVE_FORMAT_PCM;
		else if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		else return;
	}

	uint16_t blockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
	uint32_t avgBytesPerSec = blockAlign * pwfx->nSamplesPerSec;

	uint32_t fmtChunkSize = 16;
	uint32_t dataSize = 0;
	uint32_t riffChunkSize = 4 + (8 + fmtChunkSize) + (8 + dataSize);

	// 写 WAV 头
	ofs.write("RIFF", 4);
	ofs.write(reinterpret_cast<const char*>(&riffChunkSize), 4);
	ofs.write("WAVE", 4);

	ofs.write("fmt ", 4);
	ofs.write(reinterpret_cast<const char*>(&fmtChunkSize), 4);
	ofs.write(reinterpret_cast<const char*>(&wFormatTag), 2);
	ofs.write(reinterpret_cast<const char*>(&pwfx->nChannels), 2);
	ofs.write(reinterpret_cast<const char*>(&pwfx->nSamplesPerSec), 4);
	ofs.write(reinterpret_cast<const char*>(&avgBytesPerSec), 4);
	ofs.write(reinterpret_cast<const char*>(&blockAlign), 2);
	ofs.write(reinterpret_cast<const char*>(&pwfx->wBitsPerSample), 2);

	ofs.write("data", 4);
	ofs.write(reinterpret_cast<const char*>(&dataSize), 4);

	uint32_t totalDataSize = 0;

	// 循环写入 PCM 数据
	while (running || !saveQueue.empty()) {
		std::unique_lock<std::mutex> lock(saveMutex);
		saveCV.wait(lock, [this] { return !saveQueue.empty() || !running; });

		if (!saveQueue.empty()) {
			auto frame = std::move(saveQueue.front());
			saveQueue.pop();
			lock.unlock();

			ofs.write(reinterpret_cast<const char*>(frame.data.data()), frame.data.size());
			totalDataSize += static_cast<uint32_t>(frame.data.size());
		}
	}

	// 更新 WAV 文件头长度
	ofs.seekp(4, std::ios::beg);
	uint32_t riffSize = 4 + (8 + fmtChunkSize) + (8 + totalDataSize);
	ofs.write(reinterpret_cast<const char*>(&riffSize), 4);

	ofs.seekp(40, std::ios::beg);
	ofs.write(reinterpret_cast<const char*>(&totalDataSize), 4);

	ofs.close();
	std::cout << "Saved WAV file: " << outputWavFile << " (streaming mode)" << std::endl;
}


