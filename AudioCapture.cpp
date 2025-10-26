#include "AudioCapture.h"
#include <fstream>
#include <cmath>
#include <windows.h>
#include <gdiplus.h>
#include <cmath>
#include "AudioCapture.h"
#pragma comment(lib, "gdiplus.lib")


AudioCapture::AudioCapture() {}

AudioCapture::~AudioCapture() {
	if (pwfx) CoTaskMemFree(pwfx);
}

/**
 * 开始捕获音频
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
	if (ratio >= highFreqRatio) {
		//std::cout << "检测到高频声音，" << ratio * 100 << "% 的频率 > " << highFreqMin
		//    << " Hz 幅度大于 " << highFreqEpsilon << std::endl;
		return true;
	}
	return false;
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
 * 根据左右声道能量计算枪声方位
 *
 * 参数:
 * - pData: 音频缓冲指针
 * - numFrames: 帧数
 * - pwfx: 音频格式
 *
 * 返回值:
 * - float: -90 左，+90 右，0 居中
 */
float AudioCapture::getGunshotAngle(const uint8_t* pData, uint32_t numFrames, WAVEFORMATEX* pwfx) {
	if (!pData || !pwfx || pwfx->nChannels < 2 || numFrames == 0) return 0.0f;

	double sumSqLeft = 0.0;
	double sumSqRight = 0.0;

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
	else {
		return 0.0f;
	}

	double rmsLeft = std::sqrt(sumSqLeft / numFrames);
	double rmsRight = std::sqrt(sumSqRight / numFrames);

	if (rmsLeft < 1e-6 && rmsRight < 1e-6) return 0.0f; // 静音

	double dbDiff = 20.0 * std::log10((rmsRight + 1e-9) / (rmsLeft + 1e-9));
	double maxDb = 20.0;
	double angle = (dbDiff / maxDb) * 90.0;

	if (angle > 90.0) angle = 90.0;
	else if (angle < -90.0) angle = -90.0;

	return static_cast<float>(angle);
}

using namespace Gdiplus;

// 全局窗口句柄（只创建一次）
static HWND g_hwnd = nullptr;

// 窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	if (msg == WM_DESTROY) PostQuitMessage(0);
	return DefWindowProc(hwnd, msg, wp, lp);
}

/**
 * @brief 在屏幕中心绘制一个指定角度的弧形线段。
 *
 * @param angleDeg 角度（0度为屏幕正上方，顺时针增加）。
 */
void AudioCapture::DrawOverlayArc(float angleDeg) {
	static bool gdiInit = false;
	static ULONG_PTR gtoken;
	if (!gdiInit) {
		GdiplusStartupInput gi;
		GdiplusStartup(&gtoken, &gi, NULL);
		gdiInit = true;
	}

	if (!g_hwnd) {
		HINSTANCE hInst = GetModuleHandle(NULL);
		WNDCLASS wc = { 0 };
		wc.lpfnWndProc = WndProc;
		wc.hInstance = hInst;
		wc.lpszClassName = L"ArcOverlay";
		RegisterClass(&wc);

		int w = GetSystemMetrics(SM_CXSCREEN);
		int h = GetSystemMetrics(SM_CYSCREEN);

		g_hwnd = CreateWindowEx(
			WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
			wc.lpszClassName, L"ArcOverlayWnd",
			WS_POPUP, 0, 0, w, h,
			0, 0, hInst, 0);

		ShowWindow(g_hwnd, SW_SHOW);
	}

	int w = GetSystemMetrics(SM_CXSCREEN);
	int h = GetSystemMetrics(SM_CYSCREEN);

	Bitmap bmp(w, h, PixelFormat32bppPARGB);
	Graphics g(&bmp);
	g.SetSmoothingMode(SmoothingModeAntiAlias);
	g.Clear(Color(0, 0, 0, 0));

	const float arcSpan = 5.0f;
	const float radius = (float)min(w, h) * 0.25f;
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;
	const float penWidth = max(2.0f, radius * 0.02f);

	float gdiCenterAngle = 270.0f + angleDeg;
	float startAngle = gdiCenterAngle - arcSpan / 2.f;

	Pen pen(Color(255, 255, 0, 0), penWidth);
	pen.SetLineJoin(LineJoinRound);
	pen.SetStartCap(LineCapRound);
	pen.SetEndCap(LineCapRound);

	RectF rect(cx - radius, cy - radius, radius * 2, radius * 2);
	g.DrawArc(&pen, rect, startAngle, arcSpan);

	// 绘制角度文字
	FontFamily fontFamily(L"Arial");
	Font font(&fontFamily, radius * 0.1f, FontStyleBold, UnitPixel);
	SolidBrush brush(Color(255, 255, 255, 0));  // 白色文字
	std::wstring angleText = L"Angle: " + std::to_wstring(static_cast<int>(angleDeg)) + L"°";
	PointF textPos(cx - radius * 0.2f, cy - radius - 30);
	g.DrawString(angleText.c_str(), -1, &font, textPos, &brush);

	HBITMAP hb;
	bmp.GetHBITMAP(Color(0, 0, 0, 0), &hb);
	HDC sdc = GetDC(0);
	HDC mdc = CreateCompatibleDC(sdc);
	SelectObject(mdc, hb);

	SIZE sz = { w, h };
	POINT ptSrc = { 0, 0 };
	POINT ptDst = { 0, 0 };
	BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	UpdateLayeredWindow(g_hwnd, sdc, &ptDst, &sz, mdc, &ptSrc, 0, &bf, ULW_ALPHA);

	DeleteDC(mdc);
	ReleaseDC(0, sdc);
	DeleteObject(hb);
}


/**
 * 模型线程
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

			// 把事件发给主线程（例如 PostMessage 或 自定义队列）
			if (event.highFreq) {
				PostMessage(mainWindowHandle, WM_USER + 100, 0, reinterpret_cast<LPARAM>(new AudioEvent(event)));
			}
			//Sleep(1);//延迟
		}
	}
}

void AudioCapture::setMainWindowHandle(HWND hwnd) {
	mainWindowHandle = hwnd;
}

/**
 * 保存 WAV 文件（流式）
 */
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

	ofs.seekp(4, std::ios::beg);
	uint32_t riffSize = 4 + (8 + fmtChunkSize) + (8 + totalDataSize);
	ofs.write(reinterpret_cast<const char*>(&riffSize), 4);

	ofs.seekp(40, std::ios::beg);
	ofs.write(reinterpret_cast<const char*>(&totalDataSize), 4);

	ofs.close();
	std::cout << "Saved WAV file: " << outputWavFile << " (streaming mode)" << std::endl;
}
