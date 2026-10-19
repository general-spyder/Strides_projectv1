#include "PluginEditor.h"

using namespace strides;
using juce::Colour;
using juce::Graphics;
using juce::Justification;
using juce::Rectangle;
using juce::String;

namespace
{
const Colour cBg1 (0xff0d0e22), cBg2 (0xff1e1042), cCard (0xff1c1e3d), cText (0xfff2f3ff), cDim (0xff9a9cc4),
             cGood (0xff2ee6a6), cMid (0xffffc83d), cBad (0xffff5c7a), cPurple (0xff7c5cff), cPink (0xffff4f9a), cOrange (0xffff9f1c);

Colour simColour (float s) { return s >= 75 ? cGood : s >= 50 ? cMid : cBad; }
String verdict (float s) { return s >= 75 ? "Close to references" : s >= 50 ? "Slightly off" : "Needs work"; }

Colour statusColour (const String& s)
{
    if (s == "Fixed") return cGood;
    if (s == "Improved") return Colour (0xff9be564);
    if (s == "No change") return cMid;
    if (s == "Overcorrected") return cOrange;
    return cBad;
}
}

//==============================================================================
void ChecklistView::refresh()
{
    setSize (getWidth(), juce::jmax (120, tasks ? (int) tasks->size() * rowH : 0));
    repaint();
}

void ChecklistView::paint (Graphics& g)
{
    if (tasks == nullptr || tasks->empty())
    {
        g.setColour (cDim);
        g.setFont (Fonts::get (14));
        g.drawFittedText (emptyMessage, getLocalBounds().withHeight (110).reduced (40, 0), Justification::centred, 3);
        return;
    }
    for (size_t i = 0; i < tasks->size(); ++i)
    {
        auto& t = (*tasks)[i];
        auto r = Rectangle<int> (0, (int) i * rowH, getWidth(), rowH).reduced (0, 5);
        const auto col = metricColour (t.metric);

        g.setColour (cCard);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (col);
        g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 12.0f, 5.0f, (float) r.getHeight() - 24.0f, 2.5f);

        auto textArea = r.withTrimmedLeft (66).withTrimmedRight (reviewMode ? 190 : 20);

        if (! reviewMode)
        {
            auto box = Rectangle<float> ((float) r.getX() + 24.0f, (float) r.getCentreY() - 12.0f, 24.0f, 24.0f);
            g.setColour (t.done ? col : col.withAlpha (0.6f));
            if (t.done) g.fillRoundedRectangle (box, 7.0f); else g.drawRoundedRectangle (box, 7.0f, 2.0f);
            if (t.done)
            {
                juce::Path p;
                p.startNewSubPath (box.getX() + 6, box.getCentreY());
                p.lineTo (box.getX() + 10.5f, box.getBottom() - 7);
                p.lineTo (box.getRight() - 5, box.getY() + 7);
                g.setColour (juce::Colours::white);
                g.strokePath (p, juce::PathStrokeType (2.5f));
            }
        }
        else
        {
            auto badge = Rectangle<float> ((float) r.getRight() - 170.0f, (float) r.getY() + 16.0f, 150.0f, 30.0f);
            const auto sc = statusColour (t.status);
            g.setColour (sc.withAlpha (0.22f)); g.fillRoundedRectangle (badge, 15.0f);
            g.setColour (sc); g.setFont (Fonts::get (13, 2));
            g.drawText (t.status, badge, Justification::centred);
            const String unit = t.band >= 0 ? " dB" : " pts";
            g.setColour (cDim); g.setFont (Fonts::get (11));
            g.drawFittedText ("was " + String (t.baseDiff, 1) + "  now " + String (t.newDiff, 1) + unit,
                              Rectangle<int> (r.getRight() - 180, r.getY() + 52, 170, 30), Justification::centred, 2);
            g.setColour (col.withAlpha (0.0f));
        }

        g.setColour (t.done ? cDim : cText);
        g.setFont (Fonts::get (14, 1));
        g.drawFittedText (t.title, textArea.removeFromTop (28).withTrimmedTop (6), Justification::centredLeft, 1);
        g.setColour (cDim);
        g.setFont (Fonts::get (11.5f));
        g.drawFittedText (t.detail, textArea.removeFromTop (38), Justification::topLeft, 2, 1.0f);
        g.setColour (col);
        g.setFont (Fonts::get (11, 1));
        g.drawFittedText (t.where, textArea.removeFromTop (20), Justification::centredLeft, 1);
    }
}

