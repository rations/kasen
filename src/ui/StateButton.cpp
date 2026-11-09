#include "StateButton.h"

#include <ControlLook.h>
#include <GradientLinear.h>
#include <Shape.h>

#include <math.h>

StateButton::StateButton(const char *name, const char *label, BMessage *message)
    : BButton(name, label, message), m_active_base((rgb_color){60, 150, 230, 255}),
      m_active_text((rgb_color){255, 255, 255, 255}), m_has_active_color(false),
      m_forced_active(false), m_ignore_value(false), m_glyph(GLYPH_LABEL), m_rec_recording(false),
      m_rec_punch(false)
{
}

StateButton *StateButton::MakeGlyphButton(const char *name, GlyphKind kind, BMessage *message,
                                          const char *tooltip)
{
    // BButton sizes its height from the label, and an empty label collapses
    // it: give it one letter for layout. Glyph buttons never draw the label.
    StateButton *b = new StateButton(name, "M", message);
    b->SetGlyph(kind);
    if (tooltip)
        b->SetToolTip(tooltip);
    // Wide enough for the widest glyph (two triangles or the loop arrows)
    // with the button's own padding; height follows the font as for any
    // BButton.
    float w = ceilf(b->GlyphHeight() * 3.4f);
    b->SetExplicitMinSize(BSize(w, B_SIZE_UNSET));
    b->SetExplicitMaxSize(BSize(w, B_SIZE_UNSET));
    return b;
}

void StateButton::SetActiveColor(rgb_color base, rgb_color text)
{
    m_active_base = base;
    m_active_text = text;
    m_has_active_color = true;
    Invalidate();
}

void StateButton::SetForcedActive(bool active)
{
    if (m_forced_active == active)
        return;
    m_forced_active = active;
    Invalidate();
}

void StateButton::SetActiveIgnoresValue(bool ignore)
{
    m_ignore_value = ignore;
    Invalidate();
}

void StateButton::SetGlyph(GlyphKind kind)
{
    if (m_glyph == kind)
        return;
    m_glyph = kind;
    Invalidate();
}

void StateButton::SetRecordState(bool recording, bool punch)
{
    if (m_rec_recording == recording && m_rec_punch == punch)
        return;
    m_rec_recording = recording;
    m_rec_punch = punch;
    Invalidate();
}

bool StateButton::IsActiveState() const
{
    if (m_forced_active)
        return true;
    if (m_ignore_value)
        return false;
    return Value() == B_CONTROL_ON;
}

// Glyph height in pixels: odd, so a centred shape has a middle pixel row.
float StateButton::GlyphHeight() const
{
    return (float)((int)(be_plain_font->Size() * 0.8f) | 1);
}

// Faithful copy of BButton::Draw (native frame + background + label via
// be_control_look), substituting the base/text colours while active.
void StateButton::Draw(BRect updateRect)
{
    BRect rect(Bounds());
    rgb_color background = ViewColor();

    const bool active = IsActiveState() && m_has_active_color;
    rgb_color base = active ? m_active_base : ui_color(B_CONTROL_BACKGROUND_COLOR);
    rgb_color text = active ? m_active_text : ui_color(B_CONTROL_TEXT_COLOR);

    uint32 flags = be_control_look->Flags(this);

    be_control_look->DrawButtonFrame(this, rect, updateRect, base, background, flags);
    be_control_look->DrawButtonBackground(this, rect, updateRect, base, flags);

    if (m_glyph == GLYPH_LABEL) {
        be_control_look->DrawLabel(this, Label(), rect, updateRect, base, flags,
                                   BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE), &text);
        return;
    }

    // Glyph colour: the active text colour when lit, else a dark tint of the
    // button (as MediaPlayer's symbols), lighter when disabled.
    rgb_color glyph;
    if (active)
        glyph = text;
    else if (!IsEnabled())
        glyph = tint_color(base, B_DARKEN_2_TINT);
    else
        glyph = tint_color(base, B_DARKEN_4_TINT);

    switch (m_glyph) {
        case GLYPH_LOOP:
            DrawLoopGlyph(rect, glyph);
            break;
        case GLYPH_RECORD:
            DrawRecordGlyph(rect, glyph);
            break;
        case GLYPH_MONO:
        case GLYPH_STEREO:
            DrawRingsGlyph(rect, glyph, m_glyph == GLYPH_STEREO);
            break;
        default:
            DrawShapeGlyph(rect, glyph);
            break;
    }
}

// Vertical gradient for a glyph spanning y0..y1: a shade darker at the top,
// the colour itself at the bottom (MediaPlayer's SymbolButton look).
static BGradientLinear GlyphGradient(rgb_color color, float y0, float y1, float x)
{
    BGradientLinear g;
    g.AddColor(tint_color(color, B_DARKEN_1_TINT), 0);
    g.AddColor(color, 255);
    g.SetStart(BPoint(x, y0));
    g.SetEnd(BPoint(x, y1));
    return g;
}

