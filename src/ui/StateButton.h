#ifndef STATEBUTTON_H
#define STATEBUTTON_H

#include <Button.h>
#include <InterfaceDefs.h>

class BShape;

// A BButton that tints its background with a caller-supplied colour when it is
// in its "active" state, and can draw a vector glyph instead of a text label.
// Drawing goes through be_control_look exactly like BButton::Draw so the native
// frame/press look is preserved; only the base/text colours are substituted
// when active.
//
// Glyphs are filled BShapes with a light vertical gradient, sized from the
// plain font, in the manner of Haiku MediaPlayer's transport buttons, so they
// scale with the system font and never depend on a font carrying a symbol.
//
// Active state is (Value() == B_CONTROL_ON) for toggle buttons, OR a forced
// flag for momentary buttons (e.g. the Fx button, active whenever the track
// carries an effect chain).
class StateButton : public BButton
{
public:
    enum GlyphKind {
        GLYPH_LABEL = 0, // draw Label() text (default)
        GLYPH_LOOP,      // repeat arrows
        GLYPH_RECORD,    // dot (normal) / dot in a ring (punch); also record-arm
        GLYPH_PLAY,      // right triangle
        GLYPH_PAUSE,     // two bars
        GLYPH_STOP,      // square
        GLYPH_RTZ,       // bar + left triangle: return to zero
        GLYPH_STEP_BACK, // two left triangles
        GLYPH_STEP_FWD,  // two right triangles
        GLYPH_NEXT,      // right triangle + bar: next boundary
        GLYPH_MONO,      // one ring
        GLYPH_STEREO     // two overlapping rings
    };

    StateButton(const char *name, const char *label, BMessage *message);

    // A transport button: glyph, no label, a tooltip, and a fixed width that
    // fits the glyph, so a row of them is even.
    static StateButton *MakeGlyphButton(const char *name, GlyphKind kind, BMessage *message,
                                        const char *tooltip);

    // Colour used for the button background when active, and the label/glyph
    // colour drawn on top of it.
    void SetActiveColor(rgb_color base, rgb_color text);

    // Force the active look regardless of Value() (for momentary buttons).
    void SetForcedActive(bool active);

    // For momentary buttons whose colour should track only SetForcedActive and
    // never the transient Value() during a click (e.g. the Fx button).
    void SetActiveIgnoresValue(bool ignore);

    void SetGlyph(GlyphKind kind);

    // Record-glyph state (only meaningful when GLYPH_RECORD): whether a take is
    // actively recording, and whether the engine is in punch mode.
    void SetRecordState(bool recording, bool punch);

    void Draw(BRect updateRect) override;

private:
    bool IsActiveState() const;
    float GlyphHeight() const;
    BShape *CreateGlyphShape(GlyphKind kind, float h) const;
    void DrawShapeGlyph(BRect rect, rgb_color color);
    void DrawLoopGlyph(BRect rect, rgb_color color);
    void DrawRecordGlyph(BRect rect, rgb_color color);
    void DrawRingsGlyph(BRect rect, rgb_color color, bool stereo);

    rgb_color m_active_base;
    rgb_color m_active_text;
    bool m_has_active_color;
    bool m_forced_active;
    bool m_ignore_value;
    GlyphKind m_glyph;
    bool m_rec_recording;
    bool m_rec_punch;
};

#endif // STATEBUTTON_H
