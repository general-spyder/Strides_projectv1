#pragma once
#include "Strides.h"
#include "Analyzer.h"

class StridesProcessor : public juce::AudioProcessor
{
public:
    StridesProcessor();

    void prepareToPlay (double sr, int) override { sampleRate = sr; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainInputChannelSet() == l.getMainOutputChannelSet();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Strides"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // capture what the DAW plays through the plugin (VST3 mode)
    void startCapture (double maxSeconds);
    bool isCapturing() const { return capturing.load(); }
    double getCaptureSeconds() const { return capSR > 0 ? capPos.load() / capSR : 0.0; }
    bool takeCapture (juce::AudioBuffer<float>& out, double& sr);

    strides::Session session;      // message-thread only

private:
    double sampleRate = 44100.0, capSR = 0.0;
    juce::AudioBuffer<float> capBuf;
    std::atomic<int> capPos { 0 };
    std::atomic<bool> capturing { false };
    int capMax = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StridesProcessor)
};
