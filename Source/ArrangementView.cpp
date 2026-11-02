#include "ArrangementView.h"

ArrangementView::ArrangementView (te::Edit& e)
    : edit (e), editState (e.state)
{
    editState.addListener (this);
    setWantsKeyboardFocus (false);
    setOpaque (true);

    renameEditor.setMultiLine (false);
    renameEditor.setSelectAllWhenFocused (true);
    renameEditor.onReturnKey = [this] { renameEditor.giveAwayKeyboardFocus(); };
    renameEditor.onEscapeKey = [this] { renameTrackID = {}; renameEditor.setVisible (false); };
    renameEditor.onFocusLost = [this]
    {
        if (auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, renameTrackID)))
        {
            auto name = renameEditor.getText().trim();
            if (name.isNotEmpty())
            {
                edit.getUndoManager().beginNewTransaction();
                t->setName (name);
            }
        }
        renameTrackID = {};
        renameEditor.setVisible (false);
        repaint();
    };
    addChildComponent (renameEditor);

    startTimerHz (60);
}

ArrangementView::~ArrangementView()
{
    editState.removeListener (this);
}

// MIDI clips need an instrument on their track to be audible; add a synth if there is none
static void ensureInstrument (te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (plugin->isSynth())
            return;

    if (auto plugin = track.edit.getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
        track.pluginList.insertPlugin (plugin, 0, nullptr);
}

//==============================================================================
// Coordinate mapping

double ArrangementView::beatToX (double beat) const       { return headerWidth + (beat - scrollBeats) * pixelsPerBeat; }
double ArrangementView::xToBeat (double x) const          { return (x - headerWidth) / pixelsPerBeat + scrollBeats; }
double ArrangementView::barToBeat (int bar) const
{
    return edit.tempoSequence.toBeats (te::tempo::BarsAndBeats { bar, te::BeatDuration() }).inBeats();
}

int ArrangementView::beatToBar (double beat) const
{
    return edit.tempoSequence.toBarsAndBeats (beatToTime (beat)).bars;
}

double ArrangementView::beatsPerBar() const
{
    // The engine's beat is one numerator unit, so a bar is always `numerator` beats long
    return juce::jmax (1.0, (double) edit.tempoSequence.getTimeSigAt (0_tp).numerator.get());
}

double ArrangementView::timeToBeat (te::TimePosition t) const { return edit.tempoSequence.toBeats (t).inBeats(); }
double ArrangementView::timeToX (te::TimePosition t) const    { return beatToX (timeToBeat (t)); }

te::TimePosition ArrangementView::beatToTime (double beat) const
{
    return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
}

int ArrangementView::trackY (int trackIndex) const
{
    return rulerHeight + trackIndex * trackHeight - scrollY;
}

int ArrangementView::yToTrackIndex (int y) const
{
    return (y - rulerHeight + scrollY) / trackHeight;
}

juce::Rectangle<int> ArrangementView::getLanesArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (rulerHeight);
}

juce::Rectangle<int> ArrangementView::getClipBounds (te::Clip& clip, int trackIndex) const
{
    const auto pos = clip.getPosition();
    const auto x1 = timeToX (pos.getStart());
    const auto x2 = timeToX (pos.getEnd());
    const auto y = trackY (trackIndex);

    return juce::Rectangle<double> (x1, y + 2.0, juce::jmax (2.0, x2 - x1), trackHeight - 4.0)
               .toNearestInt();
}

//==============================================================================
// Grid, snapping, zoom

double ArrangementView::getGridBeats() const
{
    // Adaptive grid: keep grid lines at least ~12px apart
    for (auto g : { 0.25, 0.5, 1.0, 4.0, 8.0, 16.0, 32.0, 64.0 })
        if (g * pixelsPerBeat >= 12.0)
            return g;

    return 128.0;
}

double ArrangementView::snapBeat (double beat, bool bypass) const
{
    if (bypass)
        return beat;

    const auto g = getGridBeats();
    return std::round (beat / g) * g;
}

void ArrangementView::zoomAround (double factor, float mouseX)
{
    const auto anchorBeat = xToBeat (mouseX);
    pixelsPerBeat = juce::jlimit (0.5, 800.0, pixelsPerBeat * factor);
    scrollBeats = juce::jmax (0.0, anchorBeat - (mouseX - headerWidth) / pixelsPerBeat);
    repaint();
}

void ArrangementView::scrollBy (double deltaBeats, int deltaY)
{
    const int numTracks = te::getAudioTracks (edit).size();
    const int maxScrollY = juce::jmax (0, numTracks * trackHeight - (getHeight() - rulerHeight) + trackHeight);

    scrollBeats = juce::jmax (0.0, scrollBeats + deltaBeats);
    scrollY = juce::jlimit (0, maxScrollY, scrollY + deltaY);
    repaint();
}

//==============================================================================
// Hit testing

ArrangementView::ClipHit ArrangementView::hitTestClip (juce::Point<int> p) const
{
    if (! getLanesArea().contains (p))
        return {};

    const auto tracks = te::getAudioTracks (edit);
    const int index = yToTrackIndex (p.y);

    if (! juce::isPositiveAndBelow (index, tracks.size()))
        return {};

    // Iterate backwards so the top-most (last painted) clip wins
    const auto& clips = tracks[index]->getClips();

    for (int i = clips.size(); --i >= 0;)
    {
        auto* clip = clips.getUnchecked (i);
        const auto b = getClipBounds (*clip, index);

        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
        {
            const int hy = b.getY() + clipHeaderHeight / 2;

            if (std::abs (p.y - hy) <= 6)
            {
                if (std::abs (p.x - fadeHandleX (*wave, true, b)) <= 6)  return { clip, DragMode::fadeIn };
                if (std::abs (p.x - fadeHandleX (*wave, false, b)) <= 6) return { clip, DragMode::fadeOut };
            }
        }

        if (b.contains (p))
        {
            const int grab = juce::jmin (edgeGrabWidth, b.getWidth() / 4);

            if (p.x < b.getX() + grab)      return { clip, DragMode::trimStart };
            if (p.x >= b.getRight() - grab) return { clip, DragMode::trimEnd };

            return { clip, DragMode::move };
        }
    }

    return {};
}

te::Clip* ArrangementView::getSelectedClip() const
{
    return selectedClipID.isValid() ? te::findClipForID (edit, selectedClipID) : nullptr;
}

//==============================================================================
// Painting

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    const auto lanes = getLanesArea();

    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (lanes);
        paintGrid (g, lanes);
        paintClips (g, lanes);
    }

    paintRuler (g);
    paintHeaders (g);
    paintPlayhead (g);
}

