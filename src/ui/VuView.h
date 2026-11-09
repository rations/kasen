#pragma once

#include <View.h>

// Stereo peak meter, -60 .. +6 dBFS, on a continuous green -> yellow -> red
// scale. Fed linear peak values on the UI tick via SetPeaks(); applies a little
// UI-side decay so the fall-off reads smoothly regardless of how the source is
// sampled. Each channel has a peak-hold tick (held ~1.5 s, then it falls) and a
// clip light at the top that latches at 0 dBFS until the meter is clicked.
class VuView : public BView
{
public:
    explicit VuView(const char *name, float min_width = 14.0f);

    void AttachedToWindow() override;
    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override; // clears the clip lights

    // Linear peaks (0..1+). Mono sources pass the same value for both.
    void SetPeaks(float left, float right);

private:
    struct Channel {
        float level = 0.0f; // displayed (decayed) linear peak
        float hold = 0.0f;  // peak-hold value
        bigtime_t hold_until = 0;
        bool clipped = false; // latched clip light
    };
    void Feed(Channel &ch, float peak, bigtime_t now);
    void DrawBar(BRect r, const Channel &ch);

    Channel m_ch[2];
    float m_min_width;
};
