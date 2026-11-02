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

    std::function<void()> onAddFile, onShowSettings, onToggleMixer, onRecord;
    void setMixerShown (bool b) { mixerButton.setToggleState (b, juce::dontSendNotification); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateButtons();

    te::Edit& edit;

    juce::TextButton playButton { "Play" }, stopButton { "Stop" }, recordButton { "Rec" },
                     addFileButton { "Add Audio..." }, settingsButton { "Audio Settings" },
                     loopButton { "Loop" }, clickButton { "Click" }, punchButton { "Punch" }, mixerButton { "Mixer" };
    juce::Label positionLabel, timeLabel, tempoLabel, cpuLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
