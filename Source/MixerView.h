#pragma once

#include "Common.h"

/*  Mixer: one channel strip per audio track plus the master strip.
    Faders work on the volume plugin's slider position (the engine's fader curve);
    meters read the LevelMeterPlugin of the track / master chain.
*/
class MixerView : public juce::Component,
                  private juce::ValueTree::Listener,
                  private juce::AsyncUpdater
{
public:
    explicit MixerView (te::Edit&);
    ~MixerView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int stripWidth = 92;

private:
    class Strip;

    void rebuild();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override  { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override               { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override        { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override                { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;

    te::Edit& edit;
    juce::ValueTree editState;
    juce::Viewport viewport;
    juce::Component content;
    std::unique_ptr<Strip> masterStrip;
    juce::OwnedArray<Strip> strips;
    juce::String lastSignature;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