void ChecklistView::mouseUp (const juce::MouseEvent& e)
{
    if (reviewMode || tasks == nullptr || e.x > 70) return;
    const int i = e.y / rowH;
    if (i >= 0 && i < (int) tasks->size())
    {
        (*tasks)[(size_t) i].done = ! (*tasks)[(size_t) i].done;
        repaint();
        if (onChange) onChange();
    }
}

//==============================================================================
StridesEditor::StridesEditor (StridesProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (1100, 760);

    const char* tabs[] = { "References", "Analyse", "Checklist", "Review" };
    for (int i = 0; i < 4; ++i)
    {
        tabBtn[i].setButtonText (tabs[i]);
        tabBtn[i].onClick = [this, i] { page = i; refreshAll(); };
        addAndMakeVisible (tabBtn[i]);
    }
    for (int i = 0; i < 5; ++i)
    {
        addBtn[i].onClick = [this, i] { chooseFile (RoleRef, i); };
        rmBtn[i].setButtonText ("Remove");
        rmBtn[i].setColour (juce::TextButton::buttonColourId, Colour (0xff3a2a4f));
        rmBtn[i].onClick = [this, i]
        {
            auto& refs = proc.session.refs;
            if (i < (int) refs.size()) refs.erase (refs.begin() + i);
            recompute(); refreshAll();
        };
        addChildComponent (addBtn[i]);
        addChildComponent (rmBtn[i]);
    }
    loadSongBtn.setButtonText ("Load your song");
    loadSongBtn.setColour (juce::TextButton::buttonColourId, cPurple);
    loadSongBtn.onClick = [this] { chooseFile (RoleSong, -1); };
    loadReviewBtn.setButtonText ("Load revised version");
    loadReviewBtn.setColour (juce::TextButton::buttonColourId, cPink);
    loadReviewBtn.onClick = [this] { chooseFile (RoleReview, -1); };
    captureBtn.setButtonText ("Capture from DAW");
    captureBtn.setColour (juce::TextButton::buttonColourId, cOrange);
    captureBtn.onClick = [this]
    {
        if (! proc.isCapturing()) { proc.startCapture (240.0); captureBtn.setButtonText ("Stop & analyse"); status = "Recording - press play in your DAW (max 4 min)"; }
        else
        {
            juce::AudioBuffer<float> b; double sr = 0;
            captureBtn.setButtonText ("Capture from DAW");
            if (proc.takeCapture (b, sr)) analyseBuffer (std::move (b), sr, "Captured from DAW", page == PageReview ? RoleReview : RoleSong);
            else status = "Capture too short - record at least 5 seconds of playback";
        }
        repaint();
    };
    printBtn.setButtonText ("Print report");
    printBtn.setColour (juce::TextButton::buttonColourId, Colour (0xff2b2f5c));
    printBtn.onClick = [this] { printReport(); };
    for (auto* b : { &loadSongBtn, &loadReviewBtn, &captureBtn, &printBtn }) addChildComponent (b);
    printBtn.setVisible (true);
    captureBtn.setVisible (false);

    checkVP.setViewedComponent (&checklist, false);
    checkVP.setScrollBarsShown (true, false);
    reviewVP.setViewedComponent (&reviewList, false);
    reviewVP.setScrollBarsShown (true, false);
    addChildComponent (checkVP);
    addChildComponent (reviewVP);
    checklist.setSize (1020, 120);
    reviewList.setSize (1020, 120);
    reviewList.reviewMode = true;
    checklist.onChange = [this] { repaint(); };

    startTimerHz (10);
    refreshAll();
}

