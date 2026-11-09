#include "TransportView.h"

#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Looper.h>
#include <Message.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <Window.h>

#include <OS.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "engine/jackdaw-engine.h"
#include "engine/tempomap.h"
#include "engine/timeruler.h"
#include "Messages.h"
#include "StateButton.h"
#include "StepperControl.h"

// Timecode formats cycled by clicking the position readout (time-ruler mode).
static const int32 kTimeModes[] = {TIMEMODE_REALLONG, TIMEMODE_REAL,  TIMEMODE_SAMPLES,
                                   TIMEMODE_24FPS,    TIMEMODE_25FPS, TIMEMODE_NTSC,
                                   TIMEMODE_30FPS};
static const int kNumTimeModes = (int)(sizeof(kTimeModes) / sizeof(kTimeModes[0]));

// A StateButton that, on a right-click, posts a context-menu trigger (with the
// screen point) to its window instead of invoking/toggling. Used for the
// Record, Metro and Mixer buttons. Deriving from StateButton lets Record carry
// the red active tint + ring/bullseye glyph while keeping the right-click menu.
class ContextButton : public StateButton
{
public:
    ContextButton(const char *name, const char *label, BMessage *invoke, uint32 context_what)
        : StateButton(name, label, invoke), m_context_what(context_what)
    {
    }

    void MouseDown(BPoint where) override
    {
        int32 buttons = 0;
        if (Looper()) {
            BMessage *cur = Looper()->CurrentMessage();
            if (cur)
                cur->FindInt32("buttons", &buttons);
        }
        if (buttons & B_SECONDARY_MOUSE_BUTTON) {
            ConvertToScreen(&where);
            BMessage msg(m_context_what);
            msg.AddPoint("screen_where", where);
            if (Window())
                Window()->PostMessage(&msg);
            return; // don't invoke/toggle on right-click
        }
        BButton::MouseDown(where);
    }

private:
    uint32 m_context_what;
};

// A BStringView that posts a message to a target handler when clicked — the
// position readout, which cycles the timecode format.
class ClickableStringView : public BStringView
{
public:
    ClickableStringView(const char *name, const char *text, uint32 click_what)
        : BStringView(name, text), m_click_what(click_what), m_target(NULL)
    {
    }

    void SetClickTarget(BHandler *target)
    {
        m_target = target;
    }

    void MouseDown(BPoint where) override
    {
        (void)where;
        if (m_target && Looper())
            Looper()->PostMessage(m_click_what, m_target);
    }

private:
    uint32 m_click_what;
    BHandler *m_target;
};

// The play / record indicator right of the position readout, drawn with the
// same HVIF icons as the transport buttons. During a count-in the play icon
// blinks.
class StateIconView : public BView
{
public:
    enum State { NONE, PLAY, RECORD, RECORD_PUNCH, COUNT_IN };

    StateIconView()
        : BView("state", B_WILL_DRAW), m_state(NONE), m_blink_on(true), m_bitmap(NULL),
          m_bitmap_state(NONE)
    {
        float px = StateButton::ButtonIconSize();
        SetExplicitMinSize(BSize(px + 8.0f, px));
        SetExplicitMaxSize(BSize(px + 8.0f, B_SIZE_UNLIMITED));
    }

    ~StateIconView() override
    {
        delete m_bitmap;
    }

    void AttachedToWindow() override
    {
        BView::AttachedToWindow();
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    }

    void SetState(State state)
    {
        bool blink_on = state != COUNT_IN || (system_time() / 250000) % 2 == 0;
        if (state == m_state && blink_on == m_blink_on)
            return;
        m_state = state;
        m_blink_on = blink_on;
        Invalidate();
    }

    void Draw(BRect updateRect) override
    {
        (void)updateRect;
        if (m_state == NONE || !m_blink_on)
            return;
        State icon_state = m_state == COUNT_IN ? PLAY : m_state;
        float px = StateButton::ButtonIconSize();
        if (!m_bitmap || m_bitmap_state != icon_state || m_bitmap->Bounds().Width() + 1 != px) {
            delete m_bitmap;
            size_t size = 0;
            const uint8 *data = StateButton::IconFor(icon_state == PLAY ? StateButton::GLYPH_PLAY
                                                                        : StateButton::GLYPH_RECORD,
                                                     icon_state == RECORD_PUNCH, &size);
            m_bitmap = data ? StateButton::RenderIcon(data, size, px) : NULL;
            m_bitmap_state = icon_state;
            if (!m_bitmap)
                return;
        }
        BRect b = Bounds();
        BPoint at(floorf((b.left + b.right - px) / 2 + 0.5f),
                  floorf((b.top + b.bottom - px) / 2 + 0.5f));
        SetDrawingMode(B_OP_ALPHA);
        SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
        DrawBitmap(m_bitmap, at);
        SetDrawingMode(B_OP_COPY);
    }

private:
    State m_state;
    bool m_blink_on;
    BBitmap *m_bitmap;
    State m_bitmap_state;
};

