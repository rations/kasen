#include "FaderView.h"

#include <GradientLinear.h>
#include <Message.h>
#include <Window.h>

#include <math.h>
#include <stdio.h>

// dB taper: -42 .. +12 dB across the travel.
static const double kDbMin = -42.0;
static const double kDbMax = 12.0;
static const double kDbSpan = 54.0;

// Layout: a slim slider column on the left, the dB scale to its right.
static const float kFaderW = 20.0f;    // slider column width
static const float kScaleW = 24.0f;    // dB label column width
static const float kSliderHalf = 7.0f; // half the handle cap height
static const float kMinHeight = 96.0f;

static double pos_to_db(double p)
{
    if (p < 0.0)
        p = 0.0;
    if (p > 1.0)
        p = 1.0;
    return kDbMin + p * kDbSpan;
}

static double db_to_pos(double db)
{
    if (db < kDbMin)
        db = kDbMin;
    if (db > kDbMax)
        db = kDbMax;
    return (db - kDbMin) / kDbSpan;
}

FaderView::FaderView(const char *name, float gain, BMessage *message)
    : BView(name, B_WILL_DRAW | B_FRAME_EVENTS), BInvoker(message, NULL), m_pos(0.0),
      m_dragging(false)
{
    SetGain(gain);
}

void FaderView::AttachedToWindow()
{
    BView::AttachedToWindow();
    if (Parent())
        SetViewColor(Parent()->ViewColor());
    else
        SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    SetExplicitMinSize(BSize(kFaderW + kScaleW, kMinHeight));
    SetExplicitMaxSize(BSize(kFaderW + kScaleW, B_SIZE_UNLIMITED));
}

void FaderView::SetGain(float gain)
{
    double db = (gain > 0.0001f) ? 20.0 * log10((double)gain) : kDbMin;
    double pos = db_to_pos(db);
    if (pos == m_pos)
        return;
    m_pos = pos;
    if (Window())
        Invalidate();
}

double FaderView::YToPos(float y) const
{
    float usable = Bounds().Height() - 2.0f * kSliderHalf;
    if (usable < 1.0f)
        usable = Bounds().Height();
    double pos = 1.0 - (double)(y - kSliderHalf) / (double)usable;
    if (pos < 0.0)
        pos = 0.0;
    if (pos > 1.0)
        pos = 1.0;
    return pos;
}

float FaderView::PosToY(double pos) const
{
    float usable = Bounds().Height() - 2.0f * kSliderHalf;
    if (usable < 1.0f)
        usable = Bounds().Height();
    return kSliderHalf + (float)((1.0 - pos) * usable);
}

void FaderView::SetPosNotify(double pos)
{
    if (pos < 0.0)
        pos = 0.0;
    if (pos > 1.0)
        pos = 1.0;
    if (pos == m_pos)
        return;
    m_pos = pos;
    Invalidate();
    double db = pos_to_db(m_pos);
    float gain = (float)pow(10.0, db / 20.0);
    BMessage out(*Message());
    out.AddFloat("gain", gain);
    Invoke(&out);
}