StridesEditor::~StridesEditor() { setLookAndFeel (nullptr); }

//==============================================================================
void StridesEditor::resized()
{
    for (int i = 0; i < 4; ++i) tabBtn[i].setBounds (330 + i * 130, 22, 122, 38);
    printBtn.setBounds (getWidth() - 160, 22, 130, 38);
    for (int i = 0; i < 5; ++i)
    {
        addBtn[i].setBounds (840, 170 + i * 100 + 23, 110, 40);
        rmBtn[i].setBounds (960, 170 + i * 100 + 23, 90, 40);
    }
    loadSongBtn.setBounds (30, 105, 180, 38);
    captureBtn.setBounds (220, 105, 190, 38);
    loadReviewBtn.setBounds (30, 105, 230, 38);
    checkVP.setBounds (30, 150, 1040, 570);
    reviewVP.setBounds (30, 285, 1040, 435);
}

void StridesEditor::refreshAll()
{
    auto& s = proc.session;
    for (int i = 0; i < 4; ++i)
        tabBtn[i].setColour (juce::TextButton::buttonColourId, i == page ? cPurple : Colour (0xff25274d));

    for (int i = 0; i < 5; ++i)
    {
        const bool filled = i < (int) s.refs.size();
        addBtn[i].setButtonText (filled ? "Replace" : "Add audio");
        addBtn[i].setColour (juce::TextButton::buttonColourId, filled ? Colour (0xff2b2f5c) : cPurple);
        addBtn[i].setVisible (page == PageRefs && (filled || i == (int) s.refs.size()));
        rmBtn[i].setVisible (page == PageRefs && filled);
    }
    loadSongBtn.setVisible (page == PageAnalyse);
    captureBtn.setVisible (page == PageAnalyse && proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone);
    loadReviewBtn.setVisible (page == PageReview);
    checkVP.setVisible (page == PageChecklist);
    reviewVP.setVisible (page == PageReview);

    checklist.tasks = &s.tasks;
    checklist.emptyMessage = s.hasSong && s.hasRefs ? "Nothing major to fix - your song is very close to your references."
                                                    : "Add at least one reference and your song, then your personalised checklist appears here.";
    checklist.refresh();
    reviewList.tasks = s.hasReview ? &s.tasks : nullptr;
    reviewList.emptyMessage = s.tasks.empty() ? "Create a checklist first (References > Analyse), then load your revised mix here."
                                              : "Load your revised mix to see which checklist items you improved.";
    reviewList.refresh();
    repaint();
}

//==============================================================================
bool StridesEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;mp3")) return true;
    return false;
}

void StridesEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& path : files)
    {
        juce::File f (path);
        if (! f.hasFileExtension ("wav;mp3")) continue;
        if (page == PageReview)       loadAudio (f, RoleReview, -1);
        else if (page == PageAnalyse) loadAudio (f, RoleSong, -1);
        else if (page == PageRefs && proc.session.refs.size() < 5) loadAudio (f, RoleRef, -1);
    }
}

void StridesEditor::chooseFile (int role, int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a WAV or MP3 file", juce::File(), "*.wav;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, role, slot] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile()) loadAudio (f, role, slot);
                          });
}

void StridesEditor::loadAudio (const juce::File& f, int role, int slot)
{
    busy = true;
    status = "Analysing " + f.getFileName() + " ...";
    repaint();
    juce::Component::SafePointer<StridesEditor> safe (this);
    std::thread ([safe, f, role, slot]
    {
        Analysis a;
        String err;
        decodeAndAnalyse (f, a, err);
        juce::MessageManager::callAsync ([safe, f, role, slot, a, err]
        {
            if (safe != nullptr) safe->finishLoad (f, role, slot, a, err);
        });
    }).detach();
}

