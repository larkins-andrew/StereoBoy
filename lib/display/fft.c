#include "fft.h"


void bit_reverse(cplx buf[], int n)
{
    for (int i = 1, j = 0; i < n; i++)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            cplx temp = buf[i];
            buf[i] = buf[j];
            buf[j] = temp;
        }
    }
}

void fft_optimized(cplx buf[], int n)
{
    bit_reverse(buf, n);

    for (int len = 2; len <= n; len <<= 1)
    {
        double ang = 2.0 * PI / len;
        cplx wlen = cos(ang) - I * sin(ang);
        for (int i = 0; i < n; i += len)
        {
            cplx w = 1.0 + 0.0 * I;
            for (int j = 0; j < len / 2; j++)
            {
                cplx u = buf[i + j];
                cplx v = buf[i + j + len / 2] * w;
                buf[i + j] = u + v;
                buf[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void draw_bins(int n)
{
    if (n <= 0)
        n = 1;
    float complex proc_l[HISTORY_SIZE], proc_r[HISTORY_SIZE];
    static float display_l[64], display_r[64]; // Support up to 64 bars

    // 1. DC Removal & Windowing (Same as before)
    float avg_l = 0, avg_r = 0;
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        avg_l += (float)audio_history_l[i];
        avg_r += (float)audio_history_r[i];
    }
    avg_l /= HISTORY_SIZE;
    avg_r /= HISTORY_SIZE;

    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        float mult = 0.5f * (1.0f - cosf(2.0f * PI * i / (HISTORY_SIZE - 1)));
        proc_l[i] = ((float)audio_history_l[i] - avg_l) * mult + 0.0f * I;
        proc_r[i] = ((float)audio_history_r[i] - avg_r) * mult + 0.0f * I;
    }

    fft_optimized(proc_l, HISTORY_SIZE);
    fft_optimized(proc_r, HISTORY_SIZE);

    // 2. Dynamic Logarithmic Bucketing
    int last_bin = 2; // Start skipping DC
    int max_bin = HISTORY_SIZE / 2;

    for (int b = 0; b < n; b++)
    {
        // Calculate end_bin using an exponential curve
        // This ensures the frequency range of each bar grows as we go higher
        float ratio = (float)(b + 1) / n;
        int end_bin = (int)(2 + (max_bin - 2) * powf(ratio, 2.5f));

        if (end_bin <= last_bin)
            end_bin = last_bin + 1;
        if (end_bin >= max_bin)
            end_bin = max_bin - 1;

        float peak_l = 0, peak_r = 0;
        for (int i = last_bin; i <= end_bin; i++)
        {
            float m_l = cabsf(proc_l[i]) / HISTORY_SIZE;
            float m_r = cabsf(proc_r[i]) / HISTORY_SIZE;
            if (m_l > peak_l)
                peak_l = m_l;
            if (m_r > peak_r)
                peak_r = m_r;
        }

        // Adaptive Gate & Scaling
        float adaptive_gate = 6.0f - (ratio * 4.0f);
        if (adaptive_gate < 1.0f)
            adaptive_gate = 1.0f;

        float sens = 50.0f + (ratio * 100.0f);
        float target_l = 0, target_r = 0;

        if (peak_l > adaptive_gate)
            target_l = (log10f(peak_l + 1e-9f) - log10f(adaptive_gate)) * sens;
        if (peak_r > adaptive_gate)
            target_r = (log10f(peak_r + 1e-9f) - log10f(adaptive_gate)) * sens;

        // Smoothing
        float decay = 4.0f - (ratio * 3.0f);
        if (target_l > display_l[b])
            display_l[b] = target_l;
        else
            display_l[b] -= decay;
        if (target_r > display_r[b])
            display_r[b] = target_r;
        else
            display_r[b] -= decay;

        if (display_l[b] < 0)
            display_l[b] = 0;
        if (display_r[b] < 0)
            display_r[b] = 0;

        // 3. Dynamic Screen Positioning
        int total_width = SCREEN_WIDTH;
        int bar_plus_gap = total_width / n;
        int bar_width = (int)(bar_plus_gap * 0.8f); // 80% bar, 20% gap
        if (bar_width < 1)
            bar_width = 1;

        int x_pos = b * bar_plus_gap;

        draw_spectrum_bars(x_pos, bar_width, (int)display_l[b], (int)display_r[b], (int)target_l, (int)target_r);

        last_bin = end_bin + 1;
    }
}

void draw_spectrum_bars(int x_start, int width, int h_l, int h_r, int target_l, int target_r)
{
    const int MAX_BAR_HEIGHT = 110;

    if (h_l > MAX_BAR_HEIGHT)
        h_l = MAX_BAR_HEIGHT;
    if (h_r > MAX_BAR_HEIGHT)
        h_r = MAX_BAR_HEIGHT;

    for (int w = 0; w < width; w++)
    {
        int cur_x = x_start + w;
        if (cur_x >= SCREEN_WIDTH)
            break;

        // LEFT CHANNEL: Top half
        for (int y = 0; y < h_l; y++)
        {
            frame_buffer[(120 + y) * SCREEN_WIDTH + cur_x] = y > target_l ? FFT_L_COLOR_LIGHT : FFT_L_COLOR_DARK;
        }

        // RIGHT CHANNEL: Bottom half
        for (int y = 0; y < h_r; y++)
        {
            frame_buffer[(120 - y) * SCREEN_WIDTH + cur_x] = y > target_r ? FFT_R_COLOR_LIGHT : FFT_R_COLOR_DARK;
        }
    }
}

