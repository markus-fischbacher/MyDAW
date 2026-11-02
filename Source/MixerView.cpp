#include "MixerView.h"
#include "PluginUI.h"

namespace
{
    juce::String dbText (double pos)
    {
        auto db = te::volumeFaderPositionToDB ((float) pos);
        return db <= -100.0f ? "-inf" : juce::String (db, 1) + " dB";
    }

    te::LevelMeterPlugin* findOrCreateMeter (te::PluginList& list)
    {
        if (auto* m = list.findFirstPluginOfType<te::LevelMeterPlugin>())
            return m;

        list.insertPlugin (te::LevelMeterPlugin::create(), -1);
        return list.findFirstPluginOfType<te::LevelMeterPlugin>();
    }

    class Meter : public juce::Component
    {
    public:
        void setLevels (float l, float r)
        {
            left = juce::jmax (l, left - 2.5f);
            right = juce::jmax (r, right - 2.5f);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (b, 2.0f);
            auto half = b.getWidth() / 2.0f;
            drawBar (g, b.removeFromLeft (half).reduced (1.0f, 1.0f), left);
            drawBar (g, b.reduced (1.0f, 1.0f), right);
        }

    private:
        float left = -100.0f, right = -100.0f;

        static void drawBar (juce::Graphics& g, juce::Rectangle<float> r, float dB)
        {
            auto norm = juce::jlimit (0.0f, 1.0f, (dB + 60.0f) / 66.0f);   // -60 .. +6 dB
            auto h = r.getHeight() * norm;
            auto bar = r.removeFromBottom (h);
            g.setColour (dB > 0.0f ? juce::Colour (0xffe04848) : dB > -12.0f ? juce::Colour (0xffe0c040) : juce::Colour (0xff48c070));
            g.fillRect (bar);
        }
    };
}

//==============================================================================
class MixerView::Strip : public juce::Component,
                         private juce::Timer
{
public:
    Strip (te::Edit& e, te::AudioTrack* t)    // t == nullptr -> master strip
        : edit (e), track (t)
    {
        if (track != nullptr)
        {
            volume = track->getVolumePlugin();
            meter = track->getLevelMeterPlugin();
            if (meter == nullptr)
            {
                track->pluginList.insertPlugin (te::LevelMeterPlugin::create(), -1);
                meter = track->getLevelMeterPlugin();
            }
            name.setText (track->getName(), juce::dontSendNotification);
        }
        else
        {
            volume = edit.getMasterVolumePlugin().get();
            meter = findOrCreateMeter (edit.getMasterPluginList());
            name.setText ("Master", juce::dontSendNotification);
        }

        name.setJustificationType (juce::Justification::centred);
        name.setColour (juce::Label::textColourId, Palette::text);
        addAndMakeVisible (name);

        pan.setSliderStyle (juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setColour (juce::Slider::thumbColourId, Palette::accent);
        addAndMakeVisible (pan);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, stripWidth - 40, 18);
        fader.setRange (0.0, 1.0, 0.0);
        fader.setSkewFactor (1.0);
        fader.setDoubleClickReturnValue (true, te::decibelsToVolumeFaderPosition (0.0f));
        fader.textFromValueFunction = dbText;
        fader.setColour (juce::Slider::thumbColourId, Palette::accent);
        addAndMakeVisible (fader);
        addAndMakeVisible (meterComp);

        if (volume != nullptr)
        {
            fader.setValue (volume->getSliderPos(), juce::dontSendNotification);
            pan.setValue (volume->getPan(), juce::dontSendNotification);

            fader.onValueChange = [this] { volume->setSliderPos ((float) fader.getValue()); };
            pan.onValueChange   = [this] { volume->setPan ((float) pan.getValue()); };
        }

        if (track != nullptr)
        {
            for (auto* b : { &mute, &solo })
            {
                b->setClickingTogglesState (true);
                addAndMakeVisible (*b);
            }
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd0a030));
            solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a8fd0));
            mute.setToggleState (track->isMuted (false), juce::dontSendNotification);
            solo.setToggleState (track->isSolo (false), juce::dontSendNotification);
            fx.onClick = [this] { PluginUI::showTrackMenu (edit.engine, *track); };
            addAndMakeVisible (fx);
            mute.onClick = [this] { track->setMute (mute.getToggleState()); };
            solo.onClick = [this] { track->setSolo (solo.getToggleState()); };
        }

        if (meter != nullptr)
            meter->measurer.addClient (client);

        startTimerHz (30);
    }

    ~Strip() override
    {
        stopTimer();
        if (meter != nullptr)
            meter->measurer.removeClient (client);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f);
        if (track != nullptr)
        {
            g.setColour (juce::Colour::fromHSV ((float) (track->getIndexInEditTrackList() % 8) / 8.0f, 0.5f, 0.8f, 1.0f));
            g.fillRect (getLocalBounds().removeFromTop (4).reduced (4, 0));
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 6);
        r.removeFromTop (4);
        name.setBounds (r.removeFromTop (20));
        pan.setBounds (r.removeFromTop (22));
        if (track != nullptr)
        {
            fx.setBounds (r.removeFromBottom (22).reduced (1));
            r.removeFromBottom (2);
            auto row = r.removeFromBottom (24);
            mute.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (1));
            solo.setBounds (row.reduced (1));
            r.removeFromBottom (4);
        }
        auto m = r.removeFromRight (18);
        meterComp.setBounds (m.withTrimmedBottom (18).reduced (2, 4));
        fader.setBounds (r);
    }

    void refreshName()
    {
        if (track != nullptr)
            name.setText (track->getName(), juce::dontSendNotification);
    }

