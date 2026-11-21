# PeepChain

[![GitHub Release](https://img.shields.io/github/v/release/PeepChain/PeepChain?style=flat&color=%23FF4FA0)](https://github.com/PeepChain/PeepChain/releases/latest) [![Downloads](https://img.shields.io/github/downloads/PeepChain/PeepChain/total?style=flat&color=%23FF4FA0)](https://github.com/PeepChain/PeepChain/releases)

**PeepChain** is an [exact clone](#how-close-is-it) of Lil Peep's vocal chain as a single VST3 plugin.

Every effect in his chain has been carefully recreated from the GarageBand originals at his settings. Side by
side with GarageBand's own bounces, there's no **audible difference**.

**[Download](#install)**

*Need help?* Join the **[Discord](https://discord.gg/lilpeep)**

![PeepChain](docs/peepchain-ui.png)

## Contents

- [Usage](#usage)
- [How close is it?](#how-close-is-it)
- [Install](#install)
- [Building from source](#building-from-source)
- [License](#license)
- [Credits](#credits)

## Usage

It opens on Peep's settings. Turn **Input** up/down until the IN meter's peaks land in the **Peep's Peaks** zone, then
adjust to taste.

- **Effects**: click the power button to switch one off, drag a card to reorder, click **Edit** for its settings
- **Sync**: locks the delay, or the reverb's pre-delay, to your song's tempo
- **Auto Gain**: matches the output volume to the input, so bypassing compares the sound, not the loudness
- **Reset to Peep**: back to his settings
- **A / B**: two sets of settings to compare
- **Group**: links PeepChain across tracks (a lead, its doubles, the adlibs). Create a group from one track (it
  starts with that track's settings), then join it from the others; a change on one happens on all of them. Hover a
  group in the menu to rename or delete it. Input, Output and Auto Gain stay per track

## How close is it?

The same vocal through GarageBand and PeepChain, with the full chain including Delay and Reverb:

| Original (GarageBand) | Clone (PeepChain) |
| --- | --- |
| [▶ original.mp3](docs/example/original.mp3) | [▶ clone.mp3](docs/example/clone.mp3) |

Subtracting GarageBand's output from PeepChain's leaves almost nothing behind:

<details>
<summary><b>Measurements</b></summary>

| | Difference from GarageBand | Leftover level |
| --- | --- | --- |
| Full chain, on the three vocals it was built from | 45–47 dB quieter than the vocal | about 0.5 % |
| Full chain, on a vocal it had never heard (with Delay and Reverb) | 34 dB quieter | about 2 % |
| Delay | 57–60 dB quieter | about 0.1 % |
| Reverb | 91 dB quieter | under 0.01 % |

</details>

In other words, **no audible difference**. For scale, GarageBand itself varies more than that: the same audio
through the same settings comes out about 32 dB different depending on which other effects are switched on.

## Install

### Windows

1. Download `PeepChain-VST3-Windows.zip` from the [Releases](https://github.com/PeepChain/PeepChain/releases) page.
2. Unzip it and copy the `PeepChain.vst3` folder into `C:\Program Files\Common Files\VST3`.
3. Rescan plugins in your DAW.

### Mac

No plugin needed on a Mac: this patch loads Peep's vocal chain in GarageBand or Logic Pro with Apple's own effects at exactly the settings
PeepChain was matched against.

1. Download [`presets/Peep.patch.zip`](presets/Peep.patch.zip) and double-click it to unzip.
2. Move `Peep.patch` into `~/Music/Audio Music Apps/Patches/Audio` (in Finder: Go → Go to Folder..., paste the path).
3. Restart GarageBand or Logic, select an audio track, open the Library and pick **Peep** under User Patches.

The patch is the track only: the Echo and Reverb were sends in his project, so add them yourself.

Peep's settings, in chain order (click one to show them):

<details>
<summary><b>Noise Gate</b></summary>

| Setting | Value |
| --- | --- |
| Mode | Gate |
| Threshold | -46 dB |
| Reduction | -100 dB |
| Attack | 1 ms |
| Hold | 160 ms |
| Release | 27.1 ms |
| Hysteresis | -6 dB |
| Lookahead | 0.6 ms |
| Side chain filter | Off |

</details>

<details>
<summary><b>Compressor</b></summary>

| Setting | Value |
| --- | --- |
| Circuit type | Platinum Digital |
| Detection | RMS |
| Threshold | -26 dB |
| Ratio | 8:1 |
| Attack | 2 ms |
| Release | 48 ms |
| Make-up | +11 dB |
| Knee | 0.7 |
| Side chain | Max |
| Auto gain, limiter, distortion | Off |

</details>

<details>
<summary><b>AUFilter</b></summary>

| Setting | Value |
| --- | --- |
| Low | Low shelf, 134 Hz, +12.3 dB |
| Mid 1 | 602 Hz, +3.1 dB, 2.00 oct |
| Mid 2 | 2.8 kHz, +1.6 dB, 2.00 oct |
| Mid 3 | 5 kHz, 0 dB, 2.00 oct |
| High | High shelf, 10 kHz, 0 dB |

</details>

<details>
<summary><b>AUGraphicEQ (31 bands)</b></summary>

| Setting | Value |
| --- | --- |
| 20 Hz | +5.5 dB |
| 31.5 Hz | +2.4 dB |
| 40 Hz | +4.6 dB |
| 63 Hz | +3.2 dB |
| 100 Hz | +3.0 dB |
| 160 Hz | +7.2 dB |
| 1 kHz | +6.5 dB |
| 1.6 kHz | +2.8 dB |
| 2.5 kHz | +7.8 dB |
| 10 kHz | +12.6 dB |
| 20 kHz | +6.5 dB |
| All other bands | 0 dB |

</details>

<details>
<summary><b>AUMultibandCompressor</b></summary>

| Setting | Value |
| --- | --- |
| Crossovers | 200 Hz, 900 Hz, 4 kHz |
| Thresholds (low to high) | -68.9, -54.0, -51.3, -51.8 dB |
| Headroom | 30 dB on every band |
| Attack | 20 ms |
| Release | 40 ms |
| Pre-gain, post-gain | 0 dB |

</details>

<details>
<summary><b>AUPeakLimiter</b></summary>

| Setting | Value |
| --- | --- |
| Pre-gain | +18 dB |
| Attack | 2 ms |
| Release | 5 ms |

</details>

<details>
<summary><b>Echo (send)</b></summary>

| Setting | Value |
| --- | --- |
| Send | -16.5 dB |
| Time | 1/4 |
| Feedback | 38 % |
| Color | -11 |
| Dry | 0 % |
| Wet | 100 % |

</details>

<details>
<summary><b>Reverb (send): PlatinumVerb</b></summary>

| Setting | Value |
| --- | --- |
| Send | -6.0 dB |
| Mix | 100 % |
| Predelay | 0 ms |
| Room Size | 10 m |
| Room Shape | 5 |
| Stereo Base | 0.2 m |
| Balance ER/Reverb | 100 % |
| Initial Delay | 13 ms |
| Spread | 58 % |
| Reverb Time | 1.75 sec |
| Density | 100 % |
| Low Ratio | 50 % |
| Crossover | 400 Hz |
| High Cut | 8300 Hz |
| Low Freq Level | -10.0 dB |
| Diffusion | 100 % |
| ER Scale | 100 % |
| Dry | 0 % |
| Wet | 100 % |

</details>

## Building from source

Needs Git, CMake and a C++17 compiler. [JUCE](https://github.com/juce-framework/JUCE) 8.0.15 isn't included,
so it's cloned first:

```
git clone https://github.com/PeepChain/PeepChain.git
cd PeepChain
git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git third_party/JUCE
cmake -S . -B build
cmake --build build --config Release --target PeepChain_VST3
```

It should also build on Linux, but that hasn't been tested.

## License

The source code in this repository is under the [MIT License](LICENSE). The plugin is built with
[JUCE](https://juce.com), which is used under the [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.html), so the
released plugin (`PeepChain.vst3`) as a whole is distributed under the AGPLv3.

## Credits

Built with [JUCE](https://juce.com). Font: Inter by The Inter Project Authors, under the SIL Open Font License. Not affiliated with Apple, Lil Peep or his estate.
GarageBand is a trademark of Apple Inc.
