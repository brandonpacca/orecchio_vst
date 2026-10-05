#pragma once

#include <juce_core/juce_core.h>

// juce::String (const char*) accetta solo ASCII: i testi italiani con accenti passano da qui.
inline juce::String txt (const char* utf8) { return juce::String::fromUTF8 (utf8); }
inline juce::String txt (const std::string& utf8) { return juce::String::fromUTF8 (utf8.c_str()); }
