# Left off: TCL 30 SE internal audio (2026-11-11 ~08:10)

**HEADPHONE AUDIO WORKS (owner confirmed by ear, 08:10): "Too Many Nights" via PipeWire, S32_LE, sounds good.**
Fix: S32_LE needs HDALIGN align = 1 (AFE_WLEN_32_BIT_ALIGN_24BIT_DATA_8BIT_0, data in the top 24 bits). The vendor's align 0
(8BIT_0_24BIT_DATA) is the S24_LE layout; with it S32 lost its top byte and loud music wrapped. Module md5 `9756729f...`
(copy `drivers-built/audio/snd-soc-mt6765-pcm.ko.9756729f`; previous `b273e47e` in `/root/kernel-backup/` on the phone).
The WirePlumber S16 workaround is disabled (renamed `50-casper-s16.conf.off`). Everything below the next heading is history.

Full history is in `HANDOFF.md` ("Audio WORKS (driver level)", "Audio follow-up"). This file covers only where work stopped.

## Result this session
- **16-bit playback is CLEAN on the headphone jack** (owner confirmed by ear: soft 440 Hz S16 tone, HP volume min).
- **32-bit (S32_LE) playback is DISTORTED**: "Too Many Nights" via PipeWire (which opened DL1 as S32_LE) sounded distorted.
- Workaround in place: WirePlumber rule `/home/siddh/.config/wireplumber/wireplumber.conf.d/50-casper-s16.conf`
  forces `audio.format = S16LE` on alsa outputs. Verified with silence: PCM opens S16_LE, 0x3f8 = 0x10, 0x06c = 0xcfffffe7.
  **Not yet heard by the owner with music.** Ask before playing anything.

## Phone state
- Installed AFE module md5 `b273e47e...` (copy: `drivers-built/audio/snd-soc-mt6765-pcm.ko.b273e47e`);
  previous one backed up at `/root/kernel-backup/snd-soc-mt6765-pcm.ko.53f32a74` on the phone.
- Changes vs 53f32a74:
  - The earlier "AFE_I2S_CON1 bit 16 = 1" idea was WRONG and is reverted: vendor `set_i2s_dac_out_source()` calls
    `Afe_Set_Reg(AFE_I2S_CON1, 1, 1 << 16)` (value 1, mask bit 16) so it never sets the bit, and `SetI2SDacOut()` writes 0.
    Bit 16 stays 0. (During playback AFE_CONN28/29 = 0 and yet audio comes out, so bit16=0 evidently feeds the DAC from O03/O04
    here despite the vendor comment.)
  - fe_prepare: AFE_CONN_24BIT bits 3/4/28/29 = 16-bit for S16, 24-bit otherwise (vendor SetConnectionFormat for I2S1_DAC and _2).
- Amp aw87xxx OFF (reg 0x01 = 0x00), not switched on at boot.
- Mixer stored with `alsactl store 0` (persists): Headphone L/R Source = DAC, Headphone Volume 0 (-10 dB),
  Headphone Vin 0 (-12 dB), Line Out Source Open, Handset Source Open, Lineout/Handset volume 0.
  Old state backed up at `/root/kernel-backup/asound.state.pre-lowvol`.
- PipeWire running, sink "Built-in Audio Stereo" at 0.25.

## Next steps (S32 distortion)
- Compare our S32 path with vendor `mtk-soc-pcm-dl1.c` prepare: S32 -> `AFE_WLEN_32_BIT_ALIGN_8BIT_0_24BIT_DATA`, conn 24-bit, I2S WLEN 32.
  Check what HD_MODE value 1 + HDALIGN 0 really mean on MT6765 (vendor SetMemIfFetchFormatPerSample) and test S32 with a
  silent register check, then a soft tone (aplay -f S32_LE) before music.
- Or restrict DL1/DL2 to S16 in the driver and drop the WirePlumber rule.
- Speaker: only after music is clean on headphones; amp at low gain first.

