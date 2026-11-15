#pragma once
#include "NativeEffects.h"
namespace auralis {
namespace nativeUI {
inline const Colour background(0xff11151b), panel(0xff19212b), ink(0xffe2e9ee), dim(0xff8d9cab), mint(0xff80e3c0), amber(0xffffbd79);
inline void style(LookAndFeel_V4& theme) {
    theme.setColour(ResizableWindow::backgroundColourId, background); theme.setColour(Label::textColourId, ink);
    theme.setColour(Slider::thumbColourId, mint); theme.setColour(Slider::trackColourId, mint);
    theme.setColour(Slider::textBoxTextColourId, ink); theme.setColour(Slider::textBoxBackgroundColourId, panel);
    theme.setColour(TextButton::buttonColourId, Colour(0xff29333e)); theme.setColour(TextButton::textColourOffId, ink);
    theme.setColour(ComboBox::backgroundColourId, panel); theme.setColour(ComboBox::textColourId, ink); theme.setColour(ToggleButton::textColourId, ink);
}
inline void slider(Slider& s, const char* id, double low, double high, double interval, const String& suffix) {
    s.setComponentID(id); s.setRange(low, high, interval); s.setSliderStyle(Slider::LinearHorizontal);
    s.setTextBoxStyle(Slider::TextBoxRight, false, 94, 26); s.setTextValueSuffix(suffix); s.setWantsKeyboardFocus(false);
}
inline bool editing(const Slider& s) { return s.isMouseButtonDown(true) || s.hasKeyboardFocus(true); }
}
}