void ArrangementView::paintGrid (juce::Graphics& g, juce::Rectangle<int> lanes)
{
    const auto tracks = te::getAudioTracks (edit);

    for (int i = 0; i < tracks.size(); ++i)
    {
        g.setColour (i % 2 == 0 ? Palette::laneA : Palette::laneB);
        g.fillRect (lanes.getX(), trackY (i), lanes.getWidth(), trackHeight);
    }

    const auto gridBeats = getGridBeats();
    const auto firstBeat = std::floor (xToBeat (lanes.getX()) / gridBeats) * gridBeats;
    const auto lastBeat = xToBeat (lanes.getRight());

    for (auto beat = firstBeat; beat <= lastBeat; beat += gridBeats)
    {
        const bool isBar = std::abs (barToBeat (beatToBar (beat + 1.0e-6)) - beat) < 1.0e-6;
        g.setColour (isBar ? Palette::gridBar : Palette::gridBeat);
        g.drawVerticalLine (juce::roundToInt (beatToX (beat)), (float) lanes.getY(), (float) lanes.getBottom());
    }

    g.setColour (Palette::background);

    for (int i = 0; i <= tracks.size(); ++i)
        g.drawHorizontalLine (trackY (i), (float) lanes.getX(), (float) lanes.getRight());
}

void ArrangementView::paintClips (juce::Graphics& g, juce::Rectangle<int> lanes)
{
    const auto tracks = te::getAudioTracks (edit);

    for (int i = 0; i < tracks.size(); ++i)
    {
        if (trackY (i + 1) < lanes.getY() || trackY (i) > lanes.getBottom())
            continue;

        const bool autoMode = automationTracks.count (tracks[i]->itemID) > 0;

        if (autoMode)
            g.beginTransparencyLayer (0.4f);

        for (auto* clip : tracks[i]->getClips())
        {
            const auto b = getClipBounds (*clip, i);

            if (b.intersects (lanes))
                paintClip (g, *clip, b);
        }

        if (autoMode)
        {
            g.endTransparencyLayer();
            paintAutomation (g, *tracks[i], i);
        }
    }
}

void ArrangementView::paintClip (juce::Graphics& g, te::Clip& clip, juce::Rectangle<int> b)
{
    const bool selected = clip.itemID == selectedClipID;
    auto* midi = dynamic_cast<te::MidiClip*> (&clip);
    auto colour = midi != nullptr ? Palette::midiClip : Palette::clip;

    if (clip.state.hasProperty ("arrColour"))
        colour = juce::Colour::fromString (clip.state["arrColour"].toString());

    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip);
    const bool muted = waveClip != nullptr && waveClip->isMuted();

    if (muted)
        colour = colour.withSaturation (0.1f).darker (0.3f);

    g.setColour (colour.withAlpha (0.35f));
    g.fillRoundedRectangle (b.toFloat(), 3.0f);

    auto header = b.withHeight (clipHeaderHeight);
    g.setColour (colour);
    g.fillRoundedRectangle (header.toFloat(), 3.0f);

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (juce::FontOptions (11.5f));
    juce::String label = clip.getName();

    if (waveClip != nullptr)
    {
        if (waveClip->getAutoTempo())            label << "  [warp]";
        if (waveClip->hasAnyTakes())             label << "  [" << waveClip->getTakeDescriptions().size() << " takes]";
        if (waveClip->getIsReversed())           label << "  [rev]";
        if (waveClip->getTransposeSemiTones (false) != 0)
            label << "  " << juce::String::formatted ("%+d st", waveClip->getTransposeSemiTones (false));
        if (std::abs (waveClip->getGainDB()) > 0.05f)
            label << "  " << juce::String (waveClip->getGainDB(), 1) << " dB";
    }

    g.drawText (label, header.reduced (14, 0), juce::Justification::centredLeft, true);

    if (midi != nullptr)
    {
        auto& notes = midi->getSequence().getNotes();

        if (! notes.isEmpty())
        {
            int lo = 127, hi = 0;

            for (auto* n : notes)
            {
                lo = juce::jmin (lo, n->getNoteNumber());
                hi = juce::jmax (hi, n->getNoteNumber());
            }

            // Notes are clip-relative beats; clip start converted once
            const auto startBeat = timeToBeat (clip.getPosition().getStart());
            auto body = b.withTrimmedTop (clipHeaderHeight).reduced (0, 4);
            const float rowH = juce::jmin (6.0f, (float) body.getHeight() / (float) (hi - lo + 1));

            g.setColour (colour.brighter (0.6f));

            for (auto* n : notes)
            {
                const auto x1 = (float) beatToX (startBeat + n->getStartBeat().inBeats());
                const auto x2 = (float) beatToX (startBeat + n->getEndBeat().inBeats());
                const auto y = (float) body.getBottom() - (float) (n->getNoteNumber() - lo + 1) * rowH;
                g.fillRect (juce::Rectangle<float> (x1, y, juce::jmax (1.5f, x2 - x1), juce::jmax (1.5f, rowH - 1.0f))
                                .getIntersection (b.toFloat()));
            }
        }
    }
    else if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        if (auto* thumb = getThumbnailFor (*wave))
        {
            // Source range shown = clip offset .. offset + length (no stretch/loop yet)
            const auto pos = clip.getPosition();
            const auto start = te::TimePosition::fromSeconds (pos.getOffset().inSeconds());
            const te::TimeRange sourceRange (start, pos.getLength());

            auto body = b.withTrimmedTop (clipHeaderHeight).reduced (0, 2);
            g.setColour (colour.brighter (0.4f));
            thumb->drawChannels (g, body, sourceRange, juce::Decibels::decibelsToGain (wave->getGainDB()));
        }
        {
            auto body = b.withTrimmedTop (clipHeaderHeight).reduced (0, 2);
            // Fade shapes + handles
            const auto fin = fadeHandleX (*wave, true, b), fout = fadeHandleX (*wave, false, b);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            juce::Path fades;
            if (fin > b.getX() + 10)
                fades.addTriangle ((float) b.getX(), (float) body.getY(), (float) fin, (float) body.getY(), (float) b.getX(), (float) body.getBottom());
            if (fout < b.getRight() - 10)
                fades.addTriangle ((float) b.getRight(), (float) body.getY(), (float) fout, (float) body.getY(), (float) b.getRight(), (float) body.getBottom());
            g.fillPath (fades);

            g.setColour (juce::Colours::white.withAlpha (0.9f));
            const float hy = (float) b.getY() + clipHeaderHeight * 0.5f;
            g.fillEllipse ((float) fin - 3.0f, hy - 3.0f, 6.0f, 6.0f);
            g.fillEllipse ((float) fout - 3.0f, hy - 3.0f, 6.0f, 6.0f);
        }
    }

    g.setColour (selected ? Palette::accent : colour.darker (0.5f));
    g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 3.0f, selected ? 2.0f : 1.0f);
}