void StridesEditor::analyseBuffer (juce::AudioBuffer<float> buf, double sr, const String& name, int role)
{
    busy = true;
    status = "Analysing capture ...";
    repaint();
    juce::Component::SafePointer<StridesEditor> safe (this);
    auto shared = std::make_shared<juce::AudioBuffer<float>> (std::move (buf));
    std::thread ([safe, shared, sr, name, role]
    {
        Analysis a = analyse (*shared, sr);
        juce::MessageManager::callAsync ([safe, name, role, a]
        {
            if (safe != nullptr) safe->finishLoad (juce::File (name), role, -1, a, a.whole.valid ? String() : String ("Capture too short or silent"));
        });
    }).detach();
}

void StridesEditor::finishLoad (const juce::File& f, int role, int slot, const Analysis& a, const String& err)
{
    busy = false;
    if (err.isNotEmpty()) { status = "Error: " + err; repaint(); return; }
    auto& s = proc.session;
    const String name = f.getFileName();

    if (role == RoleRef)
    {
        Session::Ref r { name, a };
        if (slot >= 0 && slot < (int) s.refs.size()) s.refs[(size_t) slot] = r;
        else if (s.refs.size() < 5) s.refs.push_back (r);
        recompute();
        status = "Reference added: " + name;
    }
    else if (role == RoleSong)
    {
        s.song = a; s.songName = name; s.hasSong = true;
        recompute();
        status = s.hasRefs ? "Analysis complete" : "Song loaded - add at least one reference on the References tab";
        if (s.hasRefs) page = PageAnalyse;
    }
    else
    {
        s.review = a.whole; s.reviewName = name;
        if (s.hasRefs && s.hasSong)
        {
            reviewTasks (s.tasks, a.whole, s.refAvg);
            s.reviewCmp = compare (a.whole, s.refAvg);
            s.hasReview = true;
            status = "Review complete";
        }
        else status = "Analyse a song against references first";
    }
    refreshAll();
}

void StridesEditor::recompute()
{
    auto& s = proc.session;
    std::vector<Features> fs;
    for (auto& r : s.refs) fs.push_back (r.a.whole);
    s.refAvg = average (fs);
    s.hasRefs = s.refAvg.valid;
    s.hasReview = false;
    if (s.hasRefs && s.hasSong)
    {
        s.cmp = compare (s.song.whole, s.refAvg);
        s.tasks = buildTasks (s.song, s.refAvg);
    }
    else { s.cmp = {}; s.tasks.clear(); }
}

void StridesEditor::printReport()
{
    auto& s = proc.session;
    if (! (s.hasRefs && s.hasSong)) { status = "Analyse a song against references before printing"; repaint(); return; }
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Strides_Report.html");
    f.replaceWithText (buildReportHtml (s), false, false, "\n");
    f.startAsProcess();                                  // opens in the default browser, which shows the print dialog
    status = "Report opened - choose your printer or 'Save as PDF'";
    repaint();
}

//==============================================================================
void StridesEditor::paint (Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (cBg1, 0, 0, cBg2, (float) getWidth(), (float) getHeight(), false));
    g.fillAll();

    // soft colour glows
    g.setGradientFill (juce::ColourGradient (cPink.withAlpha (0.12f), (float) getWidth(), 0, juce::Colours::transparentBlack, (float) getWidth() - 450, 300, true));
    g.fillRect (getLocalBounds());

    g.setGradientFill (juce::ColourGradient (cPurple, 30, 10, cPink, 200, 60, false));
    g.setFont (Fonts::get (34, 2));
    g.drawText ("Strides", 30, 10, 220, 46, Justification::centredLeft);
    g.setColour (cDim);
    g.setFont (Fonts::get (10.5f, 1));
    g.drawText ("MIX ANALYSER", 33, 50, 200, 16, Justification::centredLeft);

    switch (page)
    {
        case PageRefs:      paintRefs (g); break;
        case PageAnalyse:   paintAnalyse (g); break;
        case PageChecklist: paintChecklistHeader (g); break;
        case PageReview:    paintReviewHeader (g); break;
    }

    // status line
    g.setColour (cDim);
    g.setFont (Fonts::get (11.5f));
    String st = status;
    if (proc.isCapturing()) st = "Recording ... " + mmss (proc.getCaptureSeconds());
    if (busy) st += String (" ").paddedRight (' ', 1) + String::repeatedString (".", 1 + (int) (juce::Time::getMillisecondCounter() / 300) % 3);
    g.drawText (st, 30, 732, 900, 20, Justification::centredLeft);
}