/* The transport symbols as filled shapes in a box of height h with the origin
 * top-left. Proportions follow Haiku MediaPlayer's TransportControlGroup
 * (_CreatePlayShape, _CreatePauseShape, _CreateStopShape,
 * _CreateSkipBackwardsShape, _CreateRewindShape, ...; Copyright 2006-2010
 * Stephan Aßmus, MIT licence). */
BShape *StateButton::CreateGlyphShape(GlyphKind kind, float h) const
{
    BShape *s = new BShape();
    float bar = ceilf(h / 6.0f);  // width of a skip bar
    float tri = floorf(h * 0.9f); // width of one triangle
    switch (kind) {
        case GLYPH_PLAY:
            s->MoveTo(BPoint(0, 0));
            s->LineTo(BPoint(tri, h / 2));
            s->LineTo(BPoint(0, h));
            s->Close();
            break;
        case GLYPH_PAUSE: {
            float stem = floorf(h / 3.0f);
            s->MoveTo(BPoint(0, 0));
            s->LineTo(BPoint(stem, 0));
            s->LineTo(BPoint(stem, h));
            s->LineTo(BPoint(0, h));
            s->Close();
            s->MoveTo(BPoint(h - stem, 0));
            s->LineTo(BPoint(h, 0));
            s->LineTo(BPoint(h, h));
            s->LineTo(BPoint(h - stem, h));
            s->Close();
            break;
        }
        case GLYPH_STOP: {
            float e = floorf(h * 0.9f);
            float o = floorf((h - e) / 2);
            s->MoveTo(BPoint(0, o));
            s->LineTo(BPoint(e, o));
            s->LineTo(BPoint(e, o + e));
            s->LineTo(BPoint(0, o + e));
            s->Close();
            break;
        }
        case GLYPH_RTZ:
            s->MoveTo(BPoint(0, 0));
            s->LineTo(BPoint(bar, 0));
            s->LineTo(BPoint(bar, h));
            s->LineTo(BPoint(0, h));
            s->Close();
            s->MoveTo(BPoint(bar, h / 2));
            s->LineTo(BPoint(bar + tri, 0));
            s->LineTo(BPoint(bar + tri, h));
            s->Close();
            break;
        case GLYPH_NEXT:
            s->MoveTo(BPoint(0, 0));
            s->LineTo(BPoint(tri, h / 2));
            s->LineTo(BPoint(0, h));
            s->Close();
            s->MoveTo(BPoint(tri, 0));
            s->LineTo(BPoint(tri + bar, 0));
            s->LineTo(BPoint(tri + bar, h));
            s->LineTo(BPoint(tri, h));
            s->Close();
            break;
        case GLYPH_STEP_BACK:
            s->MoveTo(BPoint(0, h / 2));
            s->LineTo(BPoint(tri, 0));
            s->LineTo(BPoint(tri, h));
            s->Close();
            s->MoveTo(BPoint(tri, h / 2));
            s->LineTo(BPoint(2 * tri, 0));
            s->LineTo(BPoint(2 * tri, h));
            s->Close();
            break;
        case GLYPH_STEP_FWD:
            s->MoveTo(BPoint(0, 0));
            s->LineTo(BPoint(tri, h / 2));
            s->LineTo(BPoint(0, h));
            s->Close();
            s->MoveTo(BPoint(tri, 0));
            s->LineTo(BPoint(2 * tri, h / 2));
            s->LineTo(BPoint(tri, h));
            s->Close();
            break;
        default:
            break;
    }
    return s;
}

void StateButton::DrawShapeGlyph(BRect rect, rgb_color color)
{
    float h = GlyphHeight();
    BShape *shape = CreateGlyphShape(m_glyph, h);
    BRect sb = shape->Bounds();
    // Centre the shape's bounds in the button (MediaPlayer's offset rule).
    BPoint offset((rect.left + rect.right) / 2 - sb.Width() / 2 - sb.left,
                  (rect.top + rect.bottom) / 2 - sb.Height() / 2 - sb.top);
    offset.x = floorf(offset.x);
    offset.y = ceilf(offset.y);

    SetDrawingMode(B_OP_OVER);
    MovePenTo(offset);
    FillShape(shape, GlyphGradient(color, offset.y, offset.y + h, offset.x));
    MovePenTo(B_ORIGIN);
    delete shape;
}

/* The repeat symbol: two rounded half-loops, top running right and bottom
 * running left, each ending in an arrowhead. The loops are stroked with round
 * caps, the arrowheads filled. Corners are cubic Béziers (k = 0.5523, a
 * quarter circle). */
