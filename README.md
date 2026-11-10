# Kasen

Kasen (歌仙, said "kah-sen", after the 36-verse form of renku poetry) is a native
[RenkuOS](https://github.com/RenkuOS) multitrack digital audio workstation built on the
Interface Kit. It plays and records through the RenkuOS media services (Media Kit, MIDI
Kit), or as a JACK client.

## Features

- Audio and MIDI tracks, recording (normal or punch in/out), and a timeline with clips and
  regions
- Waveforms down to single samples for precise editing; resizable track heights
- Piano-roll MIDI editor
- Mixer with per-track volume, pan, mute/solo and metering
- VST3, LV2 and VST2 effects and instruments, with their own editors in the FX window
- 64-bit Windows VST2/VST3 plug-ins through vstbridge and Wine
- MIDI control-surface mapping (learn a footswitch/CC to transport, mute, FX bypass, ...)
- Project save/load (`.kasen` bundles) and offline/realtime render/export

## Install

Built packages are on the [Releases](https://github.com/rations/kasen/releases) page:
`pkgman install ./kasen-<version>-x86_64.hpkg`. Kasen needs `lv2_renku` from
[LV2-renku](https://github.com/rations/LV2-renku);
[renku-audio](https://github.com/rations/renku-audio) lists the whole audio stack. Or build
it from source (below).

## Requirements

- RenkuOS x86_64.
- Audio: the RenkuOS media services by default (connect Kasen's node in Cortex, MIDI in
  PatchBay). To use JACK instead, choose it in Options → Audio System with a JACK server
  running, for example:
  ```
  jackd -X haikumidi -d hmulti -d /dev/audio/hmulti/usb/1 -r 48000 -p 256 -n 3
  ```
- VST3, LV2 and VST2 plug-ins are picked up from the system and user `add-ons/media/VST3`,
  `media/LV2` and `media/vstplugins` directories, so the `namku`, `drumku` and `rktuner`
  packages need no extra configuration.
- Wine and vstbridge (for Windows plug-ins) have to be built on RenkuOS (see
  [renku-wine-port](https://github.com/rations/renku-wine-port)); they run
  64-bit Windows plug-ins. 32-bit Windows programs and iLok-protected plug-ins need kernel
  support that RenkuOS does not have.

## Building

Native on RenkuOS (x86_64), after jack-port-renku, VST3-renku and LV2-renku (see
[renku-audio](https://github.com/rations/renku-audio) for the order):
- `packaging/make-hpkg.sh` builds the package; the version comes from `VERSION`.
- `./build-from-source.sh` builds and installs into `/boot/system/non-packaged`.

## MIDI CC / pitch-bend into VST3 instruments — needs hardware testing

Kasen delivers MIDI CC, pitch-bend, and channel-pressure into VST3 instruments via the
plug-in's `IMidiMapping` (built at load, applied on the RT thread with pre-allocated
parameter queues). This path is implemented but has **only been verified for the MIDI
control surface** (a footswitch mapped through Options → MIDI Control). Driving a
**CC-aware VST3 synth** (mod-wheel / pitch-bend changing a plug-in parameter audibly) has
**not yet been confirmed on hardware** — it needs an instrument that implements
`IMidiMapping` and a controller that emits CC/pitch-bend. (DRUMku declares no CC
assignment, so it ignores CC by design.) Reports welcome.

## License

MIT (see [LICENSE](LICENSE)). Includes portions of the Steinberg VST3 SDK (MIT). Links
JACK's `libjack` (LGPL v2.1). VST is a trademark of Steinberg Media Technologies GmbH.