void FaderView::Draw(BRect updateRect)
{
    (void)updateRect;
    BRect b = Bounds();
    float tx = floorf(kFaderW / 2.0f);
    float top = kSliderHalf;
    float bottom = b.Height() - kSliderHalf;

    // dB ticks either side of the slot, labels in the scale column. 0 dB is
    // longer and brighter: it is where a fader rests.
    SetFontSize(9.0f);
    font_height fh;
    GetFontHeight(&fh);
    rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
    rgb_color tick = tint_color(panel, B_DARKEN_3_TINT);
    rgb_color lbl = ui_color(B_PANEL_TEXT_COLOR);
    for (int d = (int)kDbMax; d >= (int)kDbMin; d -= 6) {
        float y = floorf(PosToY(db_to_pos((double)d))) + 0.5f;
        bool unity = d == 0;
        SetHighColor(unity ? tint_color(panel, B_DARKEN_4_TINT) : tick);
        float len = unity ? 6.0f : 4.0f;
        StrokeLine(BPoint(tx - 4.0f - len, y), BPoint(tx - 5.0f, y));
        StrokeLine(BPoint(tx + 5.0f, y), BPoint(tx + 4.0f + len, y));
        char m[8];
        snprintf(m, sizeof(m), d > 0 ? "+%d" : "%d", d);
        SetHighColor(lbl);
        float tw = StringWidth(m);
        DrawString(m, BPoint(kFaderW + kScaleW - 2.0f - tw, y + (fh.ascent - fh.descent) / 2.0f));
    }
    SetFontSize(be_plain_font->Size());

    // Recessed slot: dark well, shadow on the top/left edges, light on the
    // bottom/right.
    BRect slot(tx - 3.0f, top, tx + 3.0f, bottom);
    SetHighColor(24, 24, 26);
    FillRect(slot);
    SetHighColor(8, 8, 10);
    StrokeLine(slot.LeftTop(), slot.RightTop());
    StrokeLine(slot.LeftTop(), slot.LeftBottom());
    SetHighColor(tint_color(panel, B_LIGHTEN_2_TINT));
    StrokeLine(BPoint(slot.left, slot.bottom + 1.0f),
               BPoint(slot.right + 1.0f, slot.bottom + 1.0f));
    StrokeLine(BPoint(slot.right + 1.0f, slot.top), BPoint(slot.right + 1.0f, slot.bottom + 1.0f));

    // Level fill from the cap down.
    float hy = floorf(PosToY(m_pos)) + 0.5f;
    BRect fill(slot.left + 1.0f, hy, slot.right - 1.0f, slot.bottom - 1.0f);
    if (fill.IsValid()) {
        BGradientLinear g(BPoint(fill.left, 0), BPoint(fill.right, 0));
        g.AddColor((rgb_color){60, 150, 90, 255}, 0);
        g.AddColor((rgb_color){110, 200, 130, 255}, 128);
        g.AddColor((rgb_color){50, 120, 76, 255}, 255);
        FillRect(fill, g);
    }

    // Cap: a console fader knob, lit from above, with a grip line across the
    // middle; a soft shadow under it.
    BRect cap(2.0f, hy - kSliderHalf, kFaderW - 2.0f, hy + kSliderHalf);
    SetDrawingMode(B_OP_ALPHA);
    SetHighColor(0, 0, 0, 60);
    FillRoundRect(cap.OffsetByCopy(1.0f, 2.0f), 2.5f, 2.5f);
    SetDrawingMode(B_OP_COPY);
    BGradientLinear cg(BPoint(0, cap.top), BPoint(0, cap.bottom));
    cg.AddColor((rgb_color){250, 250, 252, 255}, 0);
    cg.AddColor((rgb_color){206, 206, 212, 255}, 110);
    cg.AddColor((rgb_color){170, 170, 178, 255}, 140);
    cg.AddColor((rgb_color){214, 214, 220, 255}, 255);
    FillRoundRect(cap, 2.5f, 2.5f, cg);
    SetHighColor(84, 84, 92);
    StrokeRoundRect(cap, 2.5f, 2.5f);
    SetHighColor(40, 40, 44);
    StrokeLine(BPoint(cap.left + 2.0f, hy), BPoint(cap.right - 2.0f, hy));
    SetHighColor(255, 255, 255);
    StrokeLine(BPoint(cap.left + 2.0f, hy + 1.0f), BPoint(cap.right - 2.0f, hy + 1.0f));
}

void FaderView::MouseDown(BPoint where)
{
    int32 buttons = 0;
    int32 clicks = 0;
    BMessage *msg = Window() ? Window()->CurrentMessage() : NULL;
    if (msg) {
        msg->FindInt32("buttons", &buttons);
        msg->FindInt32("clicks", &clicks);
    }
    if (buttons & (B_SECONDARY_MOUSE_BUTTON | B_TERTIARY_MOUSE_BUTTON))
        return;
    if (clicks >= 2) {
        m_dragging = false;
        SetPosNotify(db_to_pos(0.0)); // double-click -> unity
        return;
    }
    m_dragging = true;
    SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
    SetPosNotify(YToPos(where.y));
}

void FaderView::MouseUp(BPoint where)
{
    (void)where;
    m_dragging = false;
}

void FaderView::MouseMoved(BPoint where, uint32 code, const BMessage *dragMessage)
{
    (void)code;
    (void)dragMessage;
    if (m_dragging)
        SetPosNotify(YToPos(where.y));
}

void FaderView::MessageReceived(BMessage *message)
{
    if (message->what == B_MOUSE_WHEEL_CHANGED) {
        float dy = 0.0f;
        if (message->FindFloat("be:wheel_delta_y", &dy) == B_OK && dy != 0.0f) {
            double step = 1.0 / kDbSpan; // ~1 dB per notch
            SetPosNotify(m_pos - (dy > 0 ? step : -step));
        }
        return;
    }
    BView::MessageReceived(message);
}
