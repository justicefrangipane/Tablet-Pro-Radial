# Radial sound styles

Original synthesized PCM audio created for Tablet Pro Radial. These are the exact five styles approved from the subtle ASMR audition: 01 Felt click, 02 Walnut tap, 03 Bamboo ticks, 04 Tiny gears, and 10 Crystal whisper (default).

The next audition adds Linen tick, Cork touch, Cedar knock, Bamboo bead, Watch tick, Porcelain touch, and Crystal hush. Paper key (05), Oiled gear (06), and Dew click (09) were rejected and are not included. Crystal whisper is first in the picker and remains the default; Watch tick is second. The list has twelve styles and 48 clips in total.

Each style contains press, opening, closing, and modifier-lock clips. All files are mono, 48 kHz, 16-bit PCM WAV. Playback gain is 0.45, matching the audition; hover ticks use 0.24. No third-party samples or field recordings are included. Clips have bounded peaks and faded endpoints. Sound on/off remains independent of the selected style.

The lock cues use a quiet seating sound followed by a small confirming catch, derived from each original press timbre. They last 103–138 ms, peak below the corresponding press, and can be reproduced with `python scripts/build-modifier-lock-sounds.py`. They play only after successfully arming a pull-to-hover lock, once per transition. Hover ticks cannot cut the confirmation short. Preview lock is independent of mute and does not send any keyboard input.