TransportView::TransportView(JackDawProject *project)
    : BView("transport", B_WILL_DRAW), m_project(project), m_timemode(TIMEMODE_REALLONG)
{
    // Vector-glyph transport buttons (StateButton draws them as filled
    // shapes, so they never depend on a font carrying the symbols).
    m_rtz_button = StateButton::MakeGlyphButton("rtz", StateButton::GLYPH_RTZ,
                                                new BMessage(MSG_TRANSPORT_RTZ), "Go to start");
    m_step_back = StateButton::MakeGlyphButton("step_back", StateButton::GLYPH_STEP_BACK,
                                               new BMessage(MSG_TRANSPORT_STEP_BACK), "Step back");
    m_step_fwd = StateButton::MakeGlyphButton("step_fwd", StateButton::GLYPH_STEP_FWD,
                                              new BMessage(MSG_TRANSPORT_STEP_FWD), "Step forward");
    m_next_button =
        StateButton::MakeGlyphButton("next", StateButton::GLYPH_NEXT,
                                     new BMessage(MSG_TRANSPORT_NEXT_BOUNDARY), "Next boundary");
    m_play_button = StateButton::MakeGlyphButton("play", StateButton::GLYPH_PLAY,
                                                 new BMessage(MSG_TRANSPORT_PLAY), "Play");
    m_loop_button = StateButton::MakeGlyphButton("loop", StateButton::GLYPH_LOOP,
                                                 new BMessage(MSG_TRANSPORT_LOOP), "Loop");
    m_pause_button = StateButton::MakeGlyphButton("pause", StateButton::GLYPH_PAUSE,
                                                  new BMessage(MSG_TRANSPORT_PAUSE), "Pause");
    m_stop_button = StateButton::MakeGlyphButton("stop", StateButton::GLYPH_STOP,
                                                 new BMessage(MSG_TRANSPORT_STOP), "Stop");
    // Record keeps its right-click menu (Normal / Punch In/Out): a ContextButton
    // sized like the rest.
    m_record_button =
        new ContextButton("record", "M", new BMessage(MSG_TRANSPORT_RECORD), MSG_RECORD_MENU);
    m_record_button->SetGlyph(StateButton::GLYPH_RECORD);
    m_record_button->SetToolTip("Record (right-click: Normal or Punch In/Out)");
    m_record_button->SetExplicitMinSize(m_play_button->ExplicitMinSize());
    m_record_button->SetExplicitMaxSize(m_play_button->ExplicitMaxSize());

    m_play_button->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);
    m_loop_button->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);
    m_record_button->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);

    // State colours: Play green, Loop light green, Record red (dot, or dot in
    // a ring in punch mode).
    m_play_button->SetActiveColor((rgb_color){46, 139, 87, 255}, (rgb_color){255, 255, 255, 255});
    m_loop_button->SetActiveColor((rgb_color){140, 230, 140, 255}, (rgb_color){16, 16, 16, 255});
    m_record_button->SetActiveColor((rgb_color){192, 57, 43, 255}, (rgb_color){255, 255, 255, 255});

    ClickableStringView *readout =
        new ClickableStringView("readout", "00:00.000", MSG_CYCLE_TIMEMODE);
    m_readout = readout;
    m_state = new StateIconView();
    BFont font(be_bold_font);
    font.SetSize(font.Size() * 1.6f);
    m_readout->SetFont(&font);
    m_readout->SetAlignment(B_ALIGN_LEFT);

    m_split_button =
        StateButton::MakeGlyphButton("split", StateButton::GLYPH_SPLIT, new BMessage(MSG_SPLIT),
                                     "Split the active track at the playhead");
    m_grid = new BCheckBox("Grid", new BMessage(MSG_TOGGLE_GRID));
    m_snap = new BCheckBox("Snap", new BMessage(MSG_TOGGLE_SNAP));
    // Metro keeps its right-click menu (volume, count-in, click output).
    m_metro_button =
        new ContextButton("metro", "M", new BMessage(MSG_TOGGLE_METRONOME), MSG_METRO_MENU);
    m_metro_button->SetGlyph(StateButton::GLYPH_METRONOME);
    m_metro_button->SetToolTip("Metronome (right-click for options)");
    m_metro_button->SetExplicitMinSize(m_play_button->ExplicitMinSize());
    m_metro_button->SetExplicitMaxSize(m_play_button->ExplicitMaxSize());
    m_metro_button->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);
    m_metro_button->SetActiveColor((rgb_color){255, 190, 60, 255}, (rgb_color){16, 16, 16, 255});
    m_bars_mode = new BCheckBox("Bars", new BMessage(MSG_TOGGLE_RULER_MODE));

    m_bpm = new StepperControl("bpm", "BPM", new BMessage(MSG_SET_BPM), 20, 999, 1, 0);
    m_sig_num = new StepperControl("signum", "Sig", new BMessage(MSG_SET_TIMESIG), 1, 32, 1, 0);
    m_sig_den = new StepperControl("sigden", "/", new BMessage(MSG_SET_TIMESIG), 1, 32, 1, 0);

    m_mixer_button =
        new ContextButton("mixer", "Mixer", new BMessage(MSG_MIXER_TOGGLE), MSG_MIXER_MENU);
    m_mixer_button->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);

    // An empty view as wide as the Mixer button, left of row 2, so the
    // position readout sits in the true centre of the bar.
    BView *balance = new BView("mixer_balance", 0);

    // Row 1: transport, editing tools, then tempo on the right.
    // Row 2: position readout and state icon centred, the mixer on the right.
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
        .SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, B_USE_SMALL_INSETS, 0)
        .Add(m_rtz_button)
        .Add(m_step_back)
        .Add(m_step_fwd)
        .Add(m_next_button)
        .Add(new BSeparatorView(B_VERTICAL))
        .Add(m_play_button)
        .Add(m_loop_button)
        .Add(m_pause_button)
        .Add(m_stop_button)
        .Add(m_record_button)
        .Add(new BSeparatorView(B_VERTICAL))
        .Add(m_split_button)
        .Add(m_grid)
        .Add(m_snap)
        .Add(m_bars_mode)
        .AddGlue()
        .Add(m_metro_button)
        .Add(m_bpm)
        .Add(m_sig_num)
        .Add(m_sig_den)
        .End()
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
        .SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_SPACING, B_USE_SMALL_INSETS, B_USE_SMALL_INSETS)
        .Add(balance)
        .AddGlue()
        .Add(m_readout)
        .Add(m_state)
        .AddGlue()
        .Add(m_mixer_button)
        .End();
}