void StridesEditor::paintRefs (Graphics& g)
{
    auto& s = proc.session;
    g.setColour (cText);
    g.setFont (Fonts::get (20, 2));
    g.drawText ("Reference tracks  (" + String ((int) s.refs.size()) + "/5)", 30, 98, 700, 32, Justification::centredLeft);
    g.setColour (cDim);
    g.setFont (Fonts::get (12));
    g.drawText ("Add up to 5 professional tracks you want your mix to sound like. WAV or MP3 - or drag files here.", 30, 130, 900, 22, Justification::centredLeft);

    for (int i = 0; i < 5; ++i)
    {
        auto r = Rectangle<float> (30.0f, 170.0f + i * 100.0f, 1040.0f, 86.0f);
        const bool filled = i < (int) s.refs.size();
        const auto col = metricColour (i * 2);
        g.setColour (cCard);
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (filled ? col.withAlpha (0.5f) : Colour (0x22ffffff));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.2f);

        auto badge = Rectangle<float> (r.getX() + 18, r.getCentreY() - 22, 44, 44);
        g.setColour (filled ? col : Colour (0xff34365f));
        g.fillEllipse (badge);
        g.setColour (juce::Colours::white);
        g.setFont (Fonts::get (17, 2));
        g.drawText (String (i + 1), badge, Justification::centred);

        g.setColour (filled ? cText : cDim);
        g.setFont (Fonts::get (15, 1));
        g.drawText (filled ? s.refs[(size_t) i].name : "Empty slot", (int) r.getX() + 80, (int) r.getY() + 16, 700, 26, Justification::centredLeft);
        g.setColour (cDim);
        g.setFont (Fonts::get (11.5f));
        if (filled)
        {
            auto& a = s.refs[(size_t) i].a;
            g.drawText (mmss (a.duration) + " long   |   loudness " + String (a.whole.rmsDb, 1) + " dB RMS   |   width " + String ((int) a.whole.v[Width])
                        + "   |   compression " + String ((int) a.whole.v[Compression]), (int) r.getX() + 80, (int) r.getY() + 44, 740, 22, Justification::centredLeft);
        }
        else g.drawText ("Drop a file here or press Add audio", (int) r.getX() + 80, (int) r.getY() + 44, 600, 22, Justification::centredLeft);
    }
}