private:
    void timerCallback() override
    {
        meterComp.setLevels (client.getAndClearAudioLevel (0).dB, client.getAndClearAudioLevel (1).dB);

        // follow automation / undo
        if (volume != nullptr)
        {
            if (! fader.isMouseButtonDown())
                fader.setValue (volume->getSliderPos(), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())
                pan.setValue (volume->getPan(), juce::dontSendNotification);
        }
        if (track != nullptr)
        {
            mute.setToggleState (track->isMuted (false), juce::dontSendNotification);
            solo.setToggleState (track->isSolo (false), juce::dontSendNotification);
        }
    }

    te::Edit& edit;
    te::AudioTrack::Ptr track;
    te::Plugin::Ptr volumeKeep, meterKeep;
    te::VolumeAndPanPlugin* volume = nullptr;
    te::LevelMeterPlugin* meter = nullptr;
    te::LevelMeasurer::Client client;

    juce::Label name;
    juce::Slider pan, fader;
    juce::TextButton mute { "M" }, solo { "S" }, fx { "FX" };
    Meter meterComp;
};

//==============================================================================
MixerView::MixerView (te::Edit& e) : edit (e), editState (e.state)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    editState.addListener (this);
    rebuild();
}

MixerView::~MixerView()
{
    editState.removeListener (this);
    cancelPendingUpdate();
    strips.clear();
    masterStrip = nullptr;
}

void MixerView::handleAsyncUpdate()
{
    juce::String sig;
    for (auto* t : te::getAudioTracks (edit))
        sig << t->itemID.toString() << ",";

    if (sig != lastSignature)
        rebuild();
    else
        for (auto* s : strips)
            s->refreshName();
}

void MixerView::rebuild()
{
    strips.clear();
    masterStrip = nullptr;
    lastSignature.clear();

    for (auto* t : te::getAudioTracks (edit))
    {
        lastSignature << t->itemID.toString() << ",";
        auto* s = strips.add (new Strip (edit, t));
        content.addAndMakeVisible (s);
    }

    masterStrip = std::make_unique<Strip> (edit, nullptr);
    addAndMakeVisible (*masterStrip);
    resized();
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
    g.setColour (Palette::gridBar);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void MixerView::resized()
{
    auto r = getLocalBounds().withTrimmedTop (1);
    if (masterStrip != nullptr)
        masterStrip->setBounds (r.removeFromRight (stripWidth + 4));
    viewport.setBounds (r);

    auto h = viewport.getMaximumVisibleHeight();
    content.setSize (juce::jmax (r.getWidth(), strips.size() * stripWidth), h);
    int x = 0;
    for (auto* s : strips)
    {
        s->setBounds (x, 0, stripWidth, h);
        x += stripWidth;
    }
}
