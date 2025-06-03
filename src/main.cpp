#include <cstdlib>

#include "ComputerCard.h"
#include "ComputerCardExtensions.h"
#include "pico/stdlib.h"
#include "pico/sync.h"

class AudioDelay : public CardExtensions::ExtendedCard {
 private:
  static const int MAX_DELAY_SAMPLES = 96000;  // 2 seconds @ 48kHz
  int16_t* delayBuffer;
  int writePosition = 0;
  int delayTime = 24000;     // 500ms default (24000 samples)
  int32_t feedback = 512;    // 50% feedback (512/1024 = 0.5)
  int32_t wet = 512;         // 50% wet/dry mix (512/1024 = 0.5)
  int32_t inputGain = 1024;  // Input gain to prevent clipping (1024 = 1.0)
  bool bypassEffect = false;
  bool freezeBuffer = false;  // Freeze delay buffer when true

  // Tap tempo variables
  uint32_t lastTapTime = 0;
  uint32_t tapCounter = 0;
  bool tapTempoActive = false;
  int tapDelayTime = delayTime;

  // Delay clock variables
  int delayCycleCounter = 0;
  int lastDelayTime = delayTime;

  // LED visualization variables
  int ledUpdateCounter = 0;
  const int LED_UPDATE_RATE =
      4800;  // Update LEDs every 4800 samples (about 10Hz)

  // Pre-calculated second delay tap to avoid multiple buffer reads
  int16_t preCalculatedDelaySample2 = 0;

  // Low-pass filter for high-frequency noise reduction
  int32_t filterState1 = 0;   // Filter state for output 1
  int32_t filterState2 = 0;   // Filter state for output 2
  int32_t filterCoeff = 819;  // ~8kHz cutoff @ 48kHz (819/1024 ≈ 0.8)

  // Timing overflow detection
  uint32_t overflowPulseCounter = 0;

 protected:
  const CardExtensions::StartupPatterns::Pattern& GetStartupPattern() override {
    return CardExtensions::StartupPatterns::EffectCard;
  }

  void __not_in_flash_func(ProcessMainSample)() override {
    // Quick timing check start
    uint32_t startTime = time_us_32();

    HandlePulseInputs();

    // Read input sample (with gain control to prevent clipping)
    int16_t rawInput = AudioIn1();
    int16_t inputSample = static_cast<int16_t>((rawInput * inputGain) >> 10);

    // Calculate read positions for both outputs
    int readPosition1 = writePosition - delayTime;
    if (readPosition1 < 0) readPosition1 += MAX_DELAY_SAMPLES;

    // Second output uses effect-specific ratios for optimal stereo/rhythmic
    // effects
    int delayTime2;
    if (delayTime <= 768) {
      // Flanger range (1-16ms): Use 1/2 ratio for dramatic stereo effect
      delayTime2 = delayTime / 2;
    } else {
      // Chorus/Delay range (30ms+): Use 3/4 ratio for thickening/polyrhythm
      delayTime2 = (delayTime * 3) / 4;
    }

    int readPosition2 = writePosition - delayTime2;
    if (readPosition2 < 0) readPosition2 += MAX_DELAY_SAMPLES;

    // Optimized dual buffer read
    int16_t delaySample1 = delayBuffer[readPosition1];
    int16_t delaySample2 = delayBuffer[readPosition2];  // Actual second tap

    // Fixed-point feedback calculation (feedback is 0-1024, so divide by 1024)
    int32_t newDelaySample =
        inputSample + ((static_cast<int32_t>(delaySample1) * feedback) >> 10);

    // Clip the delayed sample to prevent overflow
    if (newDelaySample > 2047) newDelaySample = 2047;
    if (newDelaySample < -2048) newDelaySample = -2048;

    // Write the new delayed sample to the buffer (unless frozen)
    if (!freezeBuffer) {
      delayBuffer[writePosition] = static_cast<int16_t>(newDelaySample);

      // Update write position
      writePosition = (writePosition + 1) % MAX_DELAY_SAMPLES;
    }

    // Mix dry and wet signals for both outputs
    int16_t outputSample1, outputSample2;
    if (bypassEffect) {
      outputSample1 = inputSample;
      outputSample2 = inputSample;
    } else {
      // Fixed-point wet/dry mix (wet is 0-1024, dry is 1024-wet)
      int32_t dry = 1024 - wet;
      outputSample1 = static_cast<int16_t>(
          ((inputSample * dry) + (delaySample1 * wet)) >> 10);
      outputSample2 = static_cast<int16_t>(
          ((inputSample * dry) + (delaySample2 * wet)) >> 10);
    }

    // Apply low-pass filter to reduce high-frequency noise
    // One-pole IIR: y[n] = a*y[n-1] + (1-a)*x[n]
    filterState1 = ((filterState1 * filterCoeff) +
                    (outputSample1 * (1024 - filterCoeff))) >>
                   10;
    filterState2 = ((filterState2 * filterCoeff) +
                    (outputSample2 * (1024 - filterCoeff))) >>
                   10;

    int16_t filteredOutput1 = static_cast<int16_t>(filterState1);
    int16_t filteredOutput2 = static_cast<int16_t>(filterState2);

    // Output to both channels with different delay taps
    // Disable interrupts briefly for atomic update
    uint32_t saved_irq = save_and_disable_interrupts();
    AudioOut1(filteredOutput1);
    AudioOut2(filteredOutput2);
    restore_interrupts(saved_irq);

    // Update parameters based on knob values
    ReadControls();

    // Handle delay clock output
    UpdateDelayClock();

    ledUpdateCounter++;
    if (ledUpdateCounter >= LED_UPDATE_RATE) {
      UpdateLEDs();
      ledUpdateCounter = 0;
    }

    // Quick timing check and overflow indication
    uint32_t processingTime = time_us_32() - startTime;
    if (processingTime > 21) {  // 21μs threshold
      // Generate pulse on overflow - use PulseOut2 (PulseOut1 is delay clock)
      PulseOut2(2400);  // 50ms pulse (easily visible)
    }

    // Manage overflow pulse counter to prevent pulse spam
    if (overflowPulseCounter > 0) {
      overflowPulseCounter--;
    }
  }

