#include "VuView.h"

#include <GradientLinear.h>
#include <OS.h>

#include <math.h>

// dBFS range shown (matches the Linux JackDAW meter: -60 .. +6, span 66).
static const double kDbFloor = -60.0;
static const double kDbCeil = 6.0;
static const double kDbSpan = kDbCeil - kDbFloor;

// Peak hold: how long the tick stays before it starts to fall, and how fast.
static const bigtime_t kHoldTime = 1500000; // 1.5 s
static const float kHoldFall = 0.85f;       // per UI tick, once released

// Clip light height at the top of each bar, and the gap under it.
static const float kLedH = 4.0f;
static const float kLedGap = 2.0f;

// Scale colours, bottom to top.
static const rgb_color kGreen = {46, 186, 72, 255};
static const rgb_color kGreenHi = {130, 214, 60, 255};
static const rgb_color kYellow = {236, 214, 40, 255};
static const rgb_color kOrange = {246, 140, 30, 255};
static const rgb_color kRed = {236, 40, 36, 255};
static const rgb_color kWell = {26, 26, 28, 255};
static const rgb_color kWellEdge = {12, 12, 14, 255};
static const rgb_color kLedOff = {74, 24, 22, 255};
static const rgb_color kLedOn = {255, 52, 40, 255};

static double db_frac(double db)
{
    if (db < kDbFloor)
        db = kDbFloor;
    if (db > kDbCeil)
        db = kDbCeil;
    return (db - kDbFloor) / kDbSpan;
}

static double lin_frac(float v)
{
    if (v <= 0.000001f)
        return 0.0;
    return db_frac(20.0 * log10((double)v));
}

VuView::VuView(const char *name, float min_width)
    : BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE), m_min_width(min_width)
{
}

void VuView::AttachedToWindow()
{
    BView::AttachedToWindow();
    if (Parent())
        SetViewColor(Parent()->ViewColor());
    else
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    SetExplicitMinSize(BSize(m_min_width, 40.0f));
    SetExplicitMaxSize(BSize(m_min_width, B_SIZE_UNLIMITED));
}

void VuView::Feed(Channel &ch, float peak, bigtime_t now)
{
    // UI-side decay so the fall-off is smooth.
    ch.level = (peak > ch.level) ? peak : ch.level * 0.80f;
    if (ch.level < 0.00001f)
        ch.level = 0.0f;
    if (peak >= ch.hold) {
        ch.hold = peak;
        ch.hold_until = now + kHoldTime;
    } else if (now > ch.hold_until) {
        ch.hold *= kHoldFall;
        if (ch.hold < ch.level)
            ch.hold = ch.level;
    }
    if (peak >= 1.0f)
        ch.clipped = true;
}

void VuView::SetPeaks(float left, float right)
{
    bigtime_t now = system_time();
    Feed(m_ch[0], left, now);
    Feed(m_ch[1], right, now);
    if (Window())
        Invalidate();
}

void VuView::MouseDown(BPoint where)
{
    (void)where;
    m_ch[0].clipped = m_ch[1].clipped = false;
    Invalidate();
}

void VuView::DrawBar(BRect r, const Channel &ch)
{
    // Clip light.
    BRect led(r.left, r.top, r.right, r.top + kLedH - 1.0f);
    SetHighColor(ch.clipped ? kLedOn : kLedOff);
    FillRect(led);

    // Recessed well.
    BRect well(r.left, r.top + kLedH + kLedGap, r.right, r.bottom);
    SetHighColor(kWell);
    FillRect(well);
    SetHighColor(kWellEdge);
    StrokeLine(well.LeftTop(), well.RightTop());
    StrokeLine(well.LeftTop(), well.LeftBottom());

    BRect bar = well.InsetByCopy(1.0f, 1.0f);
    float h = bar.Height();
    if (h <= 0.0f)
        return;

    // The scale as one gradient over the whole bar; the lit part is the slice
    // of it below the level, so each height always shows its own colour.
    BGradientLinear g(BPoint(bar.left, bar.bottom), BPoint(bar.left, bar.top));
    g.AddColor(kGreen, 0);
    g.AddColor(kGreenHi, (uint8)(db_frac(-18.0) * 255));
    g.AddColor(kYellow, (uint8)(db_frac(-9.0) * 255));
    g.AddColor(kOrange, (uint8)(db_frac(-3.0) * 255));
    g.AddColor(kRed, (uint8)(db_frac(0.0) * 255));
    g.AddColor(kRed, 255);

    float lit = (float)(lin_frac(ch.level) * h);
    if (lit >= 1.0f)
        FillRect(BRect(bar.left, bar.bottom - lit + 1.0f, bar.right, bar.bottom), g);

    // Peak-hold tick.
    float hold = (float)(lin_frac(ch.hold) * h);
    if (hold >= 2.0f) {
        float y = bar.bottom - hold + 1.0f;
        FillRect(BRect(bar.left, y, bar.right, y + 1.0f), g);
    }

    // 0 dBFS mark across the well.
    float y0 = bar.bottom - (float)(db_frac(0.0) * h) + 1.0f;
    SetHighColor(90, 90, 96);
    StrokeLine(BPoint(bar.left, y0), BPoint(bar.right, y0));
}

void VuView::Draw(BRect updateRect)
{
    (void)updateRect;
    BRect b = Bounds();
    float mid = floorf(b.Width() / 2.0f);
    DrawBar(BRect(b.left, b.top, mid - 1.0f, b.bottom), m_ch[0]);
    DrawBar(BRect(mid + 1.0f, b.top, b.right, b.bottom), m_ch[1]);
}
