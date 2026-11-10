#include "WaveDraw.h"

#include <View.h>

#include <math.h>

const WavePalette kClipWavePalette = {
    {30, 48, 34, 255},   // body
    {66, 108, 72, 255},  // border
    {96, 200, 112, 255}, // wave
    {58, 92, 63, 255},   // mid
    {245, 205, 90, 255}, // hot
};

const WavePalette kRecWavePalette = {
    {58, 28, 26, 255},    // body
    {124, 54, 48, 255},   // border
    {236, 100, 86, 255},  // wave
    {104, 46, 42, 255},   // mid
    {255, 222, 130, 255}, // hot
};

// BView::BeginLineArray needs a count > 0 and drops lines past it, and the
// whole array goes to app_server in one message: draw in bounded batches.
static const int kLineBatch = 1024;

// Peaks at or past this are drawn as clipping.
static const float kClipLevel = 0.999f;

// Sample handles appear once samples are this many pixels apart.
static const double kHandleSpacing = 6.0;

void WaveDrawBody(BView *view, BRect body, const WavePalette &pal)
{
    if (!body.IsValid())
        return;
    view->SetHighColor(pal.body);
    view->FillRect(body);
    view->SetHighColor(pal.border);
    view->StrokeRect(body);
}

void WaveDrawMidLine(BView *view, const WaveLane &lane, float x0, float x1, const WavePalette &pal)
{
    if (x1 < x0)
        return;
    float mid = floorf(lane.y0 + lane.h / 2.0f);
    view->SetHighColor(pal.mid);
    view->StrokeLine(BPoint(x0, mid), BPoint(x1, mid));
}

static inline float LaneY(const WaveLane &lane, float v)
{
    float half = lane.h / 2.0f;
    float y = lane.y0 + half - v * lane.gain * lane.vzoom * half;
    if (y < lane.y0)
        y = lane.y0;
    if (y > lane.y0 + lane.h)
        y = lane.y0 + lane.h;
    return y;
}

void WaveDrawPeaks(BView *view, const WaveLane &lane, float x0, int npx, const float *mn,
                   const float *mx, int stride, const WavePalette &pal)
{
    if (npx <= 0)
        return;
    bool have_prev = false;
    float prev_lo = 0.0f, prev_hi = 0.0f;
    int i = 0;
    while (i < npx) {
        int batch = npx - i < kLineBatch ? npx - i : kLineBatch;
        // Up to three lines per column: the body and a clip mark at each end.
        view->BeginLineArray(batch * 3);
        for (int end = i + batch; i < end; i++) {
            float lo = mn[i * stride];
            float hi = mx[i * stride];
            if (lo > hi) {
                have_prev = false;
                continue;
            }
            float own_lo = lo, own_hi = hi;
            // Meet the neighbour so a steep edge has no gap between columns.
            if (have_prev) {
                if (lo > prev_hi)
                    lo = prev_hi;
                if (hi < prev_lo)
                    hi = prev_lo;
            }
            prev_lo = own_lo;
            prev_hi = own_hi;
            have_prev = true;

            float sx = x0 + (float)i + 0.5f;
            float y_top = LaneY(lane, hi);
            float y_bot = LaneY(lane, lo);
            view->AddLine(BPoint(sx, y_top), BPoint(sx, y_bot), pal.wave);
            if (own_hi * lane.gain >= kClipLevel)
                view->AddLine(BPoint(sx, y_top), BPoint(sx, y_top + 2.0f), pal.hot);
            if (own_lo * lane.gain <= -kClipLevel)
                view->AddLine(BPoint(sx, y_bot - 2.0f), BPoint(sx, y_bot), pal.hot);
        }
        view->EndLineArray();
    }
}

void WaveDrawSamples(BView *view, const WaveLane &lane, const float *frames, int stride, int count,
                     double x_first, double px_per_frame, float clip_left, float clip_right,
                     const WavePalette &pal)
{
    if (count <= 0 || px_per_frame <= 0.0)
        return;

    // Line segments joining consecutive samples, clipped to the visible span.
    int i = 0;
    while (i + 1 < count) {
        int batch = count - 1 - i < kLineBatch ? count - 1 - i : kLineBatch;
        view->BeginLineArray(batch);
        for (int end = i + batch; i < end; i++) {
            float xa = (float)(x_first + (double)i * px_per_frame);
            float xb = (float)(x_first + (double)(i + 1) * px_per_frame);
            if (xb < clip_left || xa > clip_right)
                continue;
            float ya = LaneY(lane, frames[i * stride]);
            float yb = LaneY(lane, frames[(i + 1) * stride]);
            view->AddLine(BPoint(xa, ya), BPoint(xb, yb), pal.wave);
        }
        view->EndLineArray();
    }

    if (px_per_frame < kHandleSpacing)
        return;
    view->SetHighColor(pal.wave);
    for (int k = 0; k < count; k++) {
        float x = (float)(x_first + (double)k * px_per_frame);
        if (x < clip_left - 2.0f || x > clip_right + 2.0f)
            continue;
        float y = LaneY(lane, frames[k * stride]);
        view->FillRect(BRect(x - 1.5f, y - 1.5f, x + 1.5f, y + 1.5f));
    }
}

void WavePeaksFromSamples(const float *frames, int channels, long long first, long long count,
                          double start, double fpp, int npx, float *out_min, float *out_max)
{
    for (int x = 0; x < npx; x++) {
        long long s0 = (long long)floor(start + (double)x * fpp);
        long long s1 = (long long)floor(start + (double)(x + 1) * fpp);
        if (s1 <= s0)
            s1 = s0 + 1;
        if (s0 < first)
            s0 = first;
        if (s1 > first + count)
            s1 = first + count;
        for (int c = 0; c < channels; c++) {
            float mn = 1.0f, mx = -1.0f;
            for (long long s = s0; s < s1; s++) {
                float v = frames[(s - first) * channels + c];
                if (v < mn)
                    mn = v;
                if (v > mx)
                    mx = v;
            }
            out_min[x * channels + c] = mn;
            out_max[x * channels + c] = mx;
        }
    }
}
