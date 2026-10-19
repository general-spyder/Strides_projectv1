#include "Analyzer.h"

namespace strides
{
static float clamp100 (float x) { return juce::jlimit (0.0f, 100.0f, x); }

float tolerance (int m)
{
    static const float t[] = { 25, 25, 6, 15, 25, 25, 25, 25, 25, 25 };   // EQ in dB, rest in index points
    return t[m];
}

float signedDiff (const Features& s, const Features& r, int m, int band)
{
    if (m == EQ)
    {
        if (band >= 0) return s.bands[(size_t) band] - r.bands[(size_t) band];
        float a = 0;
        for (size_t i = 0; i < 8; ++i) a += std::abs (s.bands[i] - r.bands[i]);
        return a / 8.0f;
    }
    return s.v[m] - r.v[m];
}

//==============================================================================
static Features analyseRange (const float* L, const float* R, int n, double sr)
{
    Features f;
    constexpr int order = 12, N = 1 << order, hop = N / 2;
    if (n < N * 3) return f;

    // ---------- time-domain pass (20 ms frames)
    const int frameLen = juce::jmax (1, (int) (sr * 0.02));
    const double fd = frameLen / sr;
    const int nFrames = n / frameLen;
    std::vector<float> env ((size_t) nFrames), pw ((size_t) nFrames);
    double eL = 0, eR = 0, eM = 0, eS = 0;
    float peak = 0;
    long clip = 0;
    for (int fr = 0; fr < nFrames; ++fr)
    {
        double sm = 0;
        for (int i = 0; i < frameLen; ++i)
        {
            const int idx = fr * frameLen + i;
            const float l = L[idx], r = R[idx], m = 0.5f * (l + r), s = 0.5f * (l - r);
            eL += l * l; eR += r * r; eM += m * m; eS += s * s; sm += m * m;
            peak = std::max ({ peak, std::abs (l), std::abs (r) });
            if (std::abs (l) > 0.995f || std::abs (r) > 0.995f) ++clip;
        }
        pw[(size_t) fr]  = (float) (sm / frameLen);
        env[(size_t) fr] = 10.0f * std::log10 (pw[(size_t) fr] + 1e-12f);
    }
    const double nUsed = (double) nFrames * frameLen;
    f.rmsDb   = (float) (10.0 * std::log10 ((eL + eR) / (2.0 * nUsed) + 1e-12));
    f.crestDb = (float) (20.0 * std::log10 (peak + 1e-9f)) - f.rmsDb;

    // Width (side vs mid), Balance (L vs R)
    const float ratio = (float) (std::sqrt (eS) / (std::sqrt (eM) + std::sqrt (eS) + 1e-9));
    f.v[Width] = clamp100 (ratio * 200.0f);
    const float lrDb = (float) (10.0 * std::log10 ((eL + 1e-12) / (eR + 1e-12)));
    f.v[Balance] = clamp100 (50.0f + lrDb * 10.0f);           // >50 = left heavy

    // Compression: spread of 400 ms loudness
    {
        std::vector<float> st;
        const int W = 20;
        for (int i = 0; i + W <= nFrames; i += 5)
        {
            double a = 0;
            for (int j = 0; j < W; ++j) a += pw[(size_t) (i + j)];
            const float db = 10.0f * std::log10 ((float) (a / W) + 1e-12f);
            if (db > -60.0f) st.push_back (db);
        }
        if (st.size() >= 3)
        {
            std::sort (st.begin(), st.end());
            const float p10 = st[(size_t) (st.size() * 0.10)];
            const float p95 = st[std::min (st.size() - 1, (size_t) (st.size() * 0.95))];
            f.v[Compression] = clamp100 (100.0f - (p95 - p10) * 5.0f);
        }
        else f.v[Compression] = 50.0f;
    }

    // Depth / Reverb: how much of the signal is in decays and how slowly it decays
    {
        int valid = 0, inRun = 0, runs = 0;
        double slopeSum = 0;
        for (int i = 0; i < nFrames; ++i) if (env[(size_t) i] > -60.0f) ++valid;
        int i = 1;
        while (i < nFrames)
        {
            if (env[(size_t) i] > -60.0f && env[(size_t) i] < env[(size_t) i - 1])
            {
                int j = i;
                while (j + 1 < nFrames && env[(size_t) j + 1] < env[(size_t) j] && env[(size_t) j + 1] > -60.0f) ++j;
                const int len = j - i + 1;
                if (len >= 3) { inRun += len; slopeSum += (env[(size_t) i - 1] - env[(size_t) j]) / (len * fd); ++runs; }
                i = j + 1;
            }
            else ++i;
        }
        f.v[Depth]  = valid > 0 ? clamp100 (200.0f * inRun / (float) valid) : 0.0f;
        f.v[Reverb] = clamp100 (100.0f - (runs ? (float) (slopeSum / runs) : 100.0f) * 0.7f);
    }

    // Delays: repeating onset structure at 100-750 ms
    {
        std::vector<float> on ((size_t) nFrames, 0.0f);
        double mean = 0;
        for (int i = 1; i < nFrames; ++i)
            if (env[(size_t) i] > -60.0f) { on[(size_t) i] = std::max (0.0f, env[(size_t) i] - env[(size_t) i - 1]); mean += on[(size_t) i]; }
        mean /= std::max (1, nFrames);
        double r0 = 1e-9;
        for (auto& x : on) { x -= (float) mean; r0 += (double) x * x; }
        const int lagMin = std::max (1, (int) (0.10 / fd)), lagMax = std::min ((int) (0.75 / fd), nFrames / 2);
        double best = 0;
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            double r = 0;
            for (int i = 0; i + lag < nFrames; ++i) r += (double) on[(size_t) i] * on[(size_t) (i + lag)];
            best = std::max (best, r / r0);
        }
        f.v[Delays] = clamp100 ((float) best * 150.0f);
    }

