#ifndef LOSSFILTER_H_INCLUDED
#define LOSSFILTER_H_INCLUDED

#include "FIRFilter.h"

class LossFilter
{
public:
    LossFilter (AudioProcessorValueTreeState& vts)
    {
        speed = vts.getRawParameterValue ("speed");
        spacing = vts.getRawParameterValue ("spacing");
        thickness = vts.getRawParameterValue ("thick");
        gap = vts.getRawParameterValue ("gap");

        filters.add (new FIRFilter (order));
        filters.add (new FIRFilter (order));

        currentCoefs.resize (order);
    }
    ~LossFilter() {}

    static void createParameterLayout (std::vector<std::unique_ptr<RangedAudioParameter>>& params)
    {
        NormalisableRange<float> speedRange (1.0f, 100.0f); // meters per second
        speedRange.setSkewForCentre (15.0f);

        NormalisableRange<float> spaceRange ((float) 1.0e-6, 100.0f);
        spaceRange.setSkewForCentre ((float) 1.0);

        NormalisableRange<float> thickRange ((float) 1.0e-6, 10.0f);
        thickRange.setSkewForCentre (0.0001f);

        NormalisableRange<float> gapRange ((float) 1.0e-6, 100.0f);
        gapRange.setSkewForCentre ((float) 1.0e-3);

        params.push_back (std::make_unique<AudioParameterFloat> ("speed",   "Speed [ips]",     speedRange, 15.0f));
        params.push_back (std::make_unique<AudioParameterFloat> ("spacing", "Spacing [mm]", spaceRange, (float) 1.0e-9));
        params.push_back (std::make_unique<AudioParameterFloat> ("thick",   "Thickness [mm]", thickRange, (float) 1.0e-9));
        params.push_back (std::make_unique<AudioParameterFloat> ("gap",     "Gap",       gapRange,   (float) 1.0e-9));
    }

    void prepare (float sampleRate, int samplesPerBlock)
    {
        fs = sampleRate;
        binWidth = fs / (float) order;
        fadeBuffer.resize (samplesPerBlock);

        fsFactor = (int) (fs / 44100.0f);
        const int curOrder = order * fsFactor;
        filters.clear();
        filters.add (new FIRFilter (curOrder));
        filters.add (new FIRFilter (curOrder));
        currentCoefs.resize (curOrder);
        
        filters[0]->reset();
        filters[1]->reset();
        
        calcCoefs();
        filters[0]->setCoefs (currentCoefs.getRawDataPointer());
        filters[1]->setCoefs (currentCoefs.getRawDataPointer());

        prevSpeed = *speed;
        prevSpacing = *spacing;
        prevThickness = *thickness;
        prevGap = *gap;
    }

    void calcCoefs()
    {
        // Set freq domain multipliers
        int curOrder = order * fsFactor;
        std::unique_ptr<float[]> H (new float[curOrder]);
        for (int k = 0; k < curOrder / 2; k++)
        {
            const auto freq = ((float) k * binWidth); // + (binWidth / 2.0f);
            const auto waveNumber = MathConstants<float>::twoPi * jmax (freq, 20.0f) / (*speed * 0.0254f);
            const auto thickTimesK = waveNumber * (*thickness * (float) 1.0e-3);
            const auto kGapOverTwo = waveNumber * (*gap * (float) 1.0e-3) / 2.0f;
        
            H[k] = expf (-1.0f * waveNumber * (*spacing * (float) 1.0e-3)); // Spacing loss formula
            H[k] *= (1.0f - expf (-thickTimesK)) / thickTimesK;
            H[k] *= sinf (kGapOverTwo) / kGapOverTwo;
            H[curOrder - k - 1] = H[k];
        }
        
        // Create time domain filter signals
        auto h = currentCoefs.getRawDataPointer();
        for (int n = 0; n < curOrder; n++)
        {
            for (int k = 0; k < curOrder; k++)
                h[n] += H[k] * cosf (MathConstants<float>::twoPi * (float) k * (float) n / (float) curOrder);
            
            h[n] /= (float) curOrder;
        }
    }

    inline void processBlock (float* buffer, const int numSamples)
    {
        if (*spacing == (float) 1.0e-6 && *thickness == (float) 1.0e-6 && *gap == (float) 1.0e-6
            && *spacing == prevSpacing && *thickness == prevThickness && *gap == prevGap)
            return;

        if ((*speed != prevSpeed || *spacing != prevSpacing ||
            *thickness != prevThickness || *gap != prevGap) && fadeCount == 0)
        {
            calcCoefs();
            filters[! activeFilter]->setCoefs (currentCoefs.getRawDataPointer());
        
            fadeCount = fadeLength;
            prevSpeed = *speed;
            prevSpacing = *spacing;
            prevThickness = *thickness;
            prevGap = *gap;
        }

        if (fadeCount > 0)
            for (int n = 0; n < numSamples; ++n)
                fadeBuffer.setUnchecked (n, buffer[n]);
        else
            filters[! activeFilter]->processBypassed (buffer, numSamples);
        
        filters[activeFilter]->process (buffer, numSamples);
        
        if (fadeCount > 0)
        {
            filters[! activeFilter]->process (fadeBuffer.getRawDataPointer(), numSamples);
        
            for (int n = 0; n < numSamples; ++n)
            {
                float mult = (float) fadeCount / (float) fadeLength;
                buffer[n] = buffer[n] * mult + fadeBuffer[n] * (1.0f - mult);
        
                fadeCount--;
                if (fadeCount == 0)
                    break;
            }
            
            if (fadeCount == 0)
                activeFilter = ! activeFilter;
        }
    }

private:
    OwnedArray<FIRFilter> filters;
    int activeFilter = 0;
    int fadeCount = 0;
    const int fadeLength = 512;
    Array<float> fadeBuffer;

    float* speed = nullptr;
    float* spacing = nullptr;
    float* thickness = nullptr;
    float* gap = nullptr;

    float prevSpeed;
    float prevSpacing;
    float prevThickness;
    float prevGap;

    float fs = 44100.0f;
    int fsFactor = (int) (fs / 44100.0f);
    float binWidth = fs / 100.0f;

    const int order = 100;
    Array<float> currentCoefs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LossFilter)
};

#endif //LOSSFILTER_H_INCLUDED
