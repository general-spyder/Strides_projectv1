#pragma once
#include "Strides.h"

namespace strides
{
enum Metric { Depth = 0, Width, EQ, Balance, Reverb, Delays, Compression, Saturation, Excitement, Panning, NumMetrics };

inline const char* metricName (int m)
{
    static const char* n[] = { "Depth", "Width", "EQ", "Balance", "Reverb", "Delays", "Compression", "Saturation", "Excitement", "Panning" };
    return n[m];
}
inline const char* metricBlurb (int m)
{
    static const char* n[] = { "Front-to-back space", "Stereo size of the mix", "Tonal balance, 8 bands", "Left / right level",
                               "Length of ambience tails", "Echo & repeat content", "Dynamic control", "Harmonic warmth / grit",
                               "Top-end energy & bite", "Spread of sounds across the field" };
    return n[m];
}
inline juce::Colour metricColour (int m)
{
    static const juce::uint32 c[] = { 0xff7c5cff, 0xff29c7ff, 0xff2ee6a6, 0xffffc83d, 0xffff7a59,
                                      0xffff4f9a, 0xff9be564, 0xffff9f1c, 0xffc05cff, 0xff4d8dff };
    return juce::Colour (c[m]);
}
inline const char* bandName (int b)
{
    static const char* n[] = { "Sub (20-60 Hz)", "Bass (60-250 Hz)", "Low-mid (250-500 Hz)", "Mid (500 Hz-2 kHz)",
                               "Upper-mid (2-4 kHz)", "Presence (4-8 kHz)", "Brilliance (8-12 kHz)", "Air (12-20 kHz)" };
    return n[b];
}
inline const char* bandShort (int b)
{
    static const char* n[] = { "Sub", "Bass", "LoMid", "Mid", "HiMid", "Pres", "Brill", "Air" };
    return n[b];
}
inline float bandCentre (int b)
{
    static const float c[] = { 40, 125, 350, 1000, 3000, 6000, 10000, 15000 };
    return c[b];
}
inline juce::String mmss (double s)
{
    const int t = (int) s;
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}

struct Features
{
    bool valid = false;
    float v[NumMetrics] = {};            // each metric on a 0..100 index
    std::array<float, 8> bands {};       // dB relative to the mean of all bands
    float rmsDb = -100.0f, crestDb = 0.0f;
};

struct Analysis
{
    Features whole;
    std::vector<Features> segs;          // one per segSeconds
    double segSeconds = 8.0, duration = 0.0;
};

struct Comparison
{
    float sim[NumMetrics] = {};          // 0..100 similarity
    float diff[NumMetrics] = {};         // song - reference (EQ: mean abs dB difference)
    float overall = 0.0f;
    bool valid = false;
};

struct Task
{
    int metric = 0, band = -1;           // band >= 0 for EQ band tasks
    juce::String title, detail, where;
    float baseDiff = 0.0f, doneThreshold = 0.0f, severity = 0.0f;
    bool done = false;
    // review results
    bool reviewed = false;
    float newDiff = 0.0f;
    juce::String status;
};

struct Session
{
    struct Ref { juce::String name; Analysis a; };
    std::vector<Ref> refs;
    Features refAvg;
    bool hasRefs = false;

    juce::String songName;
    Analysis song;
    bool hasSong = false;
    Comparison cmp;
    std::vector<Task> tasks;

    juce::String reviewName;
    Features review;
    Comparison reviewCmp;
    bool hasReview = false;
};

float tolerance (int metric);
float signedDiff (const Features& song, const Features& ref, int metric, int band = -1);

Analysis analyse (const juce::AudioBuffer<float>& buffer, double sampleRate);
bool decodeAndAnalyse (const juce::File& file, Analysis& out, juce::String& error);   // WAV + MP3
Features average (const std::vector<Features>& fs);
Comparison compare (const Features& song, const Features& ref);
std::vector<Task> buildTasks (const Analysis& song, const Features& refAvg);
void reviewTasks (std::vector<Task>& tasks, const Features& revised, const Features& refAvg);
juce::String buildReportHtml (const Session& s);
}