    // ---------- spectral pass
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N), dL ((size_t) N * 2), dR ((size_t) N * 2), prev ((size_t) N / 2, 0.0f);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (N - 1));

    const double nyq = sr * 0.5;
    auto bin = [&] (double hz) { return juce::jlimit (1, N / 2 - 1, (int) std::lround (hz * N / sr)); };
    const double edges[9] = { 20, 60, 250, 500, 2000, 4000, 8000, 12000, std::min (20000.0, nyq * 0.98) };
    int lo[8], hi[8];
    for (int b = 0; b < 8; ++b) { lo[b] = bin (edges[b]); hi[b] = std::max (lo[b] + 1, bin (edges[b + 1])); }
    const int flatLo = bin (500), flatHi = bin (std::min (10000.0, nyq * 0.9));
    const int hfLo = bin (6000), hfHi = bin (std::min (16000.0, nyq * 0.98));
    const int panLo = bin (100), panHi = bin (std::min (10000.0, nyq * 0.9));

    double bandPow[8] = {}, hfPow = 0, totPow = 0, flatSum = 0, fluxSum = 0, panSum = 0, panW = 0;
    int frames = 0;

    for (int pos = 0; pos + N <= n; pos += hop)
    {
        double e = 0;
        for (int i = 0; i < N; ++i)
        {
            dL[(size_t) i] = L[pos + i] * win[(size_t) i];
            dR[(size_t) i] = R[pos + i] * win[(size_t) i];
            e += (double) L[pos + i] * L[pos + i];
        }
        if (e < 1e-6 * N) continue;                                  // skip silence
        std::fill (dL.begin() + N, dL.end(), 0.0f);
        std::fill (dR.begin() + N, dR.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (dL.data());
        fft.performFrequencyOnlyForwardTransform (dR.data());

        double frameTot = 0, flux = 0, magSum = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            const float a = dL[(size_t) k], b = dR[(size_t) k], p = 0.5f * (a * a + b * b), mm = 0.5f * (a + b);
            frameTot += p;
            magSum += mm;
            flux += std::max (0.0f, mm - prev[(size_t) k]);
            prev[(size_t) k] = mm;
            if (k >= panLo && k < panHi) { const float w = a + b; panSum += w * std::abs ((b - a) / (a + b + 1e-9f)); panW += w; }
            if (k >= hfLo && k < hfHi) hfPow += p;
        }
        for (int b = 0; b < 8; ++b)
            for (int k = lo[b]; k < hi[b]; ++k)
            {
                const float a = dL[(size_t) k], c = dR[(size_t) k];
                bandPow[b] += 0.5f * (a * a + c * c);
            }
        double logSum = 0, sum = 0;
        for (int k = flatLo; k < flatHi; ++k) { const float mm = 0.5f * (dL[(size_t) k] + dR[(size_t) k]); logSum += std::log (mm + 1e-9); sum += mm; }
        const int cnt = std::max (1, flatHi - flatLo);
        flatSum += std::exp (logSum / cnt) / (sum / cnt + 1e-9);
        fluxSum += flux / (magSum + 1e-9);
        totPow += frameTot;
        ++frames;
    }
    if (frames == 0) return f;

    double meanDb = 0;
    float db[8];
    for (int b = 0; b < 8; ++b)
    {
        db[b] = (float) (10.0 * std::log10 (bandPow[b] / (frames * (double) (hi[b] - lo[b])) + 1e-12));
        meanDb += db[b] / 8.0;
    }
    for (size_t b = 0; b < 8; ++b) f.bands[b] = db[b] - (float) meanDb;

    float lowAvg = 0, highAvg = 0;
    for (size_t b = 0; b < 4; ++b) { lowAvg += f.bands[b] / 4; highAvg += f.bands[b + 4] / 4; }
    f.v[EQ] = clamp100 (50.0f + (highAvg - lowAvg) * 2.5f);               // overall tilt, for display

    f.v[Panning] = clamp100 ((float) (panSum / (panW + 1e-9)) * 300.0f);
    const float clipFrac = (float) clip / (float) (nUsed * 2);
    f.v[Saturation] = clamp100 ((float) (flatSum / frames) * 300.0f + std::min (20.0f, clipFrac * 2000.0f));
    const float hfDb = (float) (10.0 * std::log10 (hfPow / (totPow + 1e-12) + 1e-12));
    f.v[Excitement] = clamp100 (0.6f * clamp100 ((hfDb + 50.0f) * 4.0f) + 0.4f * clamp100 ((float) (fluxSum / frames) * 150.0f));

    f.valid = true;
    return f;
}

