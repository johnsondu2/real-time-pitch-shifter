#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef SMB_PITCH_SHIFT_HPP
#define SMB_PITCH_SHIFT_HPP

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#endif

#define MAX_FRAME_LENGTH 8192

void smbFft(float* fftBuffer, long fftFrameSize, long sign);
double smbAtan2(double x, double y);

void smbPitchShift(float pitchShift, long numSampsToProcess, long fftFrameSize,
                   long osamp, float sampleRate, float* indata, float* outdata);
