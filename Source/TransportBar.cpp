#include "TransportBar.h"

TransportBar::TransportBar (te::Edit& e)
    : edit (e)
{
    auto& transport = edit.getTransport();
    transport.addChangeListener (this);

    playButton.onClick = [this]
    {
        auto& t = edit.getTransport();

        if (t.isPlaying())
            t.stop (false, false);
        else
            t.play (false);
    };

    stopButton.onClick = [this]
    {
        auto& t = edit.getTransport();

        // Second stop press returns to the start, like most DAWs
        if (! t.isPlaying())
            t.setPosition (0_tp);

        t.stop (false, false);
    };

    addFileButton.onClick  = [this] { if (onAddFile) onAddFile(); };
    settingsButton.onClick = [this] { if (onShowSettings) onShowSettings(); };

    for (auto* l : { &positionLabel, &timeLabel, &tempoLabel, &cpuLabel })
    {
        l->setColour (juce::Label::textColourId, Palette::text);
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*l);
    }

    positionLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    cpuLabel.setColour (juce::Label::textColourId, Palette::textDim);

    for (auto* b : { &playButton, &stopButton, &addFileButton, &settingsButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    updateButtons();
    startTimerHz (30);
}

TransportBar::~TransportBar()
{
    edit.getTransport().removeChangeListener (this);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panel);
    g.setColour (Palette::background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (8, 6);

    playButton.setBounds (r.removeFromLeft (70));
    r.removeFromLeft (4);
    stopButton.setBounds (r.removeFromLeft (70));
    r.removeFromLeft (16);
    positionLabel.setBounds (r.removeFromLeft (130));
    timeLabel.setBounds (r.removeFromLeft (110));
    tempoLabel.setBounds (r.removeFromLeft (110));

    settingsButton.setBounds (r.removeFromRight (120));
    r.removeFromRight (4);
    addFileButton.setBounds (r.removeFromRight (110));
    r.removeFromRight (8);
    cpuLabel.setBounds (r.removeFromRight (90));
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateButtons();
}

void TransportBar::updateButtons()
{
    const bool playing = edit.getTransport().isPlaying();
    playButton.setButtonText (playing ? "Pause" : "Play");
    playButton.setColour (juce::TextButton::buttonColourId,
                          playing ? Palette::accent.withAlpha (0.6f) : Palette::background);
}

void TransportBar::timerCallback()
{
    const auto pos = edit.getTransport().getPosition();
    const auto bb = edit.tempoSequence.toBarsAndBeats (pos);
    const auto beatInBar = bb.beats.inBeats();
    const int sixteenth = static_cast<int> ((beatInBar - std::floor (beatInBar)) * 4.0);

    positionLabel.setText (juce::String (bb.bars + 1) + "." + juce::String ((int) beatInBar + 1) + "." + juce::String (sixteenth + 1),
                           juce::dontSendNotification);

    const auto secs = pos.inSeconds();
    timeLabel.setText (juce::String::formatted ("%d:%06.3f", (int) secs / 60, std::fmod (secs, 60.0)),
                       juce::dontSendNotification);

    tempoLabel.setText (juce::String (edit.tempoSequence.getBpmAt (pos), 2) + " BPM", juce::dontSendNotification);
    cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (edit.engine.getDeviceManager().getCpuUsage() * 100.0)) + "%",
                      juce::dontSendNotification);
}