Analysis analyse (const juce::AudioBuffer<float>& buf, double sr)
{
    Analysis a;
    const int n = buf.getNumSamples();
    if (n <= 0 || sr <= 0 || buf.getNumChannels() < 1) return a;
    const float* L = buf.getReadPointer (0);
    const float* R = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : L;
    a.duration = n / sr;
    a.whole = analyseRange (L, R, n, sr);
    const int segLen = (int) (a.segSeconds * sr);
    for (int start = 0; start < n; start += segLen)
    {
        const int len = std::min (segLen, n - start);
        a.segs.push_back (len >= (int) (3 * sr) ? analyseRange (L + start, R + start, len, sr) : Features());
    }
    return a;
}

bool decodeAndAnalyse (const juce::File& file, Analysis& out, juce::String& err)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();                       // WAV, AIFF, FLAC, OGG and (via JUCE_USE_MP3AUDIOFORMAT) MP3
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) { err = "Could not open " + file.getFileName() + " (use WAV or MP3)"; return false; }
    if (r->lengthInSamples > (juce::int64) (r->sampleRate * 60 * 12)) { err = "File is longer than 12 minutes"; return false; }
    juce::AudioBuffer<float> buf ((int) juce::jmin<juce::uint32> (2, r->numChannels), (int) r->lengthInSamples);
    r->read (&buf, 0, buf.getNumSamples(), 0, true, true);
    out = analyse (buf, r->sampleRate);
    if (! out.whole.valid) { err = "Audio is too short or silent"; return false; }
    return true;
}

Features average (const std::vector<Features>& fs)
{
    Features o;
    int c = 0;
    for (auto& f : fs)
    {
        if (! f.valid) continue;
        ++c;
        for (int m = 0; m < NumMetrics; ++m) o.v[m] += f.v[m];
        for (size_t b = 0; b < 8; ++b) o.bands[b] += f.bands[b];
        o.rmsDb += f.rmsDb; o.crestDb += f.crestDb;
    }
    if (c == 0) return o;
    for (int m = 0; m < NumMetrics; ++m) o.v[m] /= (float) c;
    for (auto& b : o.bands) b /= (float) c;
    o.rmsDb /= (float) c; o.crestDb /= (float) c;
    o.valid = true;
    return o;
}

Comparison compare (const Features& s, const Features& r)
{
    Comparison c;
    if (! s.valid || ! r.valid) return c;
    float tot = 0;
    for (int m = 0; m < NumMetrics; ++m)
    {
        c.diff[m] = signedDiff (s, r, m);
        c.sim[m] = 100.0f * juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (c.diff[m]) / tolerance (m));
        tot += c.sim[m];
    }
    c.overall = tot / NumMetrics;
    c.valid = true;
    return c;
}