  void __not_in_flash_func(ReadControls)() {
    // 20 delay times with logarithmic scale from flanger to delay (1ms-2s)
    const int delayTimes[20] = {
        48,     // ~1ms    - Flanger range
        96,     // ~2ms
        192,    // ~4ms
        384,    // ~8ms
        768,    // ~16ms
        1440,   // ~30ms   - Chorus range
        2400,   // ~50ms
        3600,   // ~75ms
        4800,   // ~100ms
        7200,   // ~150ms
        12000,  // ~250ms  - Short delay range
        19200,  // ~400ms
        28800,  // ~600ms
        43200,  // ~900ms
        62400,  // ~1.3s
        72000,  // ~1.5s   - Long delay range
        76800,  // ~1.6s
        81600,  // ~1.7s
        86400,  // ~1.8s
        96000   // ~2s
    };

    // Delay time control: Tap tempo takes priority, then CV1, then Main knob
    if (!tapTempoActive) {
      int segment;
      if (Connected(CV1)) {
        // CV1 connected: use CV input for 20-segment selection
        int16_t cvVal = CVIn1();  // -2048 to 2047
        // Map CV range (4095 total) to 20 segments
        segment = (cvVal + 2048) / 205;
        if (segment > 19) segment = 19;  // Clamp to 0-19
      } else {
        // CV1 disconnected: use Main knob 20-segment selection
        uint16_t mainKnobVal = KnobVal(Main);
        segment = mainKnobVal / 205;
        if (segment > 19) segment = 19;  // Clamp to 0-19
      }

      delayTime = delayTimes[segment];
    }
    // If tapTempoActive is true, delayTime is already set by HandleTapTempo()

    // CV2 dual function: LFO modulation (when connected) OR feedback control (X
    // knob)
    if (Connected(CV2)) {
      // CV2 connected: use as LFO modulation input
      int16_t lfoVal = CVIn2();  // -2048 to 2047

      // Apply effect-specific LFO modulation with very subtle depths for longer
      // delays
      int32_t modAmount;
      if (delayTime <= 768) {
        // Flanger range (1-16ms): ±75% for dramatic sweeps
        modAmount = (delayTime * lfoVal * 3) >> 13;  // ±75% modulation depth
      } else if (delayTime <= 7200) {
        // Chorus range (30-150ms): ±1.25% for barely perceptible shimmer
        modAmount = (delayTime * lfoVal) >> 17;  // ±1.25% modulation depth
      } else {
        // Delay range (250ms+): ±2.5% for very subtle tape-like warble
        modAmount = (delayTime * lfoVal) >> 16;  // ±2.5% modulation depth
      }
      int32_t modulatedDelay = delayTime + modAmount;

      // Clamp to valid range (minimum 24 samples = 0.5ms, max buffer size)
      if (modulatedDelay < 24) modulatedDelay = 24;
      if (modulatedDelay > MAX_DELAY_SAMPLES)
        modulatedDelay = MAX_DELAY_SAMPLES;

      delayTime = modulatedDelay;

      // Use X knob for feedback when CV2 is used for modulation
      feedback = (KnobVal(X) * 922) >> 12;  // Scale to 0-922 (90% of 1024)
    } else {
      // CV2 disconnected: use X knob for feedback (original behavior)
      feedback = (KnobVal(X) * 922) >> 12;  // Scale to 0-922 (90% of 1024)
    }

    // Y knob: Wet/dry mix (0 to 1024 = 0% to 100%)
    wet = (KnobVal(Y) * 1024) >> 12;  // Scale 0-4095 to 0-1024

    // Use switch to toggle bypass and clear buffer
    Switch currentSwitch = SwitchVal();

    // Only change bypass state on switch position change
    if (SwitchChanged()) {
      if (currentSwitch == Switch::Up) {
        bypassEffect = true;
        ClearBuffer();  // Clear buffer when switch goes to up position
      } else {
        bypassEffect = false;
      }
    }
  }

  void UpdateLEDs() {
    // LEDs 0-1: Effect type indicator based on current delay range
    // Determine current segment for effect type
    int currentSegment;
    if (Connected(CV1)) {
      int16_t cvVal = CVIn1();
      currentSegment = (cvVal + 2048) / 205;
      if (currentSegment > 19) currentSegment = 19;
    } else {
      uint16_t mainKnobVal = KnobVal(Main);
      currentSegment = mainKnobVal / 205;
      if (currentSegment > 19) currentSegment = 19;
    }

    // Effect type indication:
    // Segments 0-4: Flanger (LED 0 bright, LED 1 off)
    // Segments 5-9: Chorus (LED 0 dim, LED 1 bright)
    // Segments 10+: Delay (both LEDs on)
    if (currentSegment <= 4) {
      // Flanger mode
      LedBrightness(0, 4095);  // Bright
      LedBrightness(1, 0);     // Off
    } else if (currentSegment <= 9) {
      // Chorus mode
      LedBrightness(0, 1024);  // Dim
      LedBrightness(1, 4095);  // Bright
    } else {
      // Delay mode
      LedBrightness(0, 2048);  // Medium
      LedBrightness(1, 2048);  // Medium
    }

    // LEDs 2-3: Feedback amount (feedback is 0-922, scale to 0-4095)
    uint16_t feedbackLed = static_cast<uint16_t>((feedback * 4095) / 922);
    LedBrightness(2, feedbackLed);
    LedBrightness(3, feedbackLed / 2);  // Use 1/2 instead of 1/4

    // LEDs 4-5: Wet/dry mix (wet is 0-1024, scale to 0-4095)
    uint16_t wetLed = static_cast<uint16_t>((wet * 4095) / 1024);
    LedBrightness(4, wetLed);
    LedBrightness(5, wetLed / 2);  // Use 1/2 instead of 1/4

    // If bypass is enabled, flash all LEDs
    if (bypassEffect) {
      static bool ledState = true;
      static int flashCounter = 0;

      flashCounter++;
      if (flashCounter >= 24) {  // Toggle at ~1Hz
        ledState = !ledState;
        flashCounter = 0;
      }

      if (ledState) {
        for (int i = 0; i < 6; i++) {
          LedBrightness(i, 4095);
        }
      } else {
        for (int i = 0; i < 6; i++) {
          LedBrightness(i, 0);
        }
      }
    }
  }

  void HandlePulseInputs() {
    // Pulse1: Tap tempo for delay time
    if (PulseIn1RisingEdge()) {
      HandleTapTempo();
    }

    // Pulse2: Freeze/unfreeze buffer
    if (PulseIn2RisingEdge()) {
      freezeBuffer = !freezeBuffer;
    }
  }

  void HandleTapTempo() {
    uint32_t currentTime = tapCounter++;

    if (lastTapTime > 0) {
      uint32_t tapInterval = currentTime - lastTapTime;

      // Only accept tap intervals between 50ms and 2s (2400 to 96000 samples)
      if (tapInterval >= 2400 && tapInterval <= 96000) {
        tapDelayTime = tapInterval;
        tapTempoActive = true;

        // Find closest discrete delay time
        const int delayTimes[10] = {2400,  4800,  9600,  14400, 24000,
                                    38400, 48000, 67200, 81600, 96000};

        int closestIndex = 0;
        int minDifference = abs(tapDelayTime - delayTimes[0]);

        for (int i = 1; i < 10; i++) {
          int difference = abs(tapDelayTime - delayTimes[i]);
          if (difference < minDifference) {
            minDifference = difference;
            closestIndex = i;
          }
        }

        delayTime = delayTimes[closestIndex];
      }
    }

    lastTapTime = currentTime;
  }

  void ClearBuffer() {
    for (int i = 0; i < MAX_DELAY_SAMPLES; i++) {
      delayBuffer[i] = 0;
    }
  }

  void __not_in_flash_func(UpdateDelayClock)() {
    // Reset counter if delay time changed
    if (delayTime != lastDelayTime) {
      delayCycleCounter = 0;
      lastDelayTime = delayTime;
      PulseOut1(false);  // Ensure pulse starts low
    }

    // Only increment counter when buffer is not frozen
    if (!freezeBuffer) {
      delayCycleCounter++;

      // Send pulse every delay time worth of samples
      if (delayCycleCounter >= delayTime) {
        PulseOut1(true);  // Start pulse
        delayCycleCounter = 0;
      } else if (delayCycleCounter == 480) {  // Keep pulse high for ~10ms
        PulseOut1(false);                     // End pulse
      }
    }
  }

 public:
  AudioDelay() {
    // Allocate memory for the delay buffer
    delayBuffer = new int16_t[MAX_DELAY_SAMPLES];

    // Initialize the delay buffer to zeros
    for (int i = 0; i < MAX_DELAY_SAMPLES; i++) {
      delayBuffer[i] = 0;
    }

    // Enable normalization probe for jack detection
    EnableNormalisationProbe();
  }

  ~AudioDelay() {
    // Free the delay buffer
    delete[] delayBuffer;
  }
};

int main() {
  stdio_init_all();

  AudioDelay delay;
  delay.RunWithBootSupport();

  return 0;
}
