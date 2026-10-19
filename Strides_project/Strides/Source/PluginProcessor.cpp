#include "PluginProcessor.h"
#include "PluginEditor.h"

StridesProcessor::StridesProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void StridesProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (capturing.load())
    {
        const int pos = capPos.load();
        const int n = juce::jmin (buffer.getNumSamples(), capMax - pos);
        if (n > 0)
            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels(), capBuf.getNumChannels()); ++ch)
                capBuf.copyFrom (ch, pos, buffer, ch, 0, n);
        capPos.store (pos + juce::jmax (0, n));
        if (pos + n >= capMax) capturing.store (false);
    }

    // plugin is a pure analyser: audio passes through untouched.
    // standalone: mute output to avoid feedback from the audio input.
    if (wrapperType == wrapperType_Standalone)
        buffer.clear();
}

void StridesProcessor::startCapture (double maxSeconds)
{
    capturing.store (false);
    capSR = sampleRate;
    capMax = (int) (sampleRate * maxSeconds);
    capBuf.setSize (2, capMax);
    capBuf.clear();
    capPos.store (0);
    capturing.store (true);
}

bool StridesProcessor::takeCapture (juce::AudioBuffer<float>& out, double& sr)
{
    capturing.store (false);
    const int n = juce::jmin (capPos.load(), capBuf.getNumSamples());
    if (n < (int) (capSR * 5)) return false;           // need at least 5 s
    out.setSize (2, n);
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, capBuf, ch, 0, n);
    sr = capSR;
    return true;
}

juce::AudioProcessorEditor* StridesProcessor::createEditor() { return new StridesEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StridesProcessor(); }
