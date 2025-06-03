# Music Thing Modular Workshop Computer Delay Card

## TLDR

Download [this uf2](https://github.com/andym/Computer-Card-Delay/raw/main/build/computer_card_delay.uf2)

Use the ChrisJ "press reset with no card in" method to write it to a card.

Feed some audio to Audio in 1. It comes out delayed from Audio out 1 and with a different delay from Audio out 2.

The big knob changes the delay - flangy with it all the way anti-clockwise, thru some chorus-y and then on to a big 2 second delay with it all the way clockwise.

X knob is your feedback, Y knob is wet/dry.

CV in 1 takes over from the big knob. CV in 2 is the fine tuning on this, with the big knob anti-clockwise you can get flanging effects by patching CV in 2 to the output of a looping slope.

## Details

The delay is split into 20 discrete delay times with logarithmic scale (1ms to 2s). I found that a simple linear setting here caused discontinuities and distortion when it was swept around.

The delay of audio out 2 varies depending on the main delay.

Modulating the delay with CV in 2 sounds pretty weird with the knob more clockwise, it's best for flanging.

### Switch

- **Up Position**: Enables bypass mode (raw input is passed to output) and clears delay buffer
- **Middle/Down Position**: Normal delay effect operation
- **Down Position (held for 2 seconds)**: Enters USB bootloader mode for firmware updates

### CV Inputs

- **CV1**: Alternative delay time control (when cable connected)
  - Uses same 20 discrete delay segments as Main knob
  - Takes priority over Main knob when connected
  - Overridden by tap tempo when active
- **CV2**: **LFO Modulation Input** (when cable connected)
  - **Effect-specific modulation depths** for optimal sound quality:
    - **Flanger range** (1-16ms): ±75% modulation for dramatic sweeps
    - **Chorus range** (30-150ms): ±1.25% modulation for subtle shimmer
    - **Delay range** (250ms-2s): ±2.5% modulation for vintage tape character

### Pulse Inputs

- **Pulse1**: Tap tempo for delay time
  - Tap twice to set delay time based on interval between pulses
  - Finds closest discrete delay segment to tapped interval
  - Valid range: 50ms to 2s
  - Takes priority over CV1 and Main knob when active
- **Pulse2**: Freeze buffer toggle
  - Each pulse toggles buffer freeze on/off
  - When frozen, delay buffer stops updating but continues playback
  - Creates instant "hold" effects

### Pulse Outputs

- **Pulse Out 1**: Delay clock
  - Sends a pulse every time the delay cycle completes
  - 10ms pulse width, automatically syncs to current delay time
  - Stops when buffer is frozen (Pulse2 input)
- **Pulse Out 2**: Timing overflow indicator for debug
  - Generates 50ms pulses when audio processing exceeds timing deadlines
  - Used for debugging and monitoring audio processing performance

### Audio Outputs

- **Audio Out 1**: Full delay time as selected by controls
- **Audio Out 2**: Shorter tap, varies depending on the main delay

### LED Indicators

- **LEDs 0-1**: **Effect Type Indicators**
  - **Flanger**: LED 0 bright, LED 1 off (knob positions 1-5)
  - **Chorus**: LED 0 dim, LED 1 bright (knob positions 6-10)
  - **Delay**: Both LEDs medium (knob positions 11-20)
- **LEDs 2-3**: Show feedback amount
- **LEDs 4-5**: Indicate wet/dry mix level
- **All LEDs (flashing)**: Bypass mode is active
- **Progressive LED sequence**: When holding switch down for USB boot mode

## Control Priority

### Delay Time Control (highest to lowest priority):
1. **Tap Tempo** (Pulse1) - when active, overrides all other delay controls
2. **CV1** - when cable connected, overrides Main knob
3. **Main Knob** - default control when no CV1 cable and no active tap tempo

### LFO Modulation & Feedback Control:
1. **CV2 connected**: Used for LFO modulation, X knob controls feedback
2. **CV2 disconnected**: X knob controls feedback (no modulation)

### Notes:
- Jack detection automatically switches between CV and knob control
- Tap tempo remains active until overridden by CV1 or Main knob movement
- Y knob always controls wet/dry mix (no CV override)