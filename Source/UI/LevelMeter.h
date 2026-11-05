#pragma once
// Vertical stereo peak meter with peak-hold. Feed it linear peaks once per UI tick.
#include <JuceHeader.h>

namespace el {

class LevelMeter : public juce::Component {
public:
    LevelMeter();
    // Linear peak values (0..1+) since the last call; the meter does the smoothing/decay.
    void setLevels (float left, float right);
    void resetPeaks();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override { resetPeaks(); }

    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;
private:
    struct Channel { float db = -90.0f, hold = -90.0f; int holdAge = 0; };
    Channel ch[2];
    juce::ColourGradient gradient;
    float yFor (float db, float top, float height) const noexcept;
};

} // namespace el