void StateButton::DrawLoopGlyph(BRect rect, rgb_color color)
{
    float h = GlyphHeight();
    float W = floorf(h * 1.7f);
    float H = floorf(h * 0.95f);
    float r = H * 0.4f;
    const float k = 0.5523f;
    float a = floorf(h * 0.3f) + 0.5f; // arrowhead half-height
    float al = floorf(h * 0.38f);      // arrowhead length
    float pen = h / 6.5f;
    if (pen < 1.5f)
        pen = 1.5f;

    float top_end = floorf(W * 0.6f); // where the top line meets its arrowhead
    float bot_end = W - top_end;      // where the bottom line meets its arrowhead

    BShape path;
    path.MoveTo(BPoint(0, H * 0.62f));
    path.LineTo(BPoint(0, r));
    path.BezierTo(BPoint(0, r - k * r), BPoint(r - k * r, 0), BPoint(r, 0));
    path.LineTo(BPoint(top_end, 0));
    path.MoveTo(BPoint(W, H * 0.38f));
    path.LineTo(BPoint(W, H - r));
    path.BezierTo(BPoint(W, H - r + k * r), BPoint(W - r + k * r, H), BPoint(W - r, H));
    path.LineTo(BPoint(bot_end, H));

    BShape heads;
    heads.MoveTo(BPoint(top_end + al, 0));
    heads.LineTo(BPoint(top_end, -a));
    heads.LineTo(BPoint(top_end, a));
    heads.Close();
    heads.MoveTo(BPoint(bot_end - al, H));
    heads.LineTo(BPoint(bot_end, H - a));
    heads.LineTo(BPoint(bot_end, H + a));
    heads.Close();

    // The glyph spans x 0..W (arrow tips stay inside), y -a..H+a.
    BPoint offset(floorf((rect.left + rect.right) / 2 - W / 2),
                  floorf((rect.top + rect.bottom) / 2 - H / 2) + 0.5f);
    BGradientLinear g = GlyphGradient(color, offset.y - a, offset.y + H + a, offset.x);

    SetDrawingMode(B_OP_OVER);
    float old_pen = PenSize();
    cap_mode old_cap = LineCapMode();
    join_mode old_join = LineJoinMode();
    float old_miter = LineMiterLimit();
    SetPenSize(pen);
    SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
    MovePenTo(offset);
    StrokeShape(&path, g);
    MovePenTo(offset);
    FillShape(&heads, g);
    MovePenTo(B_ORIGIN);
    SetLineMode(old_cap, old_join, old_miter);
    SetPenSize(old_pen);
}

/* Record (and record-arm): a solid dot; in punch mode a smaller dot inside a
 * ring, so punch still reads at a glance. Red on the resting button; the
 * active text colour (white) on the red active base while recording/armed. */
void StateButton::DrawRecordGlyph(BRect rect, rgb_color color)
{
    bool lit = IsActiveState() && m_has_active_color;
    rgb_color c = lit ? color : (rgb_color){204, 51, 43, 255};
    if (!IsEnabled() && !lit)
        c = tint_color(c, B_LIGHTEN_1_TINT);

    float h = GlyphHeight();
    BPoint ctr(floorf((rect.left + rect.right) / 2) + 0.5f,
               floorf((rect.top + rect.bottom) / 2) + 0.5f);
    float R = h * 0.5f;
    SetDrawingMode(B_OP_OVER);
    if (m_rec_punch) {
        float pen = h / 8.0f < 1.2f ? 1.2f : h / 8.0f;
        float old_pen = PenSize();
        SetPenSize(pen);
        SetHighColor(c);
        StrokeEllipse(ctr, R + pen * 0.5f, R + pen * 0.5f);
        SetPenSize(old_pen);
        float r = R * 0.5f;
        FillEllipse(ctr, r, r, GlyphGradient(c, ctr.y - r, ctr.y + r, ctr.x));
    } else {
        FillEllipse(ctr, R, R, GlyphGradient(c, ctr.y - R, ctr.y + R, ctr.x));
    }
}

/* Mono / stereo input: one ring, or two overlapping rings side by side. */
void StateButton::DrawRingsGlyph(BRect rect, rgb_color color, bool stereo)
{
    float h = GlyphHeight();
    float R = h * 0.42f;
    float pen = h / 9.0f < 1.2f ? 1.2f : h / 9.0f;
    BPoint ctr(floorf((rect.left + rect.right) / 2) + 0.5f,
               floorf((rect.top + rect.bottom) / 2) + 0.5f);
    float old_pen = PenSize();
    SetPenSize(pen);
    SetDrawingMode(B_OP_OVER);
    SetHighColor(color);
    if (stereo) {
        float dx = R * 0.6f;
        StrokeEllipse(BPoint(ctr.x - dx, ctr.y), R, R);
        StrokeEllipse(BPoint(ctr.x + dx, ctr.y), R, R);
    } else {
        StrokeEllipse(ctr, R, R);
    }
    SetPenSize(old_pen);
}
