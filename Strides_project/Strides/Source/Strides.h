#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <thread>
#include <cmath>
#include <algorithm>

#ifndef STRIDES_HAS_FONTS
 #define STRIDES_HAS_FONTS 0
#endif
#if STRIDES_HAS_FONTS
 #include <BinaryData.h>
#endif

namespace strides
{
// ---- Poppins font loader (embedded if present, otherwise installed Poppins, otherwise fallback)
struct Fonts
{
    static juce::Typeface::Ptr& face (int weight)
    {
        static juce::Typeface::Ptr f[3];
        static bool loaded = false;
        if (! loaded)
        {
            loaded = true;
           #if STRIDES_HAS_FONTS
            const char* names[3] = { "Poppins-Regular.ttf", "Poppins-SemiBold.ttf", "Poppins-Bold.ttf" };
            for (int w = 0; w < 3; ++w)
                for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
                    if (juce::String (BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i])) == names[w])
                    {
                        int size = 0;
                        auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
                        f[w] = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
                    }
           #endif
        }
        return f[juce::jlimit (0, 2, weight)];
    }

    // weight: 0 regular, 1 semibold, 2 bold
    static juce::Font get (float height, int weight = 0)
    {
        auto& t = face (weight);
        if (t != nullptr)
            return juce::Font (juce::FontOptions (t).withHeight (height));
        return juce::Font (juce::FontOptions ("Poppins", height, weight == 0 ? juce::Font::plain : juce::Font::bold));
    }
};
}