void ArrangementView::paintRuler (juce::Graphics& g)
{
    auto ruler = getLocalBounds().withHeight (rulerHeight).withTrimmedLeft (headerWidth);
    g.setColour (Palette::panel);
    g.fillRect (ruler);

    {
        auto& tr = edit.getTransport();
        const auto lr = tr.getLoopRange();
        if (lr.getLength().inSeconds() > 0.0)
        {
            const int x1 = juce::jmax (headerWidth, (int) timeToX (lr.getStart())), x2 = (int) timeToX (lr.getEnd());
            g.setColour (Palette::accent.withAlpha (tr.looping.get() ? 0.45f : 0.18f));
            g.fillRect (juce::Rectangle<int> (x1, ruler.getY() + 2, juce::jmax (0, x2 - x1), rulerHeight - 4));
        }
    }

    g.setFont (juce::FontOptions (10.0f));
    for (int i = 1; i < edit.tempoSequence.getNumTempos(); ++i)
    {
        auto* tempo = edit.tempoSequence.getTempo (i);
        const int x = (int) timeToX (tempo->getStartTime());
        if (x < headerWidth)
            continue;
        g.setColour (Palette::accent);
        g.fillRect (x, ruler.getBottom() - 8, 2, 8);
        g.drawText (juce::String (tempo->getBpm(), 1), x + 3, ruler.getBottom() - 12, 50, 12, juce::Justification::centredLeft);
    }

    for (int i = 1; i < edit.tempoSequence.getTimeSigs().size(); ++i)
    {
        auto* ts = edit.tempoSequence.getTimeSig (i);
        const int x = (int) beatToX (ts->startBeatNumber.get().inBeats());
        if (x < headerWidth)
            continue;
        g.setColour (Palette::text);
        g.fillRect (x, ruler.getY() + 14, 2, 8);
        g.drawText (ts->getStringTimeSig(), x + 3, ruler.getY() + 14, 40, 12, juce::Justification::centredLeft);
    }

    // Bar numbers, thinned out as you zoom out
    int barStep = 1;
    while (barStep * beatsPerBar() * pixelsPerBeat < 40.0)
        barStep *= 2;

    const int firstBar = juce::jmax (0, beatToBar (juce::jmax (0.0, xToBeat (ruler.getX()))));
    const int lastBar = beatToBar (xToBeat (ruler.getRight())) + 1;

    g.setFont (juce::FontOptions (11.0f));

    for (int bar = firstBar - (firstBar % barStep); bar <= lastBar; bar += barStep)
    {
        const auto x = juce::roundToInt (beatToX (barToBeat (bar)));

        if (x < ruler.getX())
            continue;

        g.setColour (Palette::textDim);
        g.drawVerticalLine (x, (float) ruler.getBottom() - 10.0f, (float) ruler.getBottom());
        g.setColour (Palette::text);
        g.drawText (juce::String (bar + 1), x + 3, ruler.getY() + 2, 40, 14, juce::Justification::centredLeft, false);
    }

    auto& transport = edit.getTransport();

    if (transport.looping)
    {
        const auto loop = transport.getLoopRange();
        auto x1 = (float) timeToX (loop.getStart()), x2 = (float) timeToX (loop.getEnd());
        g.setColour (Palette::accent.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x1, 0.0f, x2 - x1, 6.0f));
    }

    g.setColour (Palette::background);
    g.drawHorizontalLine (rulerHeight - 1, (float) headerWidth, (float) getWidth());
}

void ArrangementView::paintHeaders (juce::Graphics& g)
{
    auto area = getLocalBounds().withWidth (headerWidth);
    g.setColour (Palette::panel);
    g.fillRect (area);

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (area.withTrimmedTop (rulerHeight));

    const auto tracks = te::getAudioTracks (edit);
    g.setFont (juce::FontOptions (13.0f));

    for (int i = 0; i < tracks.size(); ++i)
    {
        auto* t = tracks[i];
        auto r = juce::Rectangle<int> (0, trackY (i), headerWidth, trackHeight);

        if (t->itemID == selectedTrackID)
        {
            g.setColour (Palette::accent.withAlpha (0.12f));
            g.fillRect (r);
        }

        g.setColour (juce::Colour::fromHSV ((float) (i % 8) / 8.0f, 0.5f, 0.8f, 1.0f));
        g.fillRect (r.removeFromLeft (4).reduced (0, 1));

        g.setColour (Palette::text);
        if (t->itemID != renameTrackID)
            g.drawText (t->getName(), r.reduced (8, 6).removeFromTop (18), juce::Justification::centredLeft, true);

        auto drawButton = [&] (HeaderButton b, const juce::String& label, bool on, juce::Colour onColour)
        {
            auto br = headerButtonRect (i, b).toFloat();
            g.setColour (on ? onColour : Palette::background);
            g.fillRoundedRectangle (br, 3.0f);
            g.setColour (on ? juce::Colours::black : Palette::textDim);
            g.drawText (label, br, juce::Justification::centred);
        };

        drawButton (HeaderButton::mute, "M", t->isMuted (false), juce::Colour (0xffd0a030));
        drawButton (HeaderButton::solo, "S", t->isSolo (false), juce::Colour (0xff3a8fd0));

        drawButton (HeaderButton::arm, "R", isArmed (*t), juce::Colour (0xffe04848));

        const bool autoOn = automationTracks.count (t->itemID) > 0;
        drawButton (HeaderButton::automation, "A", autoOn, Palette::accent);

        if (autoOn)
        {
            const bool pan = automationParam[t->itemID] == 1;
            drawButton (HeaderButton::param, pan ? "Pan" : "Vol", true, Palette::accent.withAlpha (0.7f));
        }

        g.setColour (Palette::background);
        g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) headerWidth);
    }

    g.setColour (Palette::background);
    g.drawVerticalLine (headerWidth - 1, 0.0f, (float) getHeight());
}

juce::Rectangle<int> ArrangementView::headerButtonRect (int trackIndex, HeaderButton b) const
{
    const int y = trackY (trackIndex) + 34, h = 22;

    switch (b)
    {
        case HeaderButton::mute:        return { 10, y, 24, h };
        case HeaderButton::solo:        return { 38, y, 24, h };
        case HeaderButton::arm:         return { 66, y, 24, h };
        case HeaderButton::automation:  return { 94, y, 24, h };
        case HeaderButton::param:       return { 122, y, 34, h };
        case HeaderButton::none:        break;
    }
    return {};
}

ArrangementView::HeaderButton ArrangementView::hitTestHeaderButton (juce::Point<int> p, int trackIndex) const
{
    for (auto b : { HeaderButton::mute, HeaderButton::solo, HeaderButton::arm, HeaderButton::automation, HeaderButton::param })
    {
        if (b == HeaderButton::param)
        {
            auto tracks = te::getAudioTracks (edit);
            if (! juce::isPositiveAndBelow (trackIndex, tracks.size()) || automationTracks.count (tracks[trackIndex]->itemID) == 0)
                continue;
        }

        if (headerButtonRect (trackIndex, b).contains (p))
            return b;
    }
    return HeaderButton::none;
}