## Rules
- Ask the owner before playing anything audible; keep it low. Owner listens on headphones plugged into the phone.
- Never play audio on the laptop. Don't record with the laptop mic (too faint, and records the room).
- `sound/soc/mediatek/mt6765/` is untracked in the kernel git tree; copies in `debian/backups/audio-src-2026-11-11/`.

## Speaker, 2026-11-11 (afternoon)
- Owner: speaker is NOT dead, drivers need work. The 07:00 "blown" sound was almost certainly the S32 align bug
  (same distortion on headphones until the 08:10 fix), played through the amp at stock music gain.
- Speaker was silent because `Line Out Source` was `Open` (low-volume mixer state). Now `Playback`, `Lineout` 0 (-10 dB),
  stored with `alsactl store 0` (old state: /root/kernel-backup/asound.state.pre-lineout). DAPM check with silence:
  Line out powers On during playback, Off after.
- `aw87-spk low` added = stock receiver table (reg05 0x00, reg07 0x4e vs music 0x0f/0x92). Original script in
  /root/kernel-backup/aw87-spk.orig. Amp still OFF, not enabled at boot.
- The vmax_0.bin is NOT register writes ((threshold, value) int32 pairs for the vendor's vbat/DSP limiter), so skip it.
- Next (needs owner listening): `aw87-spk low`, quiet tone, `aw87-spk off`; then music, then `on`, then boot/per-stream enable.
- **SPEAKER WORKS (owner confirmed, 2026-11-11 afternoon).** Earlier silent tests were just too quiet: Lineout -10 dB
  (Android uses +8 dB, vendor Speaker_Amp_Change + Lineout_PGA index 0) plus `aw87-spk low`. Working setup:
  Line Out Source = Playback, Lineout 0 (-10 dB), amp `aw87-spk on` (stock music table), PipeWire sink 0.45.
  Headphone L/R Source = Open (stored), so the JACK IS OFF now: no jack detection yet; set them back to DAC for headphones.
  casper-audio-up now runs `aw87-spk on` at boot (backup: /root/kernel-backup/casper-audio-up.pre-spk).

## Headphone detection, 2026-11-11 (afternoon)
- New out-of-tree module `src/mt6357-jack/mt6357-jack.c` (installed: /usr/lib/modules/7.1.0-mt6765+/extra/mt6357-jack.ko).
  Vendor mt6357-accdet init (EINT0, mode 1), then polls ACCDET_CON18 bit14 every 200 ms: 1 = unplugged, 0 = plugged
  (measured; the CON12 irq latch + polarity approach missed the first plug and inverted). Reports SW_HEADPHONE_INSERT on
  input "Headphone Jack". Finds the regmap via platform device mt6357-sound -> parent -> parent (pwrap). No mic/buttons.
  Build: make -C linux-mt6765 O=out ARCH=arm64 LLVM=1 M=$PWD modules.
- `casper-jack.service` -> /usr/local/sbin/casper-jack (evtest loop): plugged = amp off, Line Out Open, HP L/R DAC;
  unplugged = HP Open, Line Out Playback, amp on. casper-audio-up modprobes mt6357-jack before the amp line.
- First live check: detected IN with headphones plugged, routed to jack, amp off.
- 2026-11-11: CORRECTION: switching NOT verified. Logs showed no plug events during the "it works" test. CON18 bit14 is NOT a reliable level (read 0x8000 = "plugged" ~4 s after an unplug). CON12 latch rule from logs: POL(bit14)=1 latches on unplug, POL=0 on plug; the module had the polarity backwards. UPDATE: with the user confirming each physical state (no audio playing, service stopped), CON18 matched every time: 0xc700 = out, 0x8000 = in; module events 2993 OUT, 2997 IN, 3147 OUT, 3782 IN were all real. The "missed" test was no physical action. Level design kept; service re-enabled. Not yet verified: switching WHILE audio plays (owner asked for no audio).
