// YOLO tune - pitch correction DSP.
// A line-by-line C++ port of the YoloTune AudioWorkletProcessor from the web version,
// so the plugin sounds the same as the browser tuner.
// No JUCE dependency: this file can be compiled and tested on its own.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

struct YoloParams
{
    float speedMs  = 0.0f;   // retune speed in ms (<= 0 means instant)
    float amount   = 1.0f;   // 0..1
    float sustain  = 0.0f;   // 0..1
    float flex     = 0.0f;   // 0..1
    float hum      = 0.0f;   // 0..1 humanize
    float refCents = 0.0f;   // pitch reference, -100..100 cents
    float inGain   = 1.0f;   // linear
    float outGain  = 1.0f;   // linear
    float doubler  = 0.0f;   // 0..1
    float tone     = 0.0f;   // 0..1 guide-tone volume
    int   stab     = 0;      // note stabilizer 0..3
    bool  bypass   = false;
    bool  lowLat   = true;
    bool  inst     = false;  // instrument mode (wider pitch range)
    bool  allowed[12] = { true, true, true, true, true, true, true, true, true, true, true, true };
};

class YoloDSP
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        buf.assign (N, 0.0f);
        dl.assign (DL, 0.0f);
        x.assign (2300, 0.0f);
        cm.assign (1500, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        std::fill (dl.begin(), dl.end(), 0.0f);
        w = 0; dw = 0; ph = 0; ph2 = 0; ph3 = 0;
        hasSm = false; sm = 0; tgt = 60; cc = 0; o = 0; ratio = 1; rt = 1; cnt = 0; rb = 0;
        voiced = false; m = 60;
        tonePhase = 0; toneFreq = 261.6; toneGain = 0;
        setLowLatency (lowLatState);
    }

    // Latency the plugin reports to the host (average delay of the shifter taps).
    int getLatencySamples() const { return W / 2; }

    void setLowLatency (bool on)
    {
        lowLatState = on;
        W   = on ? 576 : 1024;
        hop = on ? 240 : 480;
    }

    // in: mono input. outL / outR: stereo output (outR may be nullptr for mono out).
    void process (const float* in, float* outL, float* outR, int n, const YoloParams& p)
    {
        if (p.lowLat != lowLatState)
            setLowLatency (p.lowLat);

        const bool byp = p.bypass;
        const float ig = p.inGain, og = p.outGain, db = p.doubler;
        const int e2 = (int) std::lround (0.017 * sr), e3 = (int) std::lround (0.024 * sr);

        // guide tone smoothing (same time constants as the web version)
        const double fCoef = std::exp (-1.0 / (0.02 * sr));
        const double gCoef = std::exp (-1.0 / (0.03 * sr));

        float pkIn = 0, pkOut = 0;

        for (int i = 0; i < n; ++i)
        {
            const float s = (in != nullptr ? in[i] : 0.0f) * ig;
            pkIn = std::max (pkIn, std::abs (s));

            buf[(size_t) w] = s; w = (w + 1) % N;
            dl[(size_t) dw] = s; dw = (dw + 1) & (DL - 1);

            if (++cnt >= hop) { cnt = 0; analyze (p); }

            ratio += (rt - ratio) * (0.01 + rb * 0.06);
            ph += (1.0 - ratio) / W;
            if (ph < 0) ph += 1;
            if (ph >= 1) ph -= 1;

            // bypass: dry signal delayed by the reported latency, so it stays in sync in the DAW
            const float y = byp ? dl[(size_t) ((dw - 1 - W / 2) & (DL - 1))] : rd (ph, 0);
            float l = y, r = y;

            if (db > 0 && ! byp)
            {
                ph2 += (1.0 - ratio * 0.9931) / W;   if (ph2 < 0) ph2 += 1; if (ph2 >= 1) ph2 -= 1;
                ph3 += (1.0 - ratio * 1.00696) / W;  if (ph3 < 0) ph3 += 1; if (ph3 >= 1) ph3 -= 1;
                l += db * 0.6f * rd (ph2, e2);
                r += db * 0.6f * rd (ph3, e3);
            }

            l = softClip (l * og);
            r = softClip (r * og);

            // guide tone: a sine on the corrected note
            const double outNote = m + o * (byp ? 0.0 : p.amount);
            double tf = 440.0 * std::pow (2.0, (outNote - 69.0) / 12.0 + p.refCents / 1200.0);
            tf = std::clamp (tf, 30.0, 2000.0);
            const double tg = (byp || ! voiced) ? 0.0 : (double) p.tone * p.tone * 0.3;
            toneFreq = tf + (toneFreq - tf) * fCoef;
            toneGain = tg + (toneGain - tg) * gCoef;
            if (toneGain > 1.0e-5)
            {
                tonePhase += toneFreq / sr;
                if (tonePhase >= 1) tonePhase -= 1;
                const float t = (float) (std::sin (tonePhase * 6.283185307179586) * toneGain);
                l += t; r += t;
            }

            pkOut = std::max (pkOut, std::max (std::abs (l), std::abs (r)));

            outL[i] = l;
            if (outR != nullptr) outR[i] = r;
        }

        // telemetry for the UI (peaks are held until the UI reads them)
        float cur = peakIn.load();
        while (pkIn > cur && ! peakIn.compare_exchange_weak (cur, pkIn)) {}
        cur = peakOut.load();
        while (pkOut > cur && ! peakOut.compare_exchange_weak (cur, pkOut)) {}
    }

    // ---- telemetry (read from the message thread) ----
    std::atomic<float> teleM { 60.0f }, teleOut { 60.0f }, peakIn { 0.0f }, peakOut { 0.0f };
    std::atomic<bool>  teleVoiced { false };

