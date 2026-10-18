#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ts::theme
{
inline const juce::Colour window { 0xff3a454b };
inline const juce::Colour workspace { 0xff27323a };
inline const juce::Colour panel { 0xff3b474e };
inline const juce::Colour panelDark { 0xff1b2329 };
inline const juce::Colour title { 0xff434f56 };
inline const juce::Colour titleActive { 0xff51606a };
inline const juce::Colour button { 0xff4a575e };
inline const juce::Colour outline { 0xff1a1e20 };
inline const juce::Colour text { 0xffd6dde0 };
inline const juce::Colour textDim { 0xff7f8b90 };
inline const juce::Colour accent { 0xffe8483b };
inline const juce::Colour led { 0xff86e04f };
inline const juce::Colour ledOff { 0xff2f3a2b };
inline const juce::Colour stepA { 0xff4c5458 };
inline const juce::Colour stepB { 0xff6a5250 };
inline const juce::Colour stepOn { 0xffdfe6e8 };
inline const juce::Colour rack { 0xff5a6368 };
inline const juce::Colour rackBox { 0xff4a5357 };
inline const juce::Colour mixerBack { 0xff22232c };
inline const juce::Colour mixerStrip { 0xff2e3040 };
inline const juce::Colour mixerHeader { 0xff4a4b96 };
inline const juce::Colour knobTop { 0xff5d676c };
inline const juce::Colour knobBottom { 0xff353c40 };
inline const juce::Colour lcd { 0xff151c20 };
inline const juce::Colour rowWhite { 0xff3a474f };
inline const juce::Colour rowBlack { 0xff323e45 };
inline const juce::Colour gridLine { 0xff2d3940 };
inline const juce::Colour beatLine { 0xff283339 };
inline const juce::Colour barLine { 0xff1b2328 };
inline const juce::Colour noteText { 0xff15191b };
inline const juce::Colour selection { 0xffffc857 };
inline const juce::Colour playhead { 0xff7be0a0 };
inline const juce::Colour meterLow { 0xff74d24a };
inline const juce::Colour meterMid { 0xffe8cf45 };
inline const juce::Colour meterHigh { 0xffe5533f };

inline juce::Font font(float height, int style = juce::Font::plain)
{
    return juce::Font(juce::FontOptions(height, style));
}
}