//==============================================================================
static void advice (int m, float d, juce::String& title, juce::String& detail)
{
    const bool low = d < 0;
    static const char* lowT[]  = { "flatter and more upfront than your references", "narrower than your references", "", "shifted to the right vs. your references",
                                   "drier than your references", "has fewer echo repeats than your references", "more dynamic (less compressed) than your references",
                                   "cleaner than your references", "duller than your references", "more centre-heavy than your references" };
    static const char* highT[] = { "deeper / washier than your references", "wider than your references", "", "shifted to the left vs. your references",
                                   "wetter, with longer tails than your references", "has more echo than your references", "more compressed than your references",
                                   "grittier than your references", "brighter / harsher than your references", "more spread out than your references" };
    static const char* lowF[] = {
        "Add a short room or plate reverb on a send, roll off highs on the reverb return so it sits behind the dry sound, and automate extra reverb on phrase ends.",
        "Double-track or stereo-widen pads, guitars and synths, add a short Haas delay on non-critical parts, or use a stereo imager above ~200 Hz. Keep bass and kick mono.",
        "",
        "Re-check the pan of each element and trim the louder side; use a utility/gain plugin on the master only as a last resort.",
        "Add a plate or hall on vocals and snare. Use 1.2-2.5 s decay with 20-40 ms pre-delay so clarity is kept.",
        "Add a tempo-synced delay (1/8 or 1/4 dotted) on the lead vocal or main melody; high-cut and low-cut the repeats and keep the feedback low.",
        "Use gentle bus compression (2:1 to 4:1, 1-3 dB gain reduction) on drums and the mix bus, or add parallel compression to bring up the body.",
        "Add tape or tube saturation on the drum bus, bass and vocals. Start subtle (5-15% mix) and compare against the references.",
        "Add a high-shelf boost of 1-2 dB around 8-10 kHz, use an exciter or harmonic enhancer on top-end elements and sharpen transients.",
        "Pan supporting parts (double-tracked guitars, percussion, backing vocals) 30-80% left/right while keeping kick, bass, lead vocal and snare centred." };
    static const char* highF[] = {
        "Shorten reverb decay, cut reverb sends on busy sections, and add pre-delay so key sounds stay forward.",
        "Check mono compatibility, narrow the low end below 150 Hz, and reduce stereo wideners or chorus effects on the main elements.",
        "",
        "Re-check the pan of each element and trim the louder side; use a utility/gain plugin on the master only as a last resort.",
        "Shorten the decay, high-pass and low-pass the reverb return, or duck the reverb with a compressor keyed from the dry signal.",
        "Reduce delay feedback and send level, filter the repeats, and automate the delay to only throw on phrase ends.",
        "Back off compressor ratio / threshold, slow the attack to let transients through, and check the limiter is not doing too much.",
        "Reduce saturation drive or mix, check for clipping on the master, and use oversampling on distortion plugins.",
        "Tame 3-8 kHz with a dynamic EQ or de-esser, reduce exciter amount, and check cymbals and vocal sibilance.",
        "Bring some hard-panned elements closer to centre and make sure the core of the mix (kick, bass, vocal) is anchored in the middle." };

    juce::String amt = m == Balance ? juce::String (std::abs (d) / 10.0f, 1) + " dB"
                                    : juce::String ((int) std::abs (d)) + " pts";
    title  = juce::String (metricName (m)) + ": " + (low ? lowT[m] : highT[m]) + "  (" + amt + ")";
    detail = low ? lowF[m] : highF[m];
}

static juce::String whereText (const Analysis& song, const Features& ref, int metric, int band, float baseDiff, float thr)
{
    const int n = (int) song.segs.size();
    if (n == 0) return "Across the whole song";
    std::vector<bool> flag ((size_t) n, false);
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto& s = song.segs[(size_t) i];
        if (! s.valid) continue;
        const float d = signedDiff (s, ref, metric, band);
        if (std::abs (d) > thr && d * baseDiff > 0) { flag[(size_t) i] = true; ++count; }
    }
    if (count == 0) return "Overall tendency - not tied to one section";
    if (count >= 0.8 * n) return "Throughout most of the song";
    juce::StringArray parts;
    int i = 0;
    while (i < n && parts.size() < 4)
    {
        if (! flag[(size_t) i]) { ++i; continue; }
        int j = i;
        while (j + 1 < n && flag[(size_t) j + 1]) ++j;
        parts.add (mmss (i * song.segSeconds) + " - " + mmss (std::min (song.duration, (j + 1) * song.segSeconds)));
        i = j + 1;
    }
    return "Focus on: " + parts.joinIntoString ("   |   ");
}