void TransportView::AttachedToWindow()
{
    BView::AttachedToWindow();
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    // Balance the Mixer button so the centred readout is truly centred.
    BView *balance = FindView("mixer_balance");
    if (balance) {
        BSize mixer = m_mixer_button->PreferredSize();
        balance->SetExplicitMinSize(BSize(mixer.width, 0));
        balance->SetExplicitMaxSize(BSize(mixer.width, B_SIZE_UNLIMITED));
    }

    // Project-scoped controls apply on this looper; the readout cycles its own
    // timecode format. The transport/loop/mixer buttons keep their default
    // target (the window), which owns the engine calls.
    static_cast<ClickableStringView *>(m_readout)->SetClickTarget(this);
    m_metro_button->SetTarget(this);
    m_grid->SetTarget(this);
    m_snap->SetTarget(this);
    m_bars_mode->SetTarget(this);
    m_bpm->SetTarget(this);
    m_sig_num->SetTarget(this);
    m_sig_den->SetTarget(this);

    SyncControls();
    UpdateReadout();
}

void TransportView::SyncControls()
{
    m_bpm->SetValue(jackdaw_project_get_bpm(m_project));
    m_sig_num->SetValue(m_project->beats_per_bar);
    m_sig_den->SetValue(m_project->beat_unit);
    m_grid->SetValue(m_project->grid_enabled ? B_CONTROL_ON : B_CONTROL_OFF);
    m_snap->SetValue(m_project->snap_enabled ? B_CONTROL_ON : B_CONTROL_OFF);
    m_metro_button->SetValue(m_project->metronome_enabled ? B_CONTROL_ON : B_CONTROL_OFF);
    m_bars_mode->SetValue(m_project->ruler_mode == JACKDAW_RULER_BARS ? B_CONTROL_ON
                                                                      : B_CONTROL_OFF);
}

