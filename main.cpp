#include "AudioCapture.h"
#include <iostream>


int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    AudioCapture ac;
    ac.highFreqMin = 10000.0f;        // 用户可修改参数
    ac.highFreqEpsilon = 0.001f;
    ac.highFreqRatio = 0.1f;
    ac.outputWavFile = "high_freq_audio.wav";

    ac.start();

    std::cout << "Press Enter to stop..." << std::endl;
    std::cin.get();

    ac.stop();


}