te::AudioTrack* ArrangementView::getSelectedTrack() const
{
    return selectedTrackID.isValid() ? dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, selectedTrackID)) : nullptr;
}

void ArrangementView::headerClicked (const juce::MouseEvent& e)
{
    auto tracks = te::getAudioTracks (edit);
    const int index = yToTrackIndex (e.y);

    if (! juce::isPositiveAndBelow (index, tracks.size()))
    {
        selectedTrackID = {};
        if (e.mods.isPopupMenu())
            showTrackMenu (-1);
        repaint();
        return;
    }

    auto* t = tracks[index];
    selectedTrackID = t->itemID;

    if (e.mods.isPopupMenu())
    {
        showTrackMenu (index);
        return;
    }

    edit.getUndoManager().beginNewTransaction();

    switch (hitTestHeaderButton (e.getPosition(), index))
    {
        case HeaderButton::mute:        t->setMute (! t->isMuted (false)); break;
        case HeaderButton::solo:        t->setSolo (! t->isSolo (false)); break;
        case HeaderButton::arm:         toggleArm (*t); break;
        case HeaderButton::automation:
            if (automationTracks.erase (t->itemID) == 0)
                automationTracks.insert (t->itemID);
            break;
        case HeaderButton::param:       automationParam[t->itemID] = automationParam[t->itemID] == 1 ? 0 : 1; break;
        case HeaderButton::none:        break;
    }

    repaint();
}

void ArrangementView::showTrackMenu (int trackIndex)
{
    juce::PopupMenu m;
    m.addItem (1, "Add Audio Track");
    m.addItem (2, "Add Instrument Track");
    m.addSeparator();
    m.addItem (3, "Rename...", trackIndex >= 0);
    m.addItem (4, "Delete Track", trackIndex >= 0);

    m.showMenuAsync ({}, [this, trackIndex] (int r)
    {
        switch (r)
        {
            case 1: addTrack (false); break;
            case 2: addTrack (true); break;
            case 3: beginRename (trackIndex); break;
            case 4: deleteSelectedTrack(); break;
            default: break;
        }
    });
}

void ArrangementView::showRulerMenu (const juce::MouseEvent& e)
{
    auto& seq = edit.tempoSequence;
    int tempoIndex = -1, sigIndex = -1;

    for (int i = 1; i < seq.getNumTempos(); ++i)
        if (std::abs (timeToX (seq.getTempo (i)->getStartTime()) - e.x) <= 8.0)
            tempoIndex = i;

    for (int i = 1; i < seq.getTimeSigs().size(); ++i)
        if (std::abs (beatToX (seq.getTimeSig (i)->startBeatNumber.get().inBeats()) - e.x) <= 8.0)
            sigIndex = i;

    if (tempoIndex < 0 && sigIndex < 0)
        return;

    juce::PopupMenu m;
    if (tempoIndex >= 0) m.addItem (1, "Delete Tempo Change (" + juce::String (seq.getTempo (tempoIndex)->getBpm(), 1) + " BPM)");
    if (sigIndex >= 0)   m.addItem (2, "Delete Time Signature Change (" + seq.getTimeSig (sigIndex)->getStringTimeSig() + ")");

    m.showMenuAsync ({}, [this, tempoIndex, sigIndex] (int r)
    {
        edit.getUndoManager().beginNewTransaction();
        if (r == 1)      edit.tempoSequence.removeTempo (tempoIndex, false);
        else if (r == 2) edit.tempoSequence.removeTimeSig (sigIndex);
        repaint();
    });
}

void ArrangementView::beginRename (int trackIndex)
{
    auto tracks = te::getAudioTracks (edit);
    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return;

    renameTrackID = tracks[trackIndex]->itemID;
    renameEditor.setText (tracks[trackIndex]->getName(), false);
    renameEditor.setBounds (8, trackY (trackIndex) + 6, headerWidth - 16, 22);
    renameEditor.setVisible (true);
    renameEditor.grabKeyboardFocus();
    renameEditor.selectAll();
}

void ArrangementView::addTrack (bool instrument)
{
    edit.getTransport().stopIfRecording();
    edit.getUndoManager().beginNewTransaction();

    if (auto t = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr))
    {
        const int n = te::getAudioTracks (edit).size();
        t->setName ((instrument ? "Instrument " : "Audio ") + juce::String (n));

        if (instrument)
            ensureInstrument (*t);

        selectedTrackID = t->itemID;
    }
    repaint();
}

void ArrangementView::addReturnTrack()
{
    edit.getUndoManager().beginNewTransaction();

    if (auto t = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr))
    {
        t->setName ("Return A");

        if (auto ret = edit.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {}))
        {
            t->pluginList.insertPlugin (ret, 0, nullptr);
            if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (ret.get()))
                r->busNumber = 0;
        }

        if (auto rev = edit.getPluginCache().createNewPlugin (te::ReverbPlugin::xmlTypeName, {}))
            t->pluginList.insertPlugin (rev, 1, nullptr);

        selectedTrackID = t->itemID;
    }

    repaint();
}

void ArrangementView::deleteSelectedTrack()
{
    if (auto* t = getSelectedTrack())
    {
        edit.getUndoManager().beginNewTransaction();
        automationTracks.erase (t->itemID);
        selectedTrackID = {};
        edit.deleteTrack (t);
        selectedClipID = {};
    }
}

bool ArrangementView::isArmed (te::AudioTrack& t) const
{
    if (auto* ctx = edit.getTransport().getCurrentPlaybackContext())
        for (auto* in : ctx->getAllInputs())
            if (in->isRecordingEnabled (t.itemID))
                return true;
    return false;
}

// Arming routes the track's default input: all MIDI inputs for instrument tracks, the first audio input otherwise
void ArrangementView::toggleArm (te::AudioTrack& t)
{
    auto& transport = edit.getTransport();
    transport.ensureContextAllocated();
    auto* ctx = transport.getCurrentPlaybackContext();
    if (ctx == nullptr)
        return;

    const bool arm = ! isArmed (t);
    bool instrument = false;
    for (auto* plugin : t.pluginList)
        instrument = instrument || plugin->isSynth();

    bool usedAudio = false;
    for (auto* in : ctx->getAllInputs())
    {
        const bool midi = in->owner.isMidi();
        bool wanted = arm && (instrument ? midi : (! midi && ! usedAudio));
        if (wanted && ! midi)
            usedAudio = true;

        if (wanted)
        {
            if (in->setTarget (t.itemID, true, &edit.getUndoManager()).has_value())
                in->setRecordingEnabled (t.itemID, true);
        }
        else if (in->getTargets().contains (t.itemID))
        {
            in->setRecordingEnabled (t.itemID, false);
        }
    }
    repaint();
}

