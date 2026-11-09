#pragma once

#include <InterfaceDefs.h>
#include <Rect.h>

class BView;

// One waveform renderer for every audio region and for the take being
// recorded, so a take looks the same while it records and after it lands as
// a clip; only the palette differs. Callers work out the pixel columns or
// sample points and hand them in; nothing here touches the engine.

struct WavePalette {
    rgb_color body;   // item background (the region rectangle)
    rgb_color border; // item outline
    rgb_color wave;   // waveform fill / sample line
    rgb_color mid;    // zero line
    rgb_color hot;    // the part of a peak beyond full scale
};

// Green regions and the red take being recorded.
extern const WavePalette kClipWavePalette;
extern const WavePalette kRecWavePalette;

// The region rectangle under its waveform.
void WaveDrawBody(BView *view, BRect body, const WavePalette &pal);

// Vertical geometry of one channel lane: y0..y0+h, zero line in the middle.
// Full scale (|v| == 1) reaches the lane edge at vzoom 1.
struct WaveLane {
    float y0;
    float h;
    float vzoom;
    float gain;
};

// Peak mode: per-pixel (min,max) columns. Column i is drawn at screen x
// x0 + i + 0.5; mn/mx are read at index i * stride (pixels where mn > mx have
// no data and break the outline). Each column is stretched to meet its
// neighbour so steep edges stay one continuous shape.
void WaveDrawPeaks(BView *view, const WaveLane &lane, float x0, int npx, const float *mn,
                   const float *mx, int stride, const WavePalette &pal);

// Sample mode: one point per sample. Sample i (value at frames[i * stride])
// sits at screen x x_first + i * px_per_frame; the points are joined by a
// line, and once they are far enough apart each gets a handle square.
// [clip_left, clip_right] bounds the drawing horizontally.
void WaveDrawSamples(BView *view, const WaveLane &lane, const float *frames, int stride, int count,
                     double x_first, double px_per_frame, float clip_left, float clip_right,
                     const WavePalette &pal);

// Zero line across [x0, x1].
void WaveDrawMidLine(BView *view, const WaveLane &lane, float x0, float x1, const WavePalette &pal);

// Exact per-pixel (min,max) from raw samples, for zoom levels finer than the
// peak table: frames is interleaved, `channels` wide, `count` frames long and
// starts at file frame `first`; pixel x covers file frames
// [start + x * fpp, start + (x + 1) * fpp). Output layout as
// audio_clip_get_peaks (out[x * channels + c]); pixels without samples get
// min > max.
void WavePeaksFromSamples(const float *frames, int channels, long long first, long long count,
                          double start, double fpp, int npx, float *out_min, float *out_max);
