#pragma once

#include "Common.h"

// Top bar: play/stop, position readout, tempo, file + settings buttons.
class TransportBar : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit TransportBar (te::Edit&);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onAddFile, onShowSettings;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateButtons();

    te::Edit& edit;

    juce::TextButton playButton { "Play" }, stopButton { "Stop" },
                     addFileButton { "Add Audio..." }, settingsButton { "Audio Settings" };
    juce::Label positionLabel, timeLabel, tempoLabel, cpuLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
