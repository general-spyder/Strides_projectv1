#pragma once
#include "PluginProcessor.h"

//==============================================================================
struct StridesLnF : public juce::LookAndFeel_V4
{
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return strides::Fonts::get (13.5f, 1); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        auto col = c.withMultipliedBrightness (down ? 0.85f : over ? 1.15f : 1.0f);
        g.setGradientFill (juce::ColourGradient (col, r.getTopLeft(), col.withHue (col.getHue() + 0.08f), r.getBottomRight(), false));
        g.fillRoundedRectangle (r, 12.0f);
    }
};

//==============================================================================
struct ChecklistView : public juce::Component
{
    std::vector<strides::Task>* tasks = nullptr;
    bool reviewMode = false;
    juce::String emptyMessage;
    static constexpr int rowH = 104;
    std::function<void()> onChange;

    void refresh();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

//==============================================================================
class StridesEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit StridesEditor (StridesProcessor&);
    ~StridesEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    enum Page { PageRefs, PageAnalyse, PageChecklist, PageReview };
    enum Role { RoleRef, RoleSong, RoleReview };

    StridesProcessor& proc;
    StridesLnF lnf;
    int page = PageRefs;
    bool busy = false;
    juce::String status;

    juce::TextButton tabBtn[4], addBtn[5], rmBtn[5], loadSongBtn, captureBtn, loadReviewBtn, printBtn;
    ChecklistView checklist, reviewList;
    juce::Viewport checkVP, reviewVP;
    std::unique_ptr<juce::FileChooser> chooser;

    void timerCallback() override { if (busy || proc.isCapturing()) repaint(); }
    void chooseFile (int role, int slot);
    void loadAudio (const juce::File&, int role, int slot);
    void finishLoad (const juce::File&, int role, int slot, const strides::Analysis&, const juce::String& err);
    void analyseBuffer (juce::AudioBuffer<float> buf, double sr, const juce::String& name, int role);
    void recompute();
    void refreshAll();
    void printReport();

    void paintRefs (juce::Graphics&);
    void paintAnalyse (juce::Graphics&);
    void paintChecklistHeader (juce::Graphics&);
    void paintReviewHeader (juce::Graphics&);
    void drawMetricCard (juce::Graphics&, juce::Rectangle<float>, int metric);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StridesEditor)
};