void TransportView::UpdateReadout()
{
    off_t pos = jackdaw_engine_get_play_pos();
    guint32 sr = jackdaw_engine_get_sample_rate();
    char text[80];

    if (m_project->ruler_mode == JACKDAW_RULER_BARS) {
        TempoMap tm;
        tempomap_from_project(&tm, m_project, sr);
        TempoMapBBT bbt;
        tempomap_frame_to_bbt(&tm, pos, &bbt);
        snprintf(text, sizeof(text), "%u.%u.%03u", bbt.bar, bbt.beat, bbt.tick);
    } else {
        char tc[64];
        format_timecode(sr, pos, 0, tc, m_timemode);
        snprintf(text, sizeof(text), "%s", tc);
    }

    m_readout->SetText(text);
    FitReadout();

    bool playing = jackdaw_engine_is_playing();
    bool recording = jackdaw_engine_is_recording();
    bool counting = jackdaw_engine_is_counting_in();

    bool punch = jackdaw_engine_get_record_mode() == RECORD_MODE_PUNCH;
    if (counting)
        m_state->SetState(StateIconView::COUNT_IN);
    else if (recording)
        m_state->SetState(punch ? StateIconView::RECORD_PUNCH : StateIconView::RECORD);
    else if (playing)
        m_state->SetState(StateIconView::PLAY);
    else
        m_state->SetState(StateIconView::NONE);

    // Reflect engine state on the toggle buttons (state can change via keyboard,
    // menu, count-in hand-off or loop, not only the buttons themselves).
    m_play_button->SetValue((playing || counting) ? B_CONTROL_ON : B_CONTROL_OFF);
    m_record_button->SetValue(recording ? B_CONTROL_ON : B_CONTROL_OFF);
    m_record_button->SetRecordState(recording, punch);
    m_loop_button->SetValue(jackdaw_engine_get_loop_enabled() ? B_CONTROL_ON : B_CONTROL_OFF);
}

// Size the readout to its text, so the state icon sits right next to it. The
// width only changes with the timecode format (digits are equal width), and
// the glue either side of the readout absorbs it.
void TransportView::FitReadout()
{
    float w = ceilf(m_readout->StringWidth(m_readout->Text())) + 6.0f;
    if (fabsf(w - m_readout->ExplicitMinSize().width) < 0.5f)
        return;
    m_readout->SetExplicitMinSize(BSize(w, B_SIZE_UNSET));
    m_readout->SetExplicitMaxSize(BSize(w, B_SIZE_UNSET));
}

void TransportView::MouseDown(BPoint where)
{
    MakeFocus(true); /* pull focus off a BPM/timesig text control */
    BView::MouseDown(where);
}

void TransportView::MessageReceived(BMessage *message)
{
    switch (message->what) {
        case MSG_SET_BPM: {
            double bpm = m_bpm->Value();
            if (bpm > 0.0)
                jackdaw_project_set_bpm(m_project, bpm);
            m_bpm->SetValue(jackdaw_project_get_bpm(m_project)); /* show clamp */
            break;
        }
        case MSG_SET_TIMESIG: {
            int num = (int)m_sig_num->Value();
            int den = (int)m_sig_den->Value();
            if (num > 0 && den > 0)
                jackdaw_project_set_time_signature(m_project, (guint)num, (guint)den);
            m_sig_num->SetValue(m_project->beats_per_bar);
            m_sig_den->SetValue(m_project->beat_unit);
            break;
        }
        case MSG_TOGGLE_METRONOME:
            jackdaw_project_set_metronome(m_project, m_metro_button->Value() == B_CONTROL_ON);
            break;
        case MSG_TOGGLE_GRID:
            jackdaw_project_set_grid_enabled(m_project, m_grid->Value() == B_CONTROL_ON);
            break;
        case MSG_TOGGLE_SNAP:
            jackdaw_project_set_snap_enabled(m_project, m_snap->Value() == B_CONTROL_ON);
            break;
        case MSG_TOGGLE_RULER_MODE:
            jackdaw_project_set_ruler_mode(m_project, m_bars_mode->Value() == B_CONTROL_ON
                                                          ? JACKDAW_RULER_BARS
                                                          : JACKDAW_RULER_TIME);
            UpdateReadout();
            break;
        case MSG_CYCLE_TIMEMODE: {
            int idx = 0;
            for (int i = 0; i < kNumTimeModes; i++)
                if (kTimeModes[i] == m_timemode)
                    idx = i;
            m_timemode = kTimeModes[(idx + 1) % kNumTimeModes];
            UpdateReadout();
            break;
        }
        default:
            BView::MessageReceived(message);
            break;
    }
}