void StridesEditor::drawMetricCard (Graphics& g, Rectangle<float> r, int m)
{
    auto& s = proc.session;
    const auto col = metricColour (m);
    const float sim = s.cmp.sim[m];

    g.setColour (cCard);
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (col.withAlpha (0.4f));
    g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.2f);

    auto in = r.reduced (16.0f, 14.0f);
    g.setColour (col);
    g.fillEllipse (in.getX(), in.getY() + 6, 10, 10);
    g.setColour (cText);
    g.setFont (Fonts::get (15, 1));
    g.drawText (metricName (m), (int) in.getX() + 18, (int) in.getY(), (int) in.getWidth() - 18, 22, Justification::centredLeft);

    g.setColour (simColour (sim));
    g.setFont (Fonts::get (34, 2));
    g.drawText (String ((int) sim) + "%", (int) in.getX(), (int) in.getY() + 28, (int) in.getWidth(), 48, Justification::centredLeft);
    g.setFont (Fonts::get (11.5f, 1));
    g.drawText (verdict (sim), (int) in.getX(), (int) in.getY() + 76, (int) in.getWidth(), 18, Justification::centredLeft);

    const float y0 = in.getY() + 108.0f;
    if (m == EQ)
    {
        const float bw = in.getWidth() / 8.0f, mid = y0 + 34.0f;
        g.setColour (Colour (0x33ffffff));
        g.drawHorizontalLine ((int) mid, in.getX(), in.getRight());
        for (size_t b = 0; b < 8; ++b)
        {
            const float sv = juce::jlimit (-12.0f, 12.0f, s.song.whole.bands[b]) / 12.0f * 32.0f;
            const float rv = juce::jlimit (-12.0f, 12.0f, s.refAvg.bands[b]) / 12.0f * 32.0f;
            const float x = in.getX() + (float) b * bw + 3;
            g.setColour (col);
            g.fillRoundedRectangle (x, sv >= 0 ? mid - sv : mid, bw - 6, std::max (2.0f, std::abs (sv)), 3.0f);
            g.setColour (juce::Colours::white);
            g.fillRect (x - 1, mid - rv - 1, bw - 4, 2.0f);
            g.setColour (cDim);
            g.setFont (Fonts::get (8));
            g.drawText (bandShort ((int) b), (int) (x - 3), (int) (y0 + 70), (int) bw, 12, Justification::centred);
        }
    }
    else
    {
        const float w = in.getWidth();
        auto track = Rectangle<float> (in.getX(), y0 + 10, w, 10);
        g.setColour (Colour (0xff2b2d57));
        g.fillRoundedRectangle (track, 5.0f);
        g.setGradientFill (juce::ColourGradient (col.darker (0.4f), track.getX(), 0, col, track.getRight(), 0, false));
        g.fillRoundedRectangle (track.withWidth (std::max (6.0f, w * s.song.whole.v[m] / 100.0f)), 5.0f);
        const float rx = in.getX() + w * s.refAvg.v[m] / 100.0f;
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (rx - 1.5f, y0 + 2, 3.0f, 26.0f, 1.5f);
        g.setColour (cDim);
        g.setFont (Fonts::get (10.5f));
        g.drawText ("You " + String ((int) s.song.whole.v[m]), (int) in.getX(), (int) y0 + 34, 80, 16, Justification::centredLeft);
        g.drawText ("Ref " + String ((int) s.refAvg.v[m]), (int) in.getRight() - 80, (int) y0 + 34, 80, 16, Justification::centredRight);
    }
    g.setColour (cDim);
    g.setFont (Fonts::get (10));
    g.drawText (metricBlurb (m), (int) in.getX(), (int) r.getBottom() - 32, (int) in.getWidth(), 16, Justification::centredLeft);
}

void StridesEditor::paintAnalyse (Graphics& g)
{
    auto& s = proc.session;
    g.setColour (cDim);
    g.setFont (Fonts::get (12));
    g.drawText (s.hasSong ? s.songName : "No song loaded yet", 430, 105, 330, 38, Justification::centredLeft);

    if (! (s.hasSong && s.hasRefs))
    {
        g.setFont (Fonts::get (15, 1));
        g.setColour (cText);
        g.drawFittedText (s.hasRefs ? "Load your song to compare it with your references."
                                    : "Start on the References tab: add 1-5 reference tracks, then load your song here.",
                          Rectangle<int> (150, 300, 800, 120), Justification::centred, 3);
        return;
    }

    g.setColour (cDim);
    g.setFont (Fonts::get (11, 1));
    g.drawText ("OVERALL MATCH", 800, 100, 270, 16, Justification::centredRight);
    g.setColour (simColour (s.cmp.overall));
    g.setFont (Fonts::get (38, 2));
    g.drawText (String ((int) s.cmp.overall) + "%", 800, 112, 270, 48, Justification::centredRight);

    for (int m = 0; m < NumMetrics; ++m)
        drawMetricCard (g, Rectangle<float> (30.0f + (float) (m % 5) * 211.0f, 170.0f + (float) (m / 5) * 280.0f, 196.0f, 265.0f), m);
}