std::vector<Task> buildTasks (const Analysis& song, const Features& ref)
{
    std::vector<Task> tasks;
    if (! song.whole.valid || ! ref.valid) return tasks;

    for (int m = 0; m < NumMetrics; ++m)
    {
        if (m == EQ) continue;
        const float d = signedDiff (song.whole, ref, m);
        const float thr = tolerance (m) * 0.4f;
        if (std::abs (d) <= thr) continue;
        Task t;
        t.metric = m; t.baseDiff = d; t.doneThreshold = thr; t.severity = std::abs (d) / tolerance (m);
        advice (m, d, t.title, t.detail);
        t.where = whereText (song, ref, m, -1, d, thr * 1.5f);
        tasks.push_back (t);
    }

    static const char* tip[] = {
        "Check mono below 100 Hz and high-pass non-bass instruments.",
        "Kick/bass weight lives here - balance them against each other with complementary EQ.",
        "This is the 'mud' zone - wide cuts around 250-400 Hz often clean up a mix.",
        "Body of vocals, guitars and snare - watch for boxiness around 500-800 Hz.",
        "Vocal clarity and aggression - small moves (+/-2 dB) matter a lot here.",
        "Definition and sibilance region - use a de-esser if you add energy here.",
        "Sparkle and cymbals - a gentle shelf or exciter works well; watch for hiss.",
        "Openness and air - a high shelf above 12 kHz adds polish." };

    for (int b = 0; b < 8; ++b)
    {
        const float d = signedDiff (song.whole, ref, EQ, b);
        if (std::abs (d) <= 3.0f) continue;
        Task t;
        t.metric = EQ; t.band = b; t.baseDiff = d; t.doneThreshold = 2.0f; t.severity = std::abs (d) / 6.0f;
        t.title = juce::String ("EQ: ") + bandName (b) + " is " + juce::String (std::abs (d), 1) + (d > 0 ? " dB louder" : " dB quieter") + " than your references";
        t.detail = juce::String (d > 0 ? "Try a wide bell cut of 1-3 dB around " : "Try a wide bell boost of 1-3 dB around ")
                   + juce::String ((int) bandCentre (b)) + " Hz, or carve space on competing instruments. " + tip[b];
        t.where = whereText (song, ref, EQ, b, d, 4.0f);
        tasks.push_back (t);
    }

    std::sort (tasks.begin(), tasks.end(), [] (const Task& a, const Task& b) { return a.severity > b.severity; });
    if (tasks.size() > 14) tasks.resize (14);
    return tasks;
}

void reviewTasks (std::vector<Task>& tasks, const Features& rev, const Features& ref)
{
    for (auto& t : tasks)
    {
        t.reviewed = true;
        t.newDiff = signedDiff (rev, ref, t.metric, t.band);
        const float a = std::abs (t.baseDiff), n = std::abs (t.newDiff);
        const bool flipped = t.newDiff * t.baseDiff < 0;
        if (n <= t.doneThreshold)               t.status = "Fixed";
        else if (flipped && n > a * 0.9f)       t.status = "Overcorrected";
        else if (n < a * 0.85f)                 t.status = "Improved";
        else if (n > a * 1.15f)                 t.status = "Worse";
        else                                    t.status = "No change";
    }
}