void ArrangementView::selectTrackIndex (int index)
{
    auto tracks = te::getAudioTracks (edit);
    selectedTrackID = juce::isPositiveAndBelow (index, tracks.size()) ? tracks[index]->itemID : te::EditItemID();
    repaint();
}

void ArrangementView::toggleAutomationOnSelectedTrack()
{
    if (auto* t = getSelectedTrack())
        if (automationTracks.erase (t->itemID) == 0)
            automationTracks.insert (t->itemID);
    repaint();
}

//==============================================================================
// Automation

te::AutomatableParameter* ArrangementView::getAutoParam (te::AudioTrack& t) const
{
    auto* vp = t.getVolumePlugin();
    if (vp == nullptr)
        return nullptr;

    auto it = automationParam.find (t.itemID);
    return (it != automationParam.end() && it->second == 1) ? vp->panParam.get() : vp->volParam.get();
}

float ArrangementView::autoValueToY (te::AutomatableParameter& p, float v, int trackIndex) const
{
    const auto range = p.getValueRange();
    const float norm = juce::jlimit (0.0f, 1.0f, (v - range.getStart()) / range.getLength());
    const int top = trackY (trackIndex) + 8, h = trackHeight - 16;
    return (float) top + (1.0f - norm) * (float) h;
}

float ArrangementView::autoYToValue (te::AutomatableParameter& p, int y, int trackIndex) const
{
    const auto range = p.getValueRange();
    const int top = trackY (trackIndex) + 8, h = trackHeight - 16;
    const float norm = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (y - top) / (float) h);
    return range.getStart() + norm * range.getLength();
}

int ArrangementView::hitTestAutoPoint (te::AutomatableParameter& p, int trackIndex, juce::Point<int> pos) const
{
    auto& curve = p.getCurve();

    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const float x = (float) timeToX (curve.getPointTime (i));
        const float y = autoValueToY (p, curve.getPointValue (i), trackIndex);

        if (std::abs (x - (float) pos.x) <= 7.0f && std::abs (y - (float) pos.y) <= 7.0f)
            return i;
    }
    return -1;
}

void ArrangementView::paintAutomation (juce::Graphics& g, te::AudioTrack& t, int trackIndex)
{
    auto* p = getAutoParam (t);
    if (p == nullptr)
        return;

    const auto lane = juce::Rectangle<int> (headerWidth, trackY (trackIndex), getWidth() - headerWidth, trackHeight);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (lane);

    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (p->getParameterName(), lane.reduced (6, 2).removeFromTop (14), juce::Justification::centredLeft);

    auto& curve = p->getCurve();
    const int n = curve.getNumPoints();

    juce::Path path;
    if (n == 0)
    {
        const float y = autoValueToY (*p, p->getCurrentValue(), trackIndex);
        path.startNewSubPath ((float) lane.getX(), y);
        path.lineTo ((float) lane.getRight(), y);
    }
    else
    {
        const float firstY = autoValueToY (*p, curve.getPointValue (0), trackIndex);
        path.startNewSubPath ((float) lane.getX(), firstY);
        for (int i = 0; i < n; ++i)
            path.lineTo ((float) timeToX (curve.getPointTime (i)), autoValueToY (*p, curve.getPointValue (i), trackIndex));
        path.lineTo ((float) lane.getRight(), autoValueToY (*p, curve.getPointValue (n - 1), trackIndex));
    }

    g.setColour (Palette::accent);
    g.strokePath (path, juce::PathStrokeType (n == 0 ? 1.0f : 2.0f));

    for (int i = 0; i < n; ++i)
    {
        const float x = (float) timeToX (curve.getPointTime (i));
        const float y = autoValueToY (*p, curve.getPointValue (i), trackIndex);
        g.setColour (Palette::background);
        g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
        g.setColour (Palette::accent);
        g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 2.0f);
    }
}

void ArrangementView::automationMouseDown (const juce::MouseEvent& e, te::AudioTrack& t, int trackIndex)
{
    auto* p = getAutoParam (t);
    if (p == nullptr)
        return;

    auto& curve = p->getCurve();
    auto& um = edit.getUndoManager();
    um.beginNewTransaction();

    const int hit = hitTestAutoPoint (*p, trackIndex, e.getPosition());

    if (hit >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown()))
    {
        curve.removePoint (hit, &um);
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
        return;

    dragAutoTrack = t.itemID;

    if (hit >= 0)
    {
        dragAutoIndex = hit;
    }
    else
    {
        const auto time = beatToTime (juce::jmax (0.0, snapBeat (xToBeat (e.x), e.mods.isAltDown())));
        dragAutoIndex = curve.addPoint (time, autoYToValue (*p, e.y, trackIndex), 0.0f, &um);
    }

    dragMode = DragMode::automation;
    repaint();
}

void ArrangementView::paintPlayhead (juce::Graphics& g)
{
    const auto x = (float) timeToX (edit.getTransport().getPosition());

    if (x < headerWidth)
        return;

    g.setColour (Palette::playhead);
    g.drawLine (x, 0.0f, x, (float) getHeight(), 1.5f);

    juce::Path p;
    p.addTriangle (x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 8.0f);
    g.fillPath (p);
}

te::SmartThumbnail* ArrangementView::getThumbnailFor (te::WaveAudioClip& clip)
{
    auto file = clip.getAudioFile();

    if (! file.isValid())
        return nullptr;

    auto& thumb = thumbnails[clip.itemID];

    if (thumb == nullptr)
        thumb = std::make_unique<te::SmartThumbnail> (edit.engine, file, *this, &edit);

    return thumb.get();
}

//==============================================================================
// Model / timer callbacks

void ArrangementView::handleAsyncUpdate()
{
    // Drop thumbnails for clips that no longer exist
    for (auto it = thumbnails.begin(); it != thumbnails.end();)
        it = te::findClipForID (edit, it->first) == nullptr ? thumbnails.erase (it) : std::next (it);

    repaint();
}

void ArrangementView::timerCallback()
{
    auto& transport = edit.getTransport();
    const int x = juce::roundToInt (timeToX (transport.getPosition()));

    // Page-follow while playing
    if (transport.isPlaying() && dragMode == DragMode::none && x > getWidth() - 20)
    {
        scrollBeats = timeToBeat (transport.getPosition()) - 2.0;
        repaint();
        lastPlayheadX = x;
        return;
    }

    if (x != lastPlayheadX)
    {
        repaint (lastPlayheadX - 8, 0, 16, getHeight());
        repaint (x - 8, 0, 16, getHeight());
        lastPlayheadX = x;
    }
}