void StridesEditor::paintChecklistHeader (Graphics& g)
{
    auto& s = proc.session;
    int done = 0;
    for (auto& t : s.tasks) done += t.done ? 1 : 0;
    g.setColour (cText);
    g.setFont (Fonts::get (20, 2));
    g.drawText ("Improvement checklist", 30, 98, 500, 32, Justification::centredLeft);
    g.setColour (cDim);
    g.setFont (Fonts::get (12));
    g.drawText (s.tasks.empty() ? "" : String (done) + " of " + String ((int) s.tasks.size()) + " done   -   tick items as you work through them, most important first",
                30, 128, 900, 20, Justification::centredLeft);
}

void StridesEditor::paintReviewHeader (Graphics& g)
{
    auto& s = proc.session;
    g.setColour (cDim);
    g.setFont (Fonts::get (12));
    g.drawText (s.hasReview ? s.reviewName : "Load your revised mix to check whether you improved", 280, 105, 700, 38, Justification::centredLeft);
    if (! s.hasReview) return;

    int fixed = 0, improved = 0, worse = 0;
    for (auto& t : s.tasks)
    {
        if (t.status == "Fixed") ++fixed;
        else if (t.status == "Improved") ++improved;
        else if (t.status == "Worse" || t.status == "Overcorrected") ++worse;
    }
    const float delta = s.reviewCmp.overall - s.cmp.overall;
    struct Tile { String label, value; Colour c; };
    Tile tiles[4] = { { "BEFORE", String ((int) s.cmp.overall) + "%", cDim },
                      { "AFTER", String ((int) s.reviewCmp.overall) + "%", simColour (s.reviewCmp.overall) },
                      { "CHANGE", (delta >= 0 ? "+" : "") + String (delta, 1), delta >= 0 ? cGood : cBad },
                      { "FIXED / IMPROVED / WORSE", String (fixed) + " / " + String (improved) + " / " + String (worse), cText } };
    for (int i = 0; i < 4; ++i)
    {
        auto r = Rectangle<float> (30.0f + (float) i * 262.0f, 155.0f, 250.0f, 78.0f);
        g.setColour (cCard); g.fillRoundedRectangle (r, 14.0f);
        g.setColour (cDim); g.setFont (Fonts::get (10, 1));
        g.drawText (tiles[i].label, r.reduced (14, 8).removeFromTop (16).toNearestInt(), Justification::centredLeft);
        g.setColour (tiles[i].c); g.setFont (Fonts::get (28, 2));
        g.drawText (tiles[i].value, r.reduced (14, 8).withTrimmedTop (18).toNearestInt(), Justification::centredLeft);
    }
    for (int m = 0; m < NumMetrics; ++m)
    {
        auto r = Rectangle<float> (30.0f + (float) m * 104.5f, 242.0f, 98.0f, 34.0f);
        const float d = s.reviewCmp.sim[m] - s.cmp.sim[m];
        g.setColour (metricColour (m).withAlpha (0.2f)); g.fillRoundedRectangle (r, 10.0f);
        g.setColour (cText); g.setFont (Fonts::get (9.5f, 1));
        g.drawText (metricName (m), r.reduced (6, 2).removeFromTop (16).toNearestInt(), Justification::centred);
        g.setColour (d >= 0 ? cGood : cBad); g.setFont (Fonts::get (10.5f, 2));
        g.drawText ((d >= 0 ? "+" : "") + String ((int) d) + "%", r.reduced (6, 2).withTrimmedTop (15).toNearestInt(), Justification::centred);
    }
}