private:
    static constexpr int N = 4096, DL = 8192;

    double sr = 48000.0;
    std::vector<float> buf, dl, x, cm;
    int w = 0, dw = 0, W = 576, hop = 240, cnt = 0;
    bool lowLatState = true;
    double ph = 0, ph2 = 0, ph3 = 0;
    bool hasSm = false;
    double sm = 0, o = 0, ratio = 1, rt = 1, rb = 0, m = 60;
    int tgt = 60, cc = 0;
    bool voiced = false;
    double tonePhase = 0, toneFreq = 261.6, toneGain = 0;

    static float softClip (float v)
    {
        const float a = std::abs (v);
        return a < 0.7f ? v : (v < 0 ? -1.0f : 1.0f) * (0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f));
    }

    static int pc (int r) { return ((r % 12) + 12) % 12; }

    // YIN-style pitch detection (cumulative mean normalized difference)
    double detect (const YoloParams& p)
    {
        const int Wd = 768;
        const bool ins = p.inst;
        const int tmax = std::min (ins ? 1400 : 700, (int) std::floor (sr / (ins ? 58.0 : 70.0)));
        const int tmin = std::max (2, (int) std::floor (sr / (ins ? 1500.0 : 900.0)));
        const int total = Wd + tmax;

        for (int i = 0; i < total; ++i)
            x[(size_t) i] = buf[(size_t) ((w - total + i + N * 4) % N)];

        double e = 0;
        for (int i = tmax; i < total; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
        if (e / Wd < 2e-5) return -1;

        double run = 0;
        cm[0] = 1;
        for (int t = 1; t < tmax; ++t)
        {
            double d = 0;
            for (int j = tmax; j < total; ++j) { const double v = (double) x[(size_t) j] - x[(size_t) (j - t)]; d += v * v; }
            run += d;
            cm[(size_t) t] = run > 0 ? (float) (d * t / run) : 1.0f;
        }

        int best = -1;
        for (int t = tmin; t < tmax - 1; ++t)
        {
            if (cm[(size_t) t] < 0.15f)
            {
                while (t + 1 < tmax - 1 && cm[(size_t) (t + 1)] < cm[(size_t) t]) ++t;
                best = t;
                break;
            }
        }
        if (best < 0)
        {
            float mn = 1; int mi = -1;
            for (int t = tmin; t < tmax - 1; ++t) if (cm[(size_t) t] < mn) { mn = cm[(size_t) t]; mi = t; }
            if (mn < 0.3f) best = mi;
        }
        if (best < 1) return -1;

        const double a = cm[(size_t) (best - 1)], b = cm[(size_t) best], c = cm[(size_t) (best + 1)];
        const double den = 2 * (2 * b - a - c);
        double tau = best;
        if (std::abs (den) > 1e-9) tau += (c - a) / den;
        return sr / tau;
    }

    void analyze (const YoloParams& p)
    {
        const double hopSec = hop / sr;
        const double f = detect (p);

        if (f > 0)
        {
            const double mm = 69 + 12 * std::log2 (f / (440 * std::pow (2.0, p.refCents / 1200.0)));
            voiced = true; m = mm;
            rb = std::clamp ((25.0 - p.speedMs) / 28.0, 0.0, 1.0);

            if (! hasSm || std::abs (sm - mm) > 3) { sm = mm; hasSm = true; }
            else sm = sm + (mm - sm) * (1 - 0.92 * p.sustain * (1 - rb));

            const int base = (int) std::lround (sm);
            int cand = base; double bd = 99;
            for (int i = -6; i <= 6; ++i)
            {
                const int r = base + i;
                if (p.allowed[pc (r)] && std::abs (r - sm) < bd) { bd = std::abs (r - sm); cand = r; }
            }

            if (cand == tgt) cc = 0;
            else
            {
                ++cc;
                static const int need[4] = { 0, 2, 6, 14 };
                if (cc > need[std::clamp (p.stab, 0, 3)]) { tgt = cand; cc = 0; }
            }

            double oT = tgt - sm;
            const double tol = p.flex * 1.2, ae = std::abs (oT);
            if (tol > 0 && ae < tol) { const double k = ae / tol; oT *= k * k; }

            const double ms = p.speedMs;
            const double a = (ms <= 0 ? 1.0 : 1.0 - std::exp (-hopSec / std::max (0.0005, ms / 1000.0 / 2.5))) * (1 - 0.7 * p.hum);
            o += (oT - o) * a;
            rt = p.bypass ? 1.0 : std::pow (2.0, o * p.amount / 12.0);
        }
        else
        {
            voiced = false;
            o *= 0.8;
            rt = p.bypass ? 1.0 : std::pow (2.0, o * p.amount / 12.0);
            cc = 0;
        }

        teleM.store ((float) m);
        teleOut.store ((float) (m + o * (p.bypass ? 0.0 : p.amount)));
        teleVoiced.store (voiced);
    }

    // two crossfaded read taps on the delay line (the pitch shifter)
    float rd (double phase, int extra) const
    {
        double y = 0;
        for (int k = 0; k < 2; ++k)
        {
            double t = phase + k * 0.5;
            if (t >= 1) t -= 1;
            const double d = t * W + 2 + extra;
            const double pos = dw - 1 - d;
            const int i0 = (int) std::floor (pos);
            const double fr = pos - i0;
            const float a0 = dl[(size_t) (i0 & (DL - 1))], a1 = dl[(size_t) ((i0 + 1) & (DL - 1))];
            y += (a0 + (a1 - a0) * fr) * (1 - std::abs (2 * t - 1));
        }
        return (float) y;
    }
};