//==============================================================================
// Mouse

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    switch (hitTestClip (e.getPosition()).mode)
    {
        case DragMode::trimStart:
        case DragMode::trimEnd:
        case DragMode::fadeIn:
        case DragMode::fadeOut:   setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case DragMode::move:      setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case DragMode::none:
        case DragMode::automation:
        case DragMode::loopRange:
        case DragMode::scrub:     setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    dragMode = DragMode::none;

    if (renameEditor.isVisible())
        renameEditor.giveAwayKeyboardFocus();

    if (e.x < headerWidth && e.y >= rulerHeight)
    {
        headerClicked (e);
        return;
    }

    if (e.y >= rulerHeight && e.x >= headerWidth)
    {
        const auto tracks = te::getAudioTracks (edit);
        const int index = yToTrackIndex (e.y);

        if (juce::isPositiveAndBelow (index, tracks.size()))
        {
            selectedTrackID = tracks[index]->itemID;

            if (automationTracks.count (tracks[index]->itemID) > 0)
            {
                automationMouseDown (e, *tracks[index], index);
                return;
            }
        }
    }

    if (e.y < rulerHeight && e.x >= headerWidth && e.mods.isPopupMenu())
    {
        showRulerMenu (e);
        return;
    }

    if (e.y < rulerHeight && e.x >= headerWidth && e.mods.isShiftDown())
    {
        dragMode = DragMode::loopRange;
        loopAnchorBeat = juce::jmax (0.0, snapBeat (xToBeat (e.x), e.mods.isAltDown()));
        return;
    }

    if (e.y < rulerHeight && e.x >= headerWidth)
    {
        dragMode = DragMode::scrub;
        edit.getTransport().setPosition (beatToTime (juce::jmax (0.0, snapBeat (xToBeat (e.x), e.mods.isAltDown()))));
        return;
    }

    const auto hit = hitTestClip (e.getPosition());

    if (hit.clip == nullptr)
    {
        selectedClipID = {};
        repaint();
        return;
    }

    selectedClipID = hit.clip->itemID;

    if (e.mods.isPopupMenu())
    {
        repaint();
        showClipMenu (hit.clip->itemID);
        return;
    }

    dragClipID = hit.clip->itemID;
    dragMode = hit.mode;

    const auto pos = hit.clip->getPosition();
    dragMouseStartBeat = xToBeat (e.x);
    dragClipStartBeat = timeToBeat (pos.getStart());
    dragClipEndBeat = timeToBeat (pos.getEnd());

    edit.getUndoManager().beginNewTransaction();
    repaint();
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    const bool bypassSnap = e.mods.isAltDown();

    if (dragMode == DragMode::automation)
    {
        auto tracks = te::getAudioTracks (edit);
        const int index = tracks.indexOf (dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, dragAutoTrack)));

        if (index >= 0 && dragAutoIndex >= 0)
            if (auto* p = getAutoParam (*tracks[index]))
                dragAutoIndex = p->getCurve().movePoint (*p, dragAutoIndex,
                                                         beatToTime (juce::jmax (0.0, snapBeat (xToBeat (e.x), bypassSnap))),
                                                         autoYToValue (*p, e.y, index), false, &edit.getUndoManager());
        repaint();
        return;
    }

    if (dragMode == DragMode::loopRange)
    {
        const double b = juce::jmax (0.0, snapBeat (xToBeat (e.x), bypassSnap));
        if (std::abs (b - loopAnchorBeat) > 0.01)
        {
            auto& tr = edit.getTransport();
            tr.setLoopRange ({ beatToTime (juce::jmin (b, loopAnchorBeat)), beatToTime (juce::jmax (b, loopAnchorBeat)) });
            tr.looping = true;
        }
        repaint();
        return;
    }

    if (dragMode == DragMode::scrub)
    {
        edit.getTransport().setPosition (beatToTime (juce::jmax (0.0, snapBeat (xToBeat (e.x), bypassSnap))));
        return;
    }

    auto* clip = te::findClipForID (edit, dragClipID);

    if (clip == nullptr)
        return;

    const auto delta = xToBeat (e.x) - dragMouseStartBeat;
    const auto minLength = getGridBeats() * 0.25;

    switch (dragMode)
    {
        case DragMode::move:
        {
            const auto newStart = juce::jmax (0.0, snapBeat (dragClipStartBeat + delta, bypassSnap));
            clip->setStart (beatToTime (newStart), false, true);

            // Vertical move to another track
            const auto tracks = te::getAudioTracks (edit);
            const int targetIndex = yToTrackIndex (e.y);

            if (juce::isPositiveAndBelow (targetIndex, tracks.size())
                 && tracks[targetIndex] != clip->getTrack())
            {
                if (dynamic_cast<te::MidiClip*> (clip) != nullptr)
                    ensureInstrument (*tracks[targetIndex]);

                clip->moveTo (*tracks[targetIndex]);
            }

            break;
        }

        case DragMode::trimStart:
        {
            const auto newStart = juce::jlimit (0.0, dragClipEndBeat - minLength, snapBeat (dragClipStartBeat + delta, bypassSnap));
            clip->setStart (beatToTime (newStart), true, false);
            break;
        }

        case DragMode::trimEnd:
        {
            const auto newEnd = juce::jmax (dragClipStartBeat + minLength, snapBeat (dragClipEndBeat + delta, bypassSnap));
            clip->setEnd (beatToTime (newEnd), true);
            break;
        }

        case DragMode::fadeIn:
        case DragMode::fadeOut:
        {
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
            {
                const auto pos = clip->getPosition();
                const auto t = beatToTime (juce::jmax (0.0, xToBeat (e.x)));

                if (dragMode == DragMode::fadeIn)
                {
                    const auto len = juce::jlimit (0.0, (pos.getLength() - wave->getFadeOut()).inSeconds(), (t - pos.getStart()).inSeconds());
                    wave->setFadeIn (te::TimeDuration::fromSeconds (len));
                }
                else
                {
                    const auto len = juce::jlimit (0.0, (pos.getLength() - wave->getFadeIn()).inSeconds(), (pos.getEnd() - t).inSeconds());
                    wave->setFadeOut (te::TimeDuration::fromSeconds (len));
                }
            }
            break;
        }

        case DragMode::none:
        case DragMode::scrub:
        case DragMode::automation:
        case DragMode::loopRange:
            break;
    }
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    dragAutoIndex = -1;
    dragMode = DragMode::none;
    dragClipID = {};
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y >= rulerHeight && e.x < headerWidth)
    {
        if (e.y - trackY (yToTrackIndex (e.y)) < 30)
            beginRename (yToTrackIndex (e.y));
        return;
    }

    if (e.y < rulerHeight || e.x < headerWidth)
        return;

    {
        auto tr = te::getAudioTracks (edit);
        const int ti = yToTrackIndex (e.y);
        if (juce::isPositiveAndBelow (ti, tr.size()) && automationTracks.count (tr[ti]->itemID) > 0)
            return;
    }

    const auto hit = hitTestClip (e.getPosition());

    if (hit.clip != nullptr)
    {
        if (dynamic_cast<te::MidiClip*> (hit.clip) != nullptr && onOpenMidiClip)
            onOpenMidiClip (hit.clip->itemID);

        return;
    }

    // Empty lane: create a one-bar MIDI clip at the clicked (snapped) position
    const auto tracks = te::getAudioTracks (edit);
    const int index = yToTrackIndex (e.y);

    if (! juce::isPositiveAndBelow (index, tracks.size()))
        return;

    const int bar = juce::jmax (0, beatToBar (juce::jmax (0.0, xToBeat (e.x))));
    const te::TimeRange range (beatToTime (barToBeat (bar)), beatToTime (barToBeat (bar + 1)));

    edit.getUndoManager().beginNewTransaction();
    ensureInstrument (*tracks[index]);

    if (auto clip = tracks[index]->insertMIDIClip ("MIDI", range, nullptr))
    {
        selectedClipID = clip->itemID;
        repaint();

        if (onOpenMidiClip)
            onOpenMidiClip (clip->itemID);
    }
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        zoomAround (std::pow (2.0, (double) w.deltaY * 2.0), e.position.x);
        return;
    }

    // Trackpad: deltaX scrolls time, deltaY scrolls tracks; shift turns a mouse wheel horizontal
    auto dx = (double) w.deltaX, dy = (double) w.deltaY;

    if (e.mods.isShiftDown() && dx == 0.0)
        std::swap (dx, dy);

    scrollBy (-dx * 400.0 / pixelsPerBeat, juce::roundToInt (-dy * 400.0));
}

