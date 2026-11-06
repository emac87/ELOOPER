#pragma once
// Shared colours and metrics for the black ELooper UI.
#include <JuceHeader.h>

namespace el::theme {
    inline const juce::Colour background   { 0xff000000 };   // main black screen
    inline const juce::Colour panel        { 0xff0d0d0f };   // mixer / automation panels
    inline const juce::Colour panelRaised  { 0xff17171b };
    inline const juce::Colour outline      { 0xff2a2a30 };
    inline const juce::Colour text         { 0xffe8e8ea };
    inline const juce::Colour textDim      { 0xff8a8a92 };
    inline const juce::Colour accent       { 0xff40c4ff };
    inline const juce::Colour record       { 0xffff3b30 };
    inline const juce::Colour play         { 0xff34c759 };
    inline const juce::Colour overdub      { 0xffff9f0a };
    inline const juce::Colour armed        { 0xffffd60a };   // pending / quantise-waiting (blink)
    inline const juce::Colour meterLow     { 0xff34c759 };
    inline const juce::Colour meterMid     { 0xffffd60a };
    inline const juce::Colour meterHigh    { 0xffff3b30 };
    constexpr int   transportHeight = 64;
    constexpr int   stripWidth      = 104;
    constexpr int   loopCellSize    = 96;
    constexpr float cornerRadius    = 6.0f;
    constexpr int   uiRefreshHz     = 30;
    inline juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
}