//==============================================================================
static juce::String esc (juce::String s) { return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;"); }

static juce::String bar (float pct, const juce::String& colour)
{
    return "<div class='bar'><div style='width:" + juce::String (juce::jlimit (0.0f, 100.0f, pct), 0) + "%;background:" + colour + "'></div></div>";
}

juce::String buildReportHtml (const Session& s)
{
    juce::String h;
    h << "<!doctype html><html><head><meta charset='utf-8'><title>Strides Report</title>"
         "<link href='https://fonts.googleapis.com/css2?family=Poppins:wght@400;600;700&display=swap' rel='stylesheet'>"
         "<style>body{font-family:'Poppins',Segoe UI,Arial,sans-serif;margin:32px;color:#1b1c36}"
         "h1{font-size:34px;margin:0;background:linear-gradient(90deg,#7c5cff,#ff4f9a,#ff9f1c);-webkit-background-clip:text;color:transparent;-webkit-print-color-adjust:exact}"
         "h2{margin-top:30px;border-bottom:2px solid #eee;padding-bottom:6px}table{width:100%;border-collapse:collapse}"
         "td,th{padding:7px 8px;text-align:left;font-size:13px;border-bottom:1px solid #eee}"
         ".bar{background:#eceefa;border-radius:6px;height:10px;width:100%;-webkit-print-color-adjust:exact;print-color-adjust:exact}"
         ".bar div{height:10px;border-radius:6px;-webkit-print-color-adjust:exact;print-color-adjust:exact}"
         ".big{font-size:40px;font-weight:700}.tag{display:inline-block;padding:2px 10px;border-radius:12px;color:#fff;font-size:11px;-webkit-print-color-adjust:exact;print-color-adjust:exact}"
         ".task{padding:10px 0;border-bottom:1px solid #eee;font-size:13px}.box{display:inline-block;width:14px;height:14px;border:2px solid #7c5cff;border-radius:3px;margin-right:8px;vertical-align:middle}"
         ".dim{color:#777;font-size:12px}@media print{body{margin:12mm}}</style></head><body>";
    h << "<h1>Strides</h1><div class='dim'>Mix analysis report &middot; " << esc (juce::Time::getCurrentTime().toString (true, true)) << "</div>";
    h << "<h2>Overview</h2><div class='big'>" << juce::String ((int) s.cmp.overall) << "% <span class='dim'>similarity to references</span></div>";
    h << "<p><b>Song:</b> " << esc (s.songName) << "<br><b>References (" << (int) s.refs.size() << "):</b> ";
    for (size_t i = 0; i < s.refs.size(); ++i) h << (i ? ", " : "") << esc (s.refs[i].name);
    h << "</p><h2>Comparison</h2><table><tr><th>Metric</th><th>Your song</th><th>Reference avg</th><th style='width:30%'>Similarity</th><th>Match</th></tr>";
    for (int m = 0; m < NumMetrics; ++m)
    {
        const auto col = metricColour (m).toDisplayString (false);
        h << "<tr><td><b>" << metricName (m) << "</b></td><td>" << juce::String (s.song.whole.v[m], 0) << "</td><td>" << juce::String (s.refAvg.v[m], 0)
          << "</td><td>" << bar (s.cmp.sim[m], "#" + col) << "</td><td>" << juce::String ((int) s.cmp.sim[m]) << "%</td></tr>";
    }
    h << "</table><h2>EQ bands (dB relative to average level)</h2><table><tr><th>Band</th><th>Your song</th><th>References</th><th>Difference</th></tr>";
    for (size_t b = 0; b < 8; ++b)
        h << "<tr><td>" << bandName ((int) b) << "</td><td>" << juce::String (s.song.whole.bands[b], 1) << "</td><td>" << juce::String (s.refAvg.bands[b], 1)
          << "</td><td>" << juce::String (s.song.whole.bands[b] - s.refAvg.bands[b], 1) << " dB</td></tr>";
    h << "</table><h2>Improvement checklist</h2>";
    if (s.tasks.empty()) h << "<p>Nothing major to fix - your song is very close to the references.</p>";
    for (auto& t : s.tasks)
    {
        const auto col = "#" + metricColour (t.metric).toDisplayString (false);
        h << "<div class='task'><span class='box'></span><span class='tag' style='background:" << col << "'>" << metricName (t.metric) << "</span> <b>"
          << esc (t.title) << "</b><br>" << esc (t.detail) << "<br><span class='dim'>" << esc (t.where) << "</span></div>";
    }
    if (s.hasReview)
    {
        h << "<h2>Review of: " << esc (s.reviewName) << "</h2><div class='big'>" << juce::String ((int) s.cmp.overall) << "% &rarr; "
          << juce::String ((int) s.reviewCmp.overall) << "%</div><table><tr><th>Task</th><th>Before</th><th>After</th><th>Result</th></tr>";
        for (auto& t : s.tasks)
            h << "<tr><td>" << esc (t.title) << "</td><td>" << juce::String (t.baseDiff, 1) << "</td><td>" << juce::String (t.newDiff, 1)
              << "</td><td><b>" << t.status << "</b></td></tr>";
        h << "</table>";
    }
    h << "<p class='dim' style='margin-top:30px'>Strides compares measured characteristics of your mix with your reference tracks. "
         "Scores are estimates - always trust your ears.</p>"
         "<script>window.onload=function(){setTimeout(function(){window.print()},600)}</script></body></html>";
    return h;
}
}