void ArrangementView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomAround (scaleFactor, e.position.x);
}

//==============================================================================
// Keyboard

bool ArrangementView::keyPressed (const juce::KeyPress& key)
{
    auto* clip = getSelectedClip();

    if (clip != nullptr && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
    {
        edit.getUndoManager().beginNewTransaction();
        clip->removeFromParent();
        selectedClipID = {};
        return true;
    }

    // Cmd+E: split the selected clip at the playhead
    if (clip != nullptr && key == juce::KeyPress ('e', juce::ModifierKeys::commandModifier, 0))
    {
        if (auto* track = dynamic_cast<te::ClipTrack*> (clip->getTrack()))
        {
            const auto t = edit.getTransport().getPosition();

            if (clip->getPosition().time.containsInclusive (t))
            {
                edit.getUndoManager().beginNewTransaction();
                track->splitClip (*clip, t);
            }
        }

        return true;
    }

    if (clip != nullptr && key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))
    {
        duplicateSelectedClip();
        return true;
    }

    if (key == juce::KeyPress ('f', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        zoomToFit();
        return true;
    }

    if (key == juce::KeyPress ('=', juce::ModifierKeys::commandModifier, 0))
    {
        zoomAround (1.5, (float) headerWidth);
        return true;
    }

    if (key == juce::KeyPress ('-', juce::ModifierKeys::commandModifier, 0))
    {
        zoomAround (1.0 / 1.5, (float) headerWidth);
        return true;
    }

    return false;
}

void ArrangementView::zoomToFit()
{
    const double endBeat = std::max (16.0, timeToBeat (te::TimePosition() + edit.getLength()));
    const double width = (double) std::max (100, getWidth() - headerWidth - 20);
    pixelsPerBeat = juce::jlimit (2.0, 400.0, width / endBeat);
    scrollBeats = 0.0;
    repaint();
}

//==============================================================================
// File drag & drop

bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    auto& formats = edit.engine.getAudioFileFormatManager().readFormatManager;

    for (auto& f : files)
        if (formats.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;

    return false;
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto start = beatToTime (juce::jmax (0.0, snapBeat (xToBeat (juce::jmax (x, headerWidth)), false)));
    int trackIndex = juce::jmax (0, yToTrackIndex (y));

    // Multiple files go onto consecutive tracks
    for (auto& f : files)
        insertAudioFile (juce::File (f), trackIndex++, start);
}

void ArrangementView::insertAudioFile (const juce::File& file, int trackIndex, te::TimePosition start)
{
    te::AudioFile audioFile (edit.engine, file);

    if (! audioFile.isValid())
        return;

    edit.ensureNumberOfAudioTracks (trackIndex + 1);
    auto* track = te::getAudioTracks (edit)[trackIndex];

    edit.getUndoManager().beginNewTransaction();

    const te::ClipPosition position { { start, te::TimeDuration::fromSeconds (audioFile.getLength()) }, {} };

    if (auto clip = track->insertWaveClip (file.getFileNameWithoutExtension(), file, position, false))
        selectedClipID = clip->itemID;

    repaint();
}

//==============================================================================
// Clip operations

int ArrangementView::fadeHandleX (te::WaveAudioClip& wave, bool fadeIn, juce::Rectangle<int> b) const
{
    const auto pos = wave.getPosition();

    if (fadeIn)
        return juce::jmax (b.getX() + 10, (int) timeToX (pos.getStart() + wave.getFadeIn()));

    return juce::jmin (b.getRight() - 10, (int) timeToX (pos.getEnd() - wave.getFadeOut()));
}

void ArrangementView::duplicateSelectedClip()
{
    auto* clip = getSelectedClip();

    if (clip == nullptr)
        return;

    auto* track = dynamic_cast<te::ClipTrack*> (clip->getTrack());

    if (track == nullptr)
        return;

    edit.getUndoManager().beginNewTransaction();

    if (auto copy = te::duplicateClip (*clip))
    {
        auto state = copy->state;
        const auto end = clip->getPosition().getEnd();

        if (auto* inserted = track->insertClipWithState (state))
        {
            inserted->setStart (end, false, true);
            selectedClipID = inserted->itemID;
        }
    }

    repaint();
}

static void askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("t", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
                        {
                            if (r == 1)
                                done (w->getTextEditorContents ("t"));
                        }), true);
}

