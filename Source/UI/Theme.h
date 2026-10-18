#pragma once

#include <juce_graphics/juce_graphics.h>

namespace ts::theme
{
inline const juce::Colour window { 0xff31383c };
inline const juce::Colour workspace { 0xff1f2427 };
inline const juce::Colour panel { 0xff3a4246 };
inline const juce::Colour panelDark { 0xff272d30 };
inline const juce::Colour title { 0xff485257 };
inline const juce::Colour titleActive { 0xff56626a };
inline const juce::Colour button { 0xff4a5357 };
inline const juce::Colour outline { 0xff1a1e20 };
inline const juce::Colour text { 0xffd6dde0 };
inline const juce::Colour textDim { 0xff7f8b90 };
inline const juce::Colour accent { 0xffe8483b };
inline const juce::Colour led { 0xff86e04f };
inline const juce::Colour ledOff { 0xff2f3a2b };
inline const juce::Colour stepA { 0xff545d62 };
inline const juce::Colour stepB { 0xff5e474b };
inline const juce::Colour stepOn { 0xffe4eaec };
inline const juce::Colour knobTop { 0xff5d676c };
inline const juce::Colour knobBottom { 0xff353c40 };
inline const juce::Colour lcd { 0xff161a1c };
inline const juce::Colour rowWhite { 0xff3d454a };
inline const juce::Colour rowBlack { 0xff343b3f };
inline const juce::Colour gridLine { 0xff30373b };
inline const juce::Colour beatLine { 0xff2a3034 };
inline const juce::Colour barLine { 0xff1d2124 };
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