void ArrangementView::showClipMenu (te::EditItemID id)
{
    auto* clip = te::findClipForID (edit, id);

    if (clip == nullptr)
        return;

    auto* wave = dynamic_cast<te::WaveAudioClip*> (clip);
    const bool isMidi = dynamic_cast<te::MidiClip*> (clip) != nullptr;

    static const juce::Colour colours[] = { juce::Colour (0xff4f8fd8), juce::Colour (0xff58b368), juce::Colour (0xffd8704f), juce::Colour (0xffc9a93a),
                                            juce::Colour (0xff9a6fd8), juce::Colour (0xffd86fa8), juce::Colour (0xff4fc9c9), juce::Colour (0xff9a9fa8) };
    static const char* colourNames[] = { "Blue", "Green", "Orange", "Yellow", "Purple", "Pink", "Teal", "Grey" };
    static const float gains[] = { -12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f };
    static const double fades[] = { 0.0, 0.01, 0.05, 0.1, 0.25, 0.5, 1.0 };
    static const int transposes[] = { -12, -7, -5, -2, -1, 0, 1, 2, 5, 7, 12 };

    juce::PopupMenu m;
    m.addItem (1, "Rename...");
    m.addItem (2, "Split at Playhead");
    m.addItem (3, "Duplicate");
    m.addItem (4, "Delete");

    juce::PopupMenu colourMenu;
    for (int i = 0; i < 8; ++i)
        colourMenu.addColouredItem (100 + i, colourNames[i], colours[i]);
    m.addSubMenu ("Colour", colourMenu);

    if (wave != nullptr)
    {
        m.addSeparator();
        m.addItem (5, "Mute Clip", true, wave->isMuted());
        m.addItem (6, "Reverse", true, wave->getIsReversed());

        juce::PopupMenu gainMenu, fadeInMenu, fadeOutMenu, transposeMenu;
        for (int i = 0; i < 6; ++i)
            gainMenu.addItem (200 + i, juce::String (gains[i], 0) + " dB", true, std::abs (wave->getGainDB() - gains[i]) < 0.05f);
        for (int i = 0; i < 7; ++i)
        {
            const auto label = i == 0 ? juce::String ("None") : juce::String ((int) (fades[i] * 1000)) + " ms";
            fadeInMenu.addItem (300 + i, label);
            fadeOutMenu.addItem (400 + i, label);
        }
        for (int i = 0; i < 11; ++i)
            transposeMenu.addItem (500 + i, juce::String::formatted ("%+d semitones", transposes[i]), true,
                                   wave->getTransposeSemiTones (false) == transposes[i]);

        m.addSubMenu ("Gain", gainMenu);
        m.addSubMenu ("Fade In", fadeInMenu);
        m.addSubMenu ("Fade Out", fadeOutMenu);
        m.addSeparator();
        if (wave->hasAnyTakes())
        {
            juce::PopupMenu takes;
            auto names = wave->getTakeDescriptions();
            for (int i = 0; i < names.size(); ++i)
                takes.addItem (600 + i, names[i], true, wave->getCurrentTake() == i);
            takes.addSeparator();
            takes.addItem (12, "Unpack Takes to New Tracks");
            takes.addItem (13, "Delete Unused Takes");
            m.addSubMenu ("Takes (" + juce::String (names.size()) + ")", takes);
        }

        m.addItem (7, "Warp to Project Tempo", true, wave->getAutoTempo());
        m.addItem (8, "Detect Source Tempo");
        m.addSubMenu ("Transpose", transposeMenu);
    }
    else if (isMidi)
    {
        m.addSeparator();
        m.addItem (9, "Edit in Piano Roll");
    }

    m.showMenuAsync ({}, [this, id] (int r)
    {
        auto* c = te::findClipForID (edit, id);

        if (c == nullptr || r == 0)
            return;

        auto* w = dynamic_cast<te::WaveAudioClip*> (c);
        edit.getUndoManager().beginNewTransaction();

        if (r == 1)
        {
            askText ("Rename clip", c->getName(), [this, id] (juce::String name)
            {
                if (auto* cc = te::findClipForID (edit, id))
                {
                    edit.getUndoManager().beginNewTransaction();
                    cc->setName (name.trim());
                    repaint();
                }
            });
        }
        else if (r == 2)
        {
            const auto t = edit.getTransport().getPosition();
            if (auto* track = dynamic_cast<te::ClipTrack*> (c->getTrack()))
                if (c->getPosition().time.containsInclusive (t))
                    track->splitClip (*c, t);
        }
        else if (r == 3)  duplicateSelectedClip();
        else if (r == 4)  { c->removeFromParent(); selectedClipID = {}; }
        else if (r == 9 && onOpenMidiClip) onOpenMidiClip (id);
        else if (r >= 100 && r < 108)
        {
            static const juce::uint32 argb[] = { 0xff4f8fd8, 0xff58b368, 0xffd8704f, 0xffc9a93a, 0xff9a6fd8, 0xffd86fa8, 0xff4fc9c9, 0xff9a9fa8 };
            c->state.setProperty ("arrColour", juce::Colour (argb[r - 100]).toString(), &edit.getUndoManager());
        }
        else if (w != nullptr)
        {
            if (r == 5)       w->setMuted (! w->isMuted());
            else if (r == 6)  w->setIsReversed (! w->getIsReversed());
            else if (r >= 200 && r < 206)
            {
                static const float g[] = { -12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f };
                w->setGainDB (g[r - 200]);
            }
            else if ((r >= 300 && r < 307) || (r >= 400 && r < 407))
            {
                static const double f[] = { 0.0, 0.01, 0.05, 0.1, 0.25, 0.5, 1.0 };
                const bool in = r < 400;
                const auto len = te::TimeDuration::fromSeconds (f[r - (in ? 300 : 400)]);
                if (in) w->setFadeIn (len); else w->setFadeOut (len);
            }
            else if (r >= 500 && r < 511)
            {
                static const int t[] = { -12, -7, -5, -2, -1, 0, 1, 2, 5, 7, 12 };
                w->setTranspose (t[r - 500]);
            }
            else if (r == 8)
            {
                w->performTempoDetect();
            }
            else if (r >= 600 && r < 700)
                w->setCurrentTake (r - 600);
            else if (r == 12)
                w->unpackTakes (true);
            else if (r == 13)
                w->deleteAllUnusedTakes (false);
            else if (r == 7)
            {
                setClipWarp (*w, ! w->getAutoTempo());
            }
        }

        repaint();
    });
}

void ArrangementView::setClipWarp (te::WaveAudioClip& w, bool on)
{
    if (w.getAutoTempo() == on)
        return;

    const auto info = w.getAudioFile().getInfo();

    if (on && w.getLoopInfo().getBpm (info) <= 0.0)
        w.performTempoDetect();

    const auto sourceBpm = w.getLoopInfo().getBpm (info);
    const auto projectBpm = edit.tempoSequence.getBpmAt (w.getPosition().getStart());

    // Keep the same musical content: stretching the audio also stretches the clip on the timeline
    if (sourceBpm > 0.0 && projectBpm > 0.0)
    {
        const auto pos = w.getPosition();
        const auto factor = on ? sourceBpm / projectBpm : projectBpm / sourceBpm;
        w.setAutoTempo (on);

        if (on)
            w.setTimeStretchMode (te::TimeStretcher::soundtouchBetter);

        w.setEnd (pos.getStart() + te::TimeDuration::fromSeconds (pos.getLength().inSeconds() * factor), false);
        return;
    }

    w.setAutoTempo (on);

    if (on)
        w.setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
}

bool ArrangementView::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString() == "BrowserFile";
}

void ArrangementView::itemDropped (const SourceDetails& d)
{
    if (! getDraggedBrowserFile)
        return;

    const auto file = getDraggedBrowserFile();
    const auto p = d.localPosition;
    const auto start = beatToTime (juce::jmax (0.0, snapBeat (xToBeat (juce::jmax (p.x, headerWidth)), false)));
    insertAudioFile (file, juce::jmax (0, yToTrackIndex (p.y)), start);
}
