#include "ArrangementView.h"

ArrangementView::ArrangementView (te::Edit& e)
    : edit (e), editState (e.state)
{
    loadLayout();
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

te::TimePosition ArrangementView::xToTime (double x) const
{
    return beatToTime (juce::jmax (0.0, xToBeat (x)));
}

int ArrangementView::rowHeight (int trackIndex) const
{
    auto tracks = te::getAudioTracks (edit);

    if (juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return trackHeight + autoRowsHeight (*tracks[trackIndex]) + compRows (*tracks[trackIndex]) * compLaneHeight;

    return trackHeight;
}

int ArrangementView::trackY (int trackIndex) const
{
    int y = rulerHeight - scrollY;

    for (int i = 0; i < trackIndex; ++i)
        y += rowHeight (i);

    return y;
}

int ArrangementView::totalTracksHeight() const
{
    return trackY (te::getAudioTracks (edit).size()) - rulerHeight + scrollY;
}

int ArrangementView::yToTrackIndex (int y) const
{
    const int n = te::getAudioTracks (edit).size();
    int top = rulerHeight - scrollY;

    for (int i = 0; i < n; ++i)
    {
        top += rowHeight (i);
        if (y < top)
            return i;
    }

    return n + (y - top) / trackHeight;
}

te::WaveAudioClip* ArrangementView::compClipFor (te::AudioTrack& t) const
{
    if (compTracks.count (t.itemID) == 0)
        return nullptr;

    if (auto* w = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (edit, selectedClipID)))
        if (w->getTrack() == &t && w->hasAnyTakes())
            return w;

    return nullptr;
}

int ArrangementView::autoRows (te::AudioTrack& t) const    { return isExpanded (t) ? (int) expandedLanes (t).size() : 0; }

int ArrangementView::autoRowsHeight (te::AudioTrack& t) const
{
    int h = 0;
    if (isExpanded (t))
        for (auto& l : expandedLanes (t))
            h += laneH (l.param);
    return h;
}

int ArrangementView::laneAt (te::AudioTrack& t, int rel, int& laneTop) const
{
    int y = 0, k = 0;
    if (rel >= 0 && isExpanded (t))
        for (auto& l : expandedLanes (t))
        {
            const int h = laneH (l.param);
            if (rel < y + h) { laneTop = y; return k; }
            y += h;
            ++k;
        }
    return -1;
}

int ArrangementView::compRows (te::AudioTrack& t) const
{
    if (auto* w = compClipFor (t))
        return w->getCompManager().getTotalNumTakes();

    return 0;
}

double ArrangementView::timelineToCompTime (te::WaveAudioClip& w, double beat) const
{
    const auto pos = w.getPosition();
    const double secs = (beatToTime (beat) - pos.getStart()).inSeconds();
    return (pos.getOffset().inSeconds() + secs) * w.getCompManager().getSpeedRatio();
}

void ArrangementView::paintCompLanes (juce::Graphics& g, te::AudioTrack& t, int y)
{
    auto* w = compClipFor (t);
    if (w == nullptr)
        return;

    auto& cm = w->getCompManager();
    const auto pos = w->getPosition();
    const int x1 = (int) timeToX (pos.getStart()), x2 = (int) timeToX (pos.getEnd());
    const int active = cm.getActiveTakeIndex();
    const double speed = cm.getSpeedRatio();
    auto toX = [&] (double compTime)
    {
        return timeToX (pos.getStart() + te::TimeDuration::fromSeconds (compTime / speed - pos.getOffset().inSeconds()));
    };

    for (int i = 0; i < cm.getTotalNumTakes(); ++i, y += compLaneHeight)
    {
        const auto r = juce::Rectangle<int> (x1, y + 1, x2 - x1, compLaneHeight - 2);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (headerWidth, y, getWidth() - headerWidth, compLaneHeight);
        g.setColour (Palette::clip.withAlpha (0.25f));
        g.fillRect (r);

        if (cm.isTakeComp (i))
        {
            // A comp lane shows which take each section uses
            auto tree = cm.getTakesTree().getChild (i);
            for (int k = 0; k < tree.getNumChildren(); ++k)
            {
                auto sec = tree.getChild (k);
                auto times = cm.getSectionTimes (sec);
                const int idx = sec[te::IDs::takeIndex];
                const int sx = juce::jmax (x1, (int) toX (times.getStart())), ex = juce::jmin (x2, (int) toX (times.getEnd()));
                g.setColour (Palette::accent.withAlpha (0.55f));
                if (ex - sx > 1)
                    g.fillRect (sx, y + 1, ex - sx - 1, compLaneHeight - 2);
                if (ex - sx > 10)
                {
                    g.setColour (juce::Colours::white);
                    g.setFont (10.0f);
                    g.drawText (juce::String (idx + 1), sx + 2, y + 1, ex - sx - 4, compLaneHeight - 2, juce::Justification::centredLeft);
                }
            }
        }
        else if (i == active)
        {
            g.setColour (Palette::accent.withAlpha (0.55f));
            g.fillRect (r);
        }

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (10.0f);
        g.drawText (cm.getTakeName (i) + (i == active ? "  (active)" : ""), juce::jmax (headerWidth, x1) + 4, y, 160, compLaneHeight, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRect (r);
    }

    if (swipeTake >= 0 && swipeClipID == w->itemID)
    {
        const int sx = (int) beatToX (juce::jmin (swipeStartBeat, swipeEndBeat)), ex = (int) beatToX (juce::jmax (swipeStartBeat, swipeEndBeat));
        const int sy = y - (cm.getTotalNumTakes() - swipeTake) * compLaneHeight;
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (sx, sy + 1, ex - sx, compLaneHeight - 2);
    }
}

void ArrangementView::compMouseDown (const juce::MouseEvent& e, te::AudioTrack& t, int take)
{
    auto* w = compClipFor (t);
    if (w == nullptr)
        return;

    swipeClipID = w->itemID;
    swipeTake = take;
    swipeStartBeat = swipeEndBeat = snapBeat (xToBeat (e.x), e.mods.isAltDown());
    dragMode = DragMode::compSwipe;
    edit.getUndoManager().beginNewTransaction();
    repaint();
}

std::vector<ArrangementView::ExpandedLane> ArrangementView::expandedLanes (te::AudioTrack& t) const
{
    std::vector<ExpandedLane> lanes;
    auto add = [this, &lanes] (te::AutomatableParameter* p, const juce::String& label)
    {
        if (p == nullptr)
            return;
        for (auto& l : lanes)
            if (l.param == p)
                return;
        lanes.push_back ({ p, label });
    };

    if (auto* vp = t.getVolumePlugin())
    {
        add (vp->volParam.get(), "Volume");
        add (vp->panParam.get(), "Pan");
    }

    const auto pins = pinnedParams.find (t.itemID);

    for (auto* plugin : t.pluginList)
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (plugin) != nullptr)
            continue;

        auto params = plugin->getAutomatableParameters();
        for (int i = 0; i < params.size() && lanes.size() < 24; ++i)
            if (params[i]->getCurve().getNumPoints() > 0
                || (pins != pinnedParams.end() && pins->second.count ({ plugin->itemID, i }) > 0))
                add (params[i], plugin->getName() + ": " + params[i]->getParameterName());
    }

    return lanes;
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
    const int maxScrollY = juce::jmax (0, totalTracksHeight() - (getHeight() - rulerHeight) + trackHeight);

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
    if (! marqueeRect.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (marqueeRect);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawRect (marqueeRect, 1);
    }

    paintPlayhead (g);
}

void ArrangementView::paintGrid (juce::Graphics& g, juce::Rectangle<int> lanes)
{
    const auto tracks = te::getAudioTracks (edit);

    for (int i = 0; i < tracks.size(); ++i)
    {
        g.setColour (i % 2 == 0 ? Palette::laneA : Palette::laneB);
        g.fillRect (lanes.getX(), trackY (i), lanes.getWidth(), rowHeight (i));
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

        if (isExpanded (*tracks[i]))
        {
            int y = trackY (i) + trackHeight;
            for (auto& l : expandedLanes (*tracks[i]))
            {
                const int h = laneH (l.param);
                if (h == collapsedLaneHeight)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.2f));
                    g.fillRect (headerWidth, y, getWidth() - headerWidth, h);
                    g.setColour (juce::Colours::white.withAlpha (0.45f));
                    g.drawHorizontalLine (y + h - 1, (float) headerWidth, (float) getWidth());
                }
                else
                    paintAutomationLane (g, *l.param, { headerWidth, y, getWidth() - headerWidth, h }, l.label);
                y += h;
            }
        }

        paintCompLanes (g, *tracks[i], trackY (i) + trackHeight + autoRowsHeight (*tracks[i]));
    }
}

void ArrangementView::paintClip (juce::Graphics& g, te::Clip& clip, juce::Rectangle<int> b)
{
    const bool selected = isClipSelected (clip);
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
        if (std::abs (waveClip->getPitchChange()) > 0.001f)
            label << "  " << juce::String::formatted ("%+d st", juce::roundToInt (waveClip->getPitchChange()));
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

            if (! wave->getAudioFile().getFile().existsAsFile())       // proxy (reverse / pitch / stretch) still being rendered
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ("rendering...", body, juce::Justification::centred);
                repaint (b);
            }
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
            const auto bar = juce::Rectangle<int> (x1, ruler.getY() + loopBarTop, juce::jmax (0, x2 - x1), rulerHeight - loopBarTop - 1);
            g.setColour (Palette::accent.withAlpha (tr.looping.get() ? 0.7f : 0.3f));
            g.fillRect (bar);
            g.setColour (Palette::accent);
            g.fillRect (bar.withWidth (3));
            g.fillRect (bar.withTrimmedLeft (bar.getWidth() - 3));
        }
        else
        {
            g.setColour (Palette::textDim.withAlpha (0.6f));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("drag here to set loop", ruler.withTrimmedTop (loopBarTop).withTrimmedLeft (6), juce::Justification::centredLeft);
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
        auto r = juce::Rectangle<int> (0, trackY (i), headerWidth, rowHeight (i));

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
            const bool fx = pluginAutoParam.count (t->itemID) > 0 && getAutoParam (*t) != nullptr
                            && getAutoParam (*t) != (pan ? t->getVolumePlugin()->panParam.get() : t->getVolumePlugin()->volParam.get());
            drawButton (HeaderButton::param, fx ? "FX" : pan ? "Pan" : "Vol", true, Palette::accent.withAlpha (0.7f));
        }

        {
            auto br = headerButtonRect (i, HeaderButton::expand).toFloat();
            const bool open = isExpanded (*t);
            g.setColour (open ? Palette::accent : Palette::background);
            g.fillRoundedRectangle (br, 3.0f);
            juce::Path chevron;
            const auto cx = br.getCentreX(), cy = br.getCentreY();
            if (open)  chevron.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
            else       chevron.addTriangle (cx - 2.0f, cy - 4.0f, cx - 2.0f, cy + 4.0f, cx + 3.0f, cy);
            g.setColour (open ? juce::Colours::black : Palette::textDim);
            g.fillPath (chevron);
        }

        if (autoOn)
        {
            if (auto* ap = getAutoParam (*t))
            {
                g.setColour (Palette::accent.withAlpha (0.85f));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (ap->getParameterName(), 10, trackY (i) + 60, headerWidth - 18, 16, juce::Justification::centredLeft, true);
            }
        }

        if (isExpanded (*t))
        {
            int ly = trackY (i) + trackHeight;
            for (auto& l : expandedLanes (*t))
            {
                const int lh = laneH (l.param);
                const bool collapsed = lh == collapsedLaneHeight;
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRect (4, ly, headerWidth - 4, lh);
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawHorizontalLine (ly + lh - 1, 0.0f, (float) headerWidth);
                g.setColour (collapsed ? Palette::textDim.withAlpha (0.7f) : Palette::textDim);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (l.label, 10, ly + 2, headerWidth - 40, 16, juce::Justification::centredLeft, true);

                auto hb = juce::Rectangle<float> ((float) headerWidth - 24.0f, (float) ly + 2.0f, 16.0f, 16.0f);
                g.setColour (Palette::background);
                g.fillRoundedRectangle (hb, 3.0f);
                juce::Path chevron;
                const auto cx = hb.getCentreX(), cy = hb.getCentreY();
                if (collapsed)  chevron.addTriangle (cx - 2.0f, cy - 4.0f, cx - 2.0f, cy + 4.0f, cx + 3.0f, cy);
                else            chevron.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
                g.setColour (Palette::textDim);
                g.fillPath (chevron);
                ly += lh;
            }
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
        case HeaderButton::expand:      return { headerWidth - 26, trackY (trackIndex) + 8, 18, 18 };
        case HeaderButton::none:        break;
    }
    return {};
}

ArrangementView::HeaderButton ArrangementView::hitTestHeaderButton (juce::Point<int> p, int trackIndex) const
{
    for (auto b : { HeaderButton::mute, HeaderButton::solo, HeaderButton::arm, HeaderButton::automation, HeaderButton::param, HeaderButton::expand })
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

    if (isExpanded (*t) && e.x >= headerWidth - 28)
    {
        int top = 0;
        const int rel = e.y - trackY (index) - trackHeight;
        const int k = laneAt (*t, rel, top);
        if (k >= 0 && rel - top < 22)
        {
            auto* param = expandedLanes (*t)[(size_t) k].param;
            if (hiddenLanes.erase (param) == 0)
                hiddenLanes.insert (param);
            repaint();
            return;
        }
    }

    edit.getUndoManager().beginNewTransaction();

    switch (hitTestHeaderButton (e.getPosition(), index))
    {
        case HeaderButton::expand:
            if (! expandedTracks.erase (t->itemID))
            {
                expandedTracks.insert (t->itemID);
                hiddenLanes.clear();
            }
            break;
        case HeaderButton::mute:        t->setMute (! t->isMuted (false)); break;
        case HeaderButton::solo:        t->setSolo (! t->isSolo (false)); break;
        case HeaderButton::arm:         toggleArm (*t); break;
        case HeaderButton::automation:
            if (automationTracks.erase (t->itemID) == 0)
                automationTracks.insert (t->itemID);
            break;
        case HeaderButton::param:       showAutomationParamMenu (*t); break;
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
    m.addItem (5, "Freeze Track (Bounce to New Track)", trackIndex >= 0);
    m.addItem (6, "Show / Hide All Automation Lanes", trackIndex >= 0);
    m.addItem (7, "Show / Hide Automation for All Tracks");
    m.addItem (8, "Show / Hide Take Lanes (comping, selected clip)", trackIndex >= 0);

    m.showMenuAsync ({}, [this, trackIndex] (int r)
    {
        switch (r)
        {
            case 1: addTrack (false); break;
            case 2: addTrack (true); break;
            case 3: beginRename (trackIndex); break;
            case 4: deleteSelectedTrack(); break;
            case 5:
                if (auto* t = te::getAudioTracks (edit)[trackIndex])
                {
                    const auto len = edit.getLength();
                    if (len.inSeconds() > 0.0)
                        bounceToNewTrack (*t, { te::TimePosition(), te::TimePosition() + len }, t->getName(), [id = t->itemID, this]
                        {
                            if (auto* orig = dynamic_cast<te::AudioTrack*> (findTrackForID (edit, id)))
                                orig->setMute (true);
                        });
                }
                break;
            case 6:
                if (auto* t = te::getAudioTracks (edit)[trackIndex])
                    if (expandedTracks.erase (t->itemID) == 0)
                        expandedTracks.insert (t->itemID);
                repaint();
                break;
            case 7: toggleExpandAllAutomation(); break;
            case 8:
                if (auto* t = te::getAudioTracks (edit)[trackIndex])
                {
                    if (! compTracks.erase (t->itemID))
                        compTracks.insert (t->itemID);
                    repaint();
                }
                break;
            default: break;
        }
    });
}

static void askText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);

void ArrangementView::editTempoMarker (int index)
{
    auto* tempo = edit.tempoSequence.getTempo (index);
    if (tempo == nullptr)
        return;

    askText ("Tempo (BPM)", juce::String (tempo->getBpm(), 2), [this, index] (juce::String text)
    {
        const double bpm = text.getDoubleValue();
        if (bpm < 20.0 || bpm > 400.0 || edit.tempoSequence.getTempo (index) == nullptr)
            return;
        edit.getUndoManager().beginNewTransaction();
        edit.tempoSequence.getTempo (index)->setBpm (bpm);
        repaint();
    });
}

void ArrangementView::editSigMarker (int index)
{
    auto* ts = edit.tempoSequence.getTimeSig (index);
    if (ts == nullptr)
        return;

    askText ("Time signature (e.g. 7/8)", ts->getStringTimeSig(), [this, index] (juce::String text)
    {
        if (! text.containsChar ('/') || edit.tempoSequence.getTimeSig (index) == nullptr)
            return;
        edit.getUndoManager().beginNewTransaction();
        edit.tempoSequence.getTimeSig (index)->setStringTimeSig (text.trim());
        repaint();
    });
}

bool ArrangementView::markerAt (int x, int& tempoIndex, int& sigIndex) const
{
    auto& seq = edit.tempoSequence;
    tempoIndex = sigIndex = -1;

    for (int i = 1; i < seq.getNumTempos(); ++i)
        if (std::abs (timeToX (seq.getTempo (i)->getStartTime()) - x) <= 8.0)
            tempoIndex = i;

    for (int i = 1; i < seq.getTimeSigs().size(); ++i)
        if (std::abs (beatToX (seq.getTimeSig (i)->startBeatNumber.get().inBeats()) - x) <= 8.0)
            sigIndex = i;

    return tempoIndex >= 0 || sigIndex >= 0;
}

void ArrangementView::showRulerMenu (const juce::MouseEvent& e)
{
    auto& seq = edit.tempoSequence;
    int tempoIndex, sigIndex;
    markerAt (e.x, tempoIndex, sigIndex);

    const double clickBeat = juce::jmax (0.0, snapBeat (xToBeat (e.x), false));

    juce::PopupMenu m;
    if (tempoIndex >= 0)
    {
        m.addItem (1, "Edit Tempo (" + juce::String (seq.getTempo (tempoIndex)->getBpm(), 1) + " BPM)...");
        m.addItem (3, "Move Tempo Change to Playhead");
        m.addItem (5, "Delete Tempo Change");
    }
    if (sigIndex >= 0)
    {
        m.addItem (2, "Edit Time Signature (" + seq.getTimeSig (sigIndex)->getStringTimeSig() + ")...");
        m.addItem (6, "Delete Time Signature Change");
    }
    if (! m.containsAnyActiveItems() || tempoIndex < 0 || sigIndex < 0)
    {
        m.addSeparator();
        m.addItem (7, "Add Tempo Change Here...");
        m.addItem (8, "Add Time Signature Change Here...");
    }

    m.showMenuAsync ({}, [this, tempoIndex, sigIndex, clickBeat] (int r)
    {
        auto& s = edit.tempoSequence;
        if (r == 1)       editTempoMarker (tempoIndex);
        else if (r == 2)  editSigMarker (sigIndex);
        else if (r == 3)
        {
            edit.getUndoManager().beginNewTransaction();
            const auto b = s.toBeats (edit.getTransport().getPosition());
            if (b.inBeats() > 0.0)
                s.getTempo (tempoIndex)->setStartBeat (b);
        }
        else if (r == 5 || r == 6)
        {
            edit.getUndoManager().beginNewTransaction();
            if (r == 5)  s.removeTempo (tempoIndex, false);
            else         s.removeTimeSig (sigIndex);
        }
        else if (r == 7 && clickBeat > 0.0)
        {
            askText ("New tempo (BPM)", juce::String (s.getBpmAt (beatToTime (clickBeat)), 2), [this, clickBeat] (juce::String text)
            {
                const double bpm = text.getDoubleValue();
                if (bpm < 20.0 || bpm > 400.0)
                    return;
                edit.getUndoManager().beginNewTransaction();
                edit.tempoSequence.insertTempo (te::BeatPosition::fromBeats (clickBeat), bpm, 1.0f);
                repaint();
            });
        }
        else if (r == 8 && clickBeat > 0.0)
        {
            const int bar = beatToBar (clickBeat);
            askText ("Time signature from bar " + juce::String (bar + 1), "4/4", [this, bar] (juce::String text)
            {
                if (! text.containsChar ('/'))
                    return;
                edit.getUndoManager().beginNewTransaction();
                auto start = edit.tempoSequence.toBeats (te::tempo::BarsAndBeats { bar, te::BeatDuration() });
                if (auto ts = edit.tempoSequence.insertTimeSig (start))
                    ts->setStringTimeSig (text.trim());
                repaint();
            });
        }
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

void ArrangementView::bounceToNewTrack (te::AudioTrack& src, te::TimeRange range, const juce::String& name,
                                        std::function<void()> muteOriginal, te::Clip* onlyClip)
{
    edit.getTransport().stop (false, false);

    te::Renderer::Parameters p (edit);
    p.destFile = edit.getTempDirectory (true).getNonexistentChildFile ("bounce", ".wav");
    p.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = 24;
    p.time = { range.getStart(), range.getEnd() + te::TimeDuration::fromSeconds (1.5) };      // tail for reverbs/delays
    p.usePlugins = true;
    p.useMasterPlugins = false;
    p.tracksToDo.setBit (te::getAllTracks (edit).indexOf (&src));

    if (onlyClip != nullptr)
        p.allowedClips.add (onlyClip);

    juce::File out;
    {
        ScopedNoProxies noProxies (edit);
        out = te::Renderer::renderToFile ("Bounce", p);
    }


    if (! out.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Bounce",
                                                "Nothing to bounce: the track is silent (a MIDI track needs an instrument).");
        return;
    }

    edit.getUndoManager().beginNewTransaction();

    if (auto t = edit.insertNewAudioTrack (te::TrackInsertPoint (nullptr, &src), nullptr))
    {
        t->setName (name + " (bounce)");
        // The render has a 1.5 s tail for reverbs/delays; only keep as much of it as is actually audible (> -60 dB)
        double length = range.getLength().inSeconds();
        te::AudioFile af (edit.engine, out);
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (out)))
        {
            const auto total = reader->lengthInSamples;
            const auto from = juce::jmin (total, (juce::int64) (length * reader->sampleRate));
            juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) (total - from));

            if (buf.getNumSamples() > 0 && reader->read (&buf, 0, buf.getNumSamples(), from, true, true))
                for (int i = buf.getNumSamples() - 1; i >= 0; --i)
                {
                    bool loud = false;
                    for (int ch = 0; ch < buf.getNumChannels() && ! loud; ++ch)
                        loud = std::abs (buf.getSample (ch, i)) > 0.001f;

                    if (loud)
                    {
                        length += (i + 1) / reader->sampleRate;
                        break;
                    }
                }
        }

        length = juce::jmin (length, af.getLength());
        t->insertWaveClip (name, out, { { range.getStart(), te::TimeDuration::fromSeconds (length) }, {} }, false);
        selectedTrackID = t->itemID;
    }

    if (muteOriginal)
        muteOriginal();

    repaint();
}

void ArrangementView::consolidateClip (te::EditItemID id)
{
    auto* clip = te::findClipForID (edit, id);
    auto* track = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr;
    if (track == nullptr)
        return;

    edit.getTransport().stop (false, false);
    const auto range = clip->getEditTimeRange();
    const auto name = clip->getName();

    te::Renderer::Parameters p (edit);
    p.destFile = edit.getTempDirectory (true).getNonexistentChildFile ("consolidated", ".wav");
    p.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    p.bitDepth = 24;
    p.time = range;
    p.usePlugins = true;
    p.useMasterPlugins = false;
    p.tracksToDo.setBit (te::getAllTracks (edit).indexOf (track));
    p.allowedClips.add (clip);

    juce::File out;
    {
        ScopedNoProxies noProxies (edit);
        out = te::Renderer::renderToFile ("Consolidate", p);
    }

    if (! out.existsAsFile())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Consolidate",
                                                "Nothing to consolidate: the clip is silent (a MIDI clip needs an instrument on its track).");
        return;
    }

    edit.getUndoManager().beginNewTransaction();
    clip->removeFromParent();

    te::AudioFile af (edit.engine, out);
    if (auto nc = track->insertWaveClip (name + " (consolidated)", out,
                                         { { range.getStart(), te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false))
        selectedClipID = nc->itemID;

    repaint();
}

void ArrangementView::crossfadeIntoNext (te::Clip& clip)
{
    auto* w = dynamic_cast<te::WaveAudioClip*> (&clip);
    auto* track = dynamic_cast<te::ClipTrack*> (clip.getTrack());

    if (w == nullptr || track == nullptr)
        return;

    te::WaveAudioClip* next = nullptr;
    for (auto* other : track->getClips())
        if (auto* ow = dynamic_cast<te::WaveAudioClip*> (other))
            if (ow != w && ow->getPosition().getStart() >= w->getPosition().getStart()
                && (next == nullptr || ow->getPosition().getStart() < next->getPosition().getStart()))
                next = ow;

    if (next == nullptr)
        return;

    auto overlap = w->getPosition().getEnd() - next->getPosition().getStart();

    if (overlap.inSeconds() <= 0.0)      // butted up: extend this clip under the next one
    {
        overlap = te::TimeDuration::fromSeconds (0.1);
        w->setEnd (w->getPosition().getEnd() + overlap, false);
    }

    w->setFadeOut (overlap);
    next->setFadeIn (overlap);
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

void ArrangementView::toggleExpandAllAutomation()
{
    const auto tracks = te::getAudioTracks (edit);
    const bool anyCollapsed = std::any_of (tracks.begin(), tracks.end(), [this] (auto* t) { return ! isExpanded (*t); });

    expandedTracks.clear();
    if (anyCollapsed)
        for (auto* t : tracks)
            expandedTracks.insert (t->itemID);

    scrollY = juce::jlimit (0, juce::jmax (0, totalTracksHeight() - (getHeight() - rulerHeight) + trackHeight), scrollY);
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

void ArrangementView::showAutomationParamMenu (te::AudioTrack& t)
{
    juce::PopupMenu m;
    m.addItem (1, "Volume");
    m.addItem (2, "Pan");

    auto targets = std::make_shared<std::vector<std::pair<te::EditItemID, int>>>();
    constexpr int base = 100;

    for (auto* plugin : t.pluginList)
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (plugin) != nullptr
            || plugin->getAutomatableParameters().isEmpty())
            continue;

        juce::PopupMenu sub;
        auto params = plugin->getAutomatableParameters();
        for (int i = 0; i < juce::jmin (params.size(), 300); ++i)
        {
            sub.addItem (base + (int) targets->size(), params[i]->getParameterName());
            targets->push_back ({ plugin->itemID, i });
        }

        if (sub.getNumItems() > 0)
        {
            if (m.getNumItems() == 2)
                m.addSeparator();
            m.addSubMenu (plugin->getName(), sub);
        }
    }

    const auto id = t.itemID;
    m.showMenuAsync ({}, [this, id, targets] (int r)
    {
        if (r == 0)
            return;

        pluginAutoParam.erase (id);

        if (r == 1 || r == 2)
            automationParam[id] = r - 1;
        else if (r - base < (int) targets->size())
        {
            pluginAutoParam[id] = (*targets)[(size_t) (r - base)];
            pinnedParams[id].insert ((*targets)[(size_t) (r - base)]);
        }

        repaint();
    });
}

te::AutomatableParameter* ArrangementView::getAutoParam (te::AudioTrack& t) const
{
    auto* vp = t.getVolumePlugin();
    if (vp == nullptr)
        return nullptr;

    if (auto sel = pluginAutoParam.find (t.itemID); sel != pluginAutoParam.end())
        for (auto* plugin : t.pluginList)
            if (plugin->itemID == sel->second.first)
            {
                auto params = plugin->getAutomatableParameters();
                if (juce::isPositiveAndBelow (sel->second.second, params.size()))
                    return params[sel->second.second];
            }

    auto it = automationParam.find (t.itemID);
    return (it != automationParam.end() && it->second == 1) ? vp->panParam.get() : vp->volParam.get();
}

float ArrangementView::autoValueToY (te::AutomatableParameter& p, float v, juce::Rectangle<int> lane) const
{
    const auto range = p.getValueRange();
    const float norm = juce::jlimit (0.0f, 1.0f, (v - range.getStart()) / range.getLength());
    const int top = lane.getY() + 8, h = lane.getHeight() - 16;
    return (float) top + (1.0f - norm) * (float) h;
}

float ArrangementView::autoYToValue (te::AutomatableParameter& p, int y, juce::Rectangle<int> lane) const
{
    const auto range = p.getValueRange();
    const int top = lane.getY() + 8, h = lane.getHeight() - 16;
    const float norm = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (y - top) / (float) h);
    return range.getStart() + norm * range.getLength();
}

int ArrangementView::hitTestAutoPoint (te::AutomatableParameter& p, juce::Rectangle<int> lane, juce::Point<int> pos) const
{
    auto& curve = p.getCurve();

    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const float x = (float) timeToX (curve.getPointTime (i));
        const float y = autoValueToY (p, curve.getPointValue (i), lane);

        if (std::abs (x - (float) pos.x) <= 7.0f && std::abs (y - (float) pos.y) <= 7.0f)
            return i;
    }
    return -1;
}

void ArrangementView::paintAutomation (juce::Graphics& g, te::AudioTrack& t, int trackIndex)
{
    if (auto* p = getAutoParam (t))
        paintAutomationLane (g, *p, { headerWidth, trackY (trackIndex), getWidth() - headerWidth, trackHeight }, p->getParameterName());
}

void ArrangementView::paintAutomationLane (juce::Graphics& g, te::AutomatableParameter& param, juce::Rectangle<int> lane,
                                           const juce::String& label)
{
    auto* p = &param;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (lane);
    juce::ignoreUnused (label);     // lane names are drawn in the track header, outside the envelope
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawHorizontalLine (lane.getBottom() - 1, (float) lane.getX(), (float) lane.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawHorizontalLine (lane.getY(), (float) lane.getX(), (float) lane.getRight());

    auto& curve = p->getCurve();
    const int n = curve.getNumPoints();

    if (p == selParam && selEndBeat > selStartBeat)
    {
        const int sx1 = (int) beatToX (selStartBeat), sx2 = (int) beatToX (selEndBeat);
        g.setColour (Palette::accent.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<int> (sx1, lane.getY(), sx2 - sx1, lane.getHeight() - 1));
    }

    juce::Path path;
    if (n == 0)
    {
        const float y = autoValueToY (*p, p->getCurrentValue(), lane);
        path.startNewSubPath ((float) lane.getX(), y);
        path.lineTo ((float) lane.getRight(), y);
    }
    else
    {
        bool first = true;
        for (int x = lane.getX(); x <= lane.getRight(); x += 3)
        {
            const float v = curve.getValueAt (xToTime ((double) x), p->getCurrentValue());
            const float y = autoValueToY (*p, v, lane);
            if (first)  { path.startNewSubPath ((float) x, y); first = false; }
            else        path.lineTo ((float) x, y);
        }
    }

    g.setColour (Palette::accent);
    g.strokePath (path, juce::PathStrokeType (n == 0 ? 1.0f : 2.0f));

    for (int i = 0; i < n; ++i)
    {
        const float x = (float) timeToX (curve.getPointTime (i));
        const float y = autoValueToY (*p, curve.getPointValue (i), lane);
        g.setColour (Palette::background);
        g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
        g.setColour (Palette::accent);
        g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 2.0f);
    }
}

void ArrangementView::automationMouseDown (const juce::MouseEvent& e, te::AudioTrack& t, te::AutomatableParameter& param,
                                           juce::Rectangle<int> lane, bool extraLane)
{
    auto* p = &param;
    auto& curve = p->getCurve();
    auto& um = edit.getUndoManager();
    um.beginNewTransaction();
    lastLaneParam = p;

    if (extraLane && e.y >= lane.getBottom() - 4 && ! e.mods.isPopupMenu())
    {
        dragMode = DragMode::laneResize;
        resizeStartY = e.y;
        resizeStartHeight = autoLaneHeight;
        return;
    }

    const int hit = hitTestAutoPoint (*p, lane, e.getPosition());

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)   showPointMenu (*p, hit);
        else            showLaneMenu (*p);
        return;
    }

    if (hit >= 0 && e.mods.isAltDown())
    {
        curve.removePoint (hit, &um);
        repaint();
        return;
    }

    dragAutoTrack = t.itemID;
    dragParam = p;
    dragLane = lane;

    if (hit < 0 && e.mods.isShiftDown())
    {
        dragMode = DragMode::autoSelect;
        selParam = p;
        selAnchorBeat = juce::jmax (0.0, snapBeat (xToBeat (e.x), false));
        selStartBeat = selEndBeat = selAnchorBeat;
        repaint();
        return;
    }

    selParam = nullptr;

    if (hit >= 0)
    {
        dragAutoIndex = hit;
    }
    else
    {
        const auto time = beatToTime (juce::jmax (0.0, snapBeat (xToBeat (e.x), e.mods.isAltDown())));
        dragAutoIndex = curve.addPoint (time, autoYToValue (*p, e.y, lane), 0.0f, &um);
    }

    dragMode = DragMode::automation;
    repaint();
}

bool ArrangementView::isLiveLane (te::AutomatableParameter* p) const
{
    if (p == nullptr)
        return false;

    for (auto* t : te::getAudioTracks (edit))
    {
        if (getAutoParam (*t) == p && automationTracks.count (t->itemID) > 0)
            return true;

        for (auto& l : expandedLanes (*t))
            if (l.param == p && isExpanded (*t))
                return true;
    }

    return false;
}

void ArrangementView::copyAutoSelection (bool cut)
{
    if (! isLiveLane (selParam) || selEndBeat <= selStartBeat)
        return;

    autoClipboard.clear();
    auto& curve = selParam->getCurve();
    const auto range = selParam->getValueRange();

    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const double b = timeToBeat (curve.getPointTime (i));
        if (b >= selStartBeat - 1.0e-6 && b <= selEndBeat + 1.0e-6)
            autoClipboard.push_back ({ b - selStartBeat, (curve.getPointValue (i) - range.getStart()) / range.getLength(),
                                       curve.getPointCurve (i) });
    }

    if (cut)
        deleteAutoSelection();
}

void ArrangementView::deleteAutoSelection()
{
    if (! isLiveLane (selParam) || selEndBeat <= selStartBeat)
        return;

    edit.getUndoManager().beginNewTransaction();
    selParam->getCurve().removePointsInRegion ({ beatToTime (selStartBeat - 1.0e-6), beatToTime (selEndBeat + 1.0e-6) }, &edit.getUndoManager());
    repaint();
}

void ArrangementView::pasteAuto()
{
    auto* target = isLiveLane (selParam) ? selParam : lastLaneParam;

    if (autoClipboard.empty() || ! isLiveLane (target))
        return;

    auto& um = edit.getUndoManager();
    um.beginNewTransaction();
    const double at = timeToBeat (edit.getTransport().getPosition());
    const auto range = target->getValueRange();
    const double span = autoClipboard.back().beatOffset;

    target->getCurve().removePointsInRegion ({ beatToTime (at - 1.0e-6), beatToTime (at + span + 1.0e-6) }, &um);

    for (auto& pt : autoClipboard)
        target->getCurve().addPoint (beatToTime (at + pt.beatOffset), range.getStart() + pt.norm * range.getLength(), pt.curve, &um);

    selParam = target;
    selStartBeat = at;
    selEndBeat = at + span;
    repaint();
}

void ArrangementView::showPointMenu (te::AutomatableParameter& param, int index)
{
    juce::PopupMenu m;
    m.addItem (1, "Linear");
    m.addItem (2, "Ease in (slow start)");
    m.addItem (3, "Ease out (slow end)");
    m.addItem (4, "Hold (step)");
    m.addSeparator();
    m.addItem (5, "Delete point");

    auto* p = &param;
    m.showMenuAsync ({}, [this, p, index] (int r)
    {
        if (r == 0 || ! isLiveLane (p) || index >= p->getCurve().getNumPoints())
            return;

        auto& um = edit.getUndoManager();
        um.beginNewTransaction();

        if (r == 5)       p->getCurve().removePoint (index, &um);
        else              p->getCurve().setCurveValue (index, r == 1 ? 0.0f : r == 2 ? 0.7f : r == 3 ? -0.7f : 1.0f, &um);

        repaint();
    });
}

void ArrangementView::showLaneMenu (te::AutomatableParameter& param)
{
    juce::PopupMenu m;
    m.addItem (1, "Collapse this lane");
    m.addItem (2, "Expand all collapsed lanes (" + juce::String ((int) hiddenLanes.size()) + ")", ! hiddenLanes.empty());
    m.addItem (3, "Paste at playhead", ! autoClipboard.empty());
    m.addItem (4, "Clear all points in this lane", param.getCurve().getNumPoints() > 0);

    auto* p = &param;
    m.showMenuAsync ({}, [this, p] (int r)
    {
        if (r == 1)       hiddenLanes.insert (p);
        else if (r == 2)  hiddenLanes.clear();
        else if (r == 3)  { lastLaneParam = p; selParam = nullptr; pasteAuto(); }
        else if (r == 4 && isLiveLane (p))
        {
            edit.getUndoManager().beginNewTransaction();
            p->getCurve().clear (&edit.getUndoManager());
        }

        repaint();
    });
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
    auto& known = thumbnailFiles[clip.itemID];

    if (thumb == nullptr || known != file.getFile())      // reverse / stretch / pitch swap the clip's source for a proxy file
    {
        thumb = std::make_unique<te::SmartThumbnail> (edit.engine, file, *this, &edit);
        known = file.getFile();
    }

    return thumb.get();
}

//==============================================================================
// Model / timer callbacks

void ArrangementView::handleAsyncUpdate()
{
    // Drop thumbnails for clips that no longer exist
    for (auto it = thumbnails.begin(); it != thumbnails.end();)
        it = te::findClipForID (edit, it->first) == nullptr ? thumbnails.erase (it) : std::next (it);

    saveLayout();
    repaint();
}

// Lane layout lives in the Edit's state (non-undoable) so it is saved with the project.
static juce::String idsToString (const std::set<te::EditItemID>& ids)
{
    juce::StringArray a;
    for (auto id : ids)
        a.add (id.toString());
    return a.joinIntoString (",");
}

static std::set<te::EditItemID> idsFromString (const juce::String& s)
{
    std::set<te::EditItemID> out;
    for (auto& t : juce::StringArray::fromTokens (s, ",", {}))
        if (auto id = te::EditItemID::fromString (t); id.isValid())
            out.insert (id);
    return out;
}

void ArrangementView::saveLayout()
{
    auto ui = editState.getOrCreateChildWithName ("ARRANGERUI", nullptr);
    ui.setProperty ("laneHeight", autoLaneHeight, nullptr);
    ui.setProperty ("expanded", idsToString (expandedTracks), nullptr);
    ui.setProperty ("automation", idsToString (automationTracks), nullptr);
    ui.setProperty ("comp", idsToString (compTracks), nullptr);

    juce::StringArray collapsed;                     // "<trackID>:<lane index>"
    for (auto id : expandedTracks)
        if (auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, id)))
        {
            auto lanes = expandedLanes (*t);
            for (int i = 0; i < (int) lanes.size(); ++i)
                if (hiddenLanes.count (lanes[(size_t) i].param) > 0)
                    collapsed.add (id.toString() + ":" + juce::String (i));
        }

    ui.setProperty ("collapsed", collapsed.joinIntoString (","), nullptr);
}

void ArrangementView::loadLayout()
{
    auto ui = editState.getChildWithName ("ARRANGERUI");

    if (! ui.isValid())
        return;

    autoLaneHeight = juce::jlimit (36, 240, (int) ui.getProperty ("laneHeight", autoLaneHeight));
    expandedTracks = idsFromString (ui["expanded"]);
    automationTracks = idsFromString (ui["automation"]);
    compTracks = idsFromString (ui["comp"]);

    for (auto& s : juce::StringArray::fromTokens (ui["collapsed"].toString(), ",", {}))
        if (auto id = te::EditItemID::fromString (s.upToLastOccurrenceOf (":", false, false)); id.isValid())
            if (auto* t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (edit, id)))
            {
                auto lanes = expandedLanes (*t);
                const int i = s.fromLastOccurrenceOf (":", false, false).getIntValue();
                if (juce::isPositiveAndBelow (i, (int) lanes.size()))
                    hiddenLanes.insert (lanes[(size_t) i].param);
            }
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
    if (e.y < rulerHeight && e.x >= headerWidth)
    {
        int ti, si;
        if (markerAt (e.x, ti, si))
        {
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    if (e.y >= loopBarTop && e.y < rulerHeight && e.x >= headerWidth)
    {
        const auto lr = edit.getTransport().getLoopRange();
        const bool nearEdge = lr.getLength().inSeconds() > 0.0
                              && (std::abs (timeToX (lr.getStart()) - e.x) <= 6.0 || std::abs (timeToX (lr.getEnd()) - e.x) <= 6.0);
        setMouseCursor (nearEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
        return;
    }

    if (e.y >= rulerHeight && e.x >= headerWidth)
    {
        const int ti = yToTrackIndex (e.y);
        auto tr = te::getAudioTracks (edit);
        if (juce::isPositiveAndBelow (ti, tr.size()) && isExpanded (*tr[ti]))
        {
            const int rel = e.y - trackY (ti) - trackHeight;
            int top = 0;
            const int k = laneAt (*tr[ti], rel, top);
            if (k >= 0 && rel - top >= laneH (expandedLanes (*tr[ti])[(size_t) k].param) - 4
                && hiddenLanes.count (expandedLanes (*tr[ti])[(size_t) k].param) == 0)
            {
                setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
                return;
            }
        }
    }

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
        case DragMode::loopStart:
        case DragMode::loopEnd:
        case DragMode::loopMove:
        case DragMode::laneResize:
        case DragMode::autoSelect:
        case DragMode::compSwipe:
        case DragMode::markerMove:
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

            const int rel = e.y - trackY (index);

            if (rel >= trackHeight && isExpanded (*tracks[index]) && rel < trackHeight + autoRowsHeight (*tracks[index]))
            {
                auto lanes = expandedLanes (*tracks[index]);
                int top = 0;
                const int k = laneAt (*tracks[index], rel - trackHeight, top);
                if (k >= 0 && hiddenLanes.count (lanes[(size_t) k].param) == 0)
                    automationMouseDown (e, *tracks[index], *lanes[(size_t) k].param,
                                         { headerWidth, trackY (index) + trackHeight + top, getWidth() - headerWidth, autoLaneHeight }, true);
                return;
            }

            if (const int compStart = trackHeight + autoRowsHeight (*tracks[index]); rel >= compStart && compRows (*tracks[index]) > 0)
            {
                compMouseDown (e, *tracks[index], (rel - compStart) / compLaneHeight);
                return;
            }

            if (automationTracks.count (tracks[index]->itemID) > 0)
            {
                if (auto* p = getAutoParam (*tracks[index]))
                    automationMouseDown (e, *tracks[index], *p, { headerWidth, trackY (index), getWidth() - headerWidth, trackHeight }, false);
                return;
            }
        }
    }

    if (e.y < rulerHeight && e.x >= headerWidth && e.mods.isPopupMenu())
    {
        showRulerMenu (e);
        return;
    }

    if (e.y < rulerHeight && e.x >= headerWidth && ! e.mods.isShiftDown())
    {
        int ti, si;
        if (markerAt (e.x, ti, si))
        {
            dragMarkerTempo = ti;
            dragMarkerSig = ti >= 0 ? -1 : si;
            dragMode = DragMode::markerMove;
            edit.getUndoManager().beginNewTransaction();
            return;
        }
    }

    if (e.y >= loopBarTop && e.y < rulerHeight && e.x >= headerWidth && ! e.mods.isShiftDown())
    {
        auto& tr = edit.getTransport();
        const auto lr = tr.getLoopRange();
        const double s = timeToBeat (lr.getStart()), en = timeToBeat (lr.getEnd());
        const double b = xToBeat (e.x);
        const double grab = 6.0 / pixelsPerBeat;
        loopMoved = false;

        if (lr.getLength().inSeconds() > 0.0 && std::abs (b - s) <= grab)             dragMode = DragMode::loopStart;
        else if (lr.getLength().inSeconds() > 0.0 && std::abs (b - en) <= grab)       dragMode = DragMode::loopEnd;
        else if (lr.getLength().inSeconds() > 0.0 && b > s && b < en)
        {
            dragMode = DragMode::loopMove;
            loopGrabBeat = b;
            loopOrigStart = s;
            loopOrigEnd = en;
        }
        else
        {
            dragMode = DragMode::loopRange;
            loopAnchorBeat = juce::jmax (0.0, snapBeat (b, e.mods.isAltDown()));
        }
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
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown();

    if (hit.clip == nullptr)
    {
        if (! additive)
        {
            selectedClipID = {};
            multiSel.clear();
        }

        marqueeBase = multiSel;
        marqueeStart = e.getPosition();
        marqueeRect = {};
        dragMode = DragMode::marquee;
        repaint();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        if (! isClipSelected (*hit.clip))
            multiSel.clear();
        selectedClipID = hit.clip->itemID;
        repaint();
        showClipMenu (hit.clip->itemID);
        return;
    }

    if (additive)
    {
        if (selectedClipID.isValid())
            multiSel.insert (selectedClipID);

        if (multiSel.erase (hit.clip->itemID) == 0)
        {
            multiSel.insert (hit.clip->itemID);
            selectedClipID = hit.clip->itemID;
        }
        else if (selectedClipID == hit.clip->itemID)
            selectedClipID = multiSel.empty() ? te::EditItemID() : *multiSel.begin();

        repaint();
        return;
    }

    if (! multiSel.empty() && multiSel.count (hit.clip->itemID) > 0 && hit.mode == DragMode::move)
        ;                                                // drag the whole selection
    else
        multiSel.clear();

    selectedClipID = hit.clip->itemID;

    groupOrigStart.clear();
    for (auto id : multiSel)
        if (auto* oc = te::findClipForID (edit, id))
            groupOrigStart[id] = timeToBeat (oc->getPosition().getStart());

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

    if (dragMode == DragMode::markerMove)
    {
        auto& seq = edit.tempoSequence;
        const double b = juce::jmax (1.0, snapBeat (xToBeat (e.x), e.mods.isAltDown()));

        if (dragMarkerTempo > 0 && dragMarkerTempo < seq.getNumTempos())
            seq.getTempo (dragMarkerTempo)->setStartBeat (te::BeatPosition::fromBeats (b));
        else if (dragMarkerSig > 0 && dragMarkerSig < seq.getTimeSigs().size())
        {
            const int bar = beatToBar (b);        // time signatures always start on a bar line
            seq.getTimeSig (dragMarkerSig)->startBeatNumber = seq.toBeats (te::tempo::BarsAndBeats { bar, te::BeatDuration() });
        }

        repaint();
        return;
    }

    if (dragMode == DragMode::marquee)
    {
        marqueeRect = juce::Rectangle<int>::leftTopRightBottom (juce::jmin (marqueeStart.x, e.x), juce::jmin (marqueeStart.y, e.y),
                                                                juce::jmax (marqueeStart.x, e.x), juce::jmax (marqueeStart.y, e.y));
        multiSel = marqueeBase;
        const auto tracks = te::getAudioTracks (edit);

        for (int i = 0; i < tracks.size(); ++i)
            for (auto* clip : tracks[i]->getClips())
                if (getClipBounds (*clip, i).intersects (marqueeRect))
                    multiSel.insert (clip->itemID);

        selectedClipID = multiSel.size() == 1 ? *multiSel.begin() : te::EditItemID();
        if (multiSel.size() == 1)
            multiSel.clear();
        repaint();
        return;
    }

    if (dragMode == DragMode::compSwipe)
    {
        swipeEndBeat = snapBeat (xToBeat (e.x), e.mods.isAltDown());
        repaint();
        return;
    }

    if (dragMode == DragMode::laneResize)
    {
        autoLaneHeight = juce::jlimit (36, 240, resizeStartHeight + e.y - resizeStartY);
        repaint();
        return;
    }

    if (dragMode == DragMode::autoSelect)
    {
        const double b = juce::jmax (0.0, snapBeat (xToBeat (e.x), bypassSnap));
        selStartBeat = juce::jmin (b, selAnchorBeat);
        selEndBeat = juce::jmax (b, selAnchorBeat);
        repaint();
        return;
    }

    if (dragMode == DragMode::automation)
    {
        if (dragParam != nullptr && dragAutoIndex >= 0 && te::findTrackForID (edit, dragAutoTrack) != nullptr)
            dragAutoIndex = dragParam->getCurve().movePoint (*dragParam, dragAutoIndex,
                                                             beatToTime (juce::jmax (0.0, snapBeat (xToBeat (e.x), bypassSnap))),
                                                             autoYToValue (*dragParam, e.y, dragLane), false, &edit.getUndoManager());
        repaint();
        return;
    }

    if (dragMode == DragMode::loopStart || dragMode == DragMode::loopEnd || dragMode == DragMode::loopMove)
    {
        auto& tr = edit.getTransport();
        const auto lr = tr.getLoopRange();
        double s = timeToBeat (lr.getStart()), en = timeToBeat (lr.getEnd());
        const double minLen = getGridBeats();
        loopMoved = true;

        if (dragMode == DragMode::loopStart)
            s = juce::jlimit (0.0, en - minLen, snapBeat (xToBeat (e.x), bypassSnap));
        else if (dragMode == DragMode::loopEnd)
            en = juce::jmax (s + minLen, snapBeat (xToBeat (e.x), bypassSnap));
        else
        {
            const double len = loopOrigEnd - loopOrigStart;
            s = juce::jmax (0.0, snapBeat (loopOrigStart + xToBeat (e.x) - loopGrabBeat, bypassSnap));
            en = s + len;
        }

        tr.setLoopRange ({ beatToTime (s), beatToTime (en) });
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

            if (! multiSel.empty())                       // group move: same offset, same tracks
            {
                for (auto& [id, orig] : groupOrigStart)
                    if (id != dragClipID)
                        if (auto* oc = te::findClipForID (edit, id))
                            oc->setStart (beatToTime (juce::jmax (0.0, orig + (newStart - dragClipStartBeat))), false, true);
                break;
            }

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
        case DragMode::loopStart:
        case DragMode::loopEnd:
        case DragMode::loopMove:
        case DragMode::laneResize:
        case DragMode::autoSelect:
        case DragMode::compSwipe:
        case DragMode::markerMove:
            break;
    }
}

void ArrangementView::setupCompDemo (int trackIndex)
{
    auto tracks = te::getAudioTracks (edit);
    if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
        return;

    for (auto* clip : tracks[trackIndex]->getClips())
        if (auto* w = dynamic_cast<te::WaveAudioClip*> (clip))
        {
            const auto file = w->getOriginalFile();
            w->addTake (file);
            w->addTake (file);
            selectedClipID = w->itemID;
            compTracks.insert (tracks[trackIndex]->itemID);

            const auto pos = w->getPosition();
            swipeClipID = w->itemID;
            swipeTake = 1;
            swipeStartBeat = timeToBeat (pos.getStart()) + 0.5;
            swipeEndBeat = timeToBeat (pos.getStart()) + 1.5;
            commitSwipe();
            swipeTake = -1;
            repaint();
            return;
        }
}

void ArrangementView::commitSwipe()
{
    auto* w = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (edit, swipeClipID));
    if (w == nullptr || swipeTake < 0)
        return;

    auto& cm = w->getCompManager();
    const int take = swipeTake;

    if (cm.isTakeComp (take))
    {
        cm.setActiveTakeIndex (take);        // clicking a comp lane just activates it
        repaint();
        return;
    }

    const bool click = std::abs (swipeEndBeat - swipeStartBeat) * pixelsPerBeat < 4.0;

    if (! cm.isCurrentTakeComp())
    {
        if (click)
        {
            cm.setActiveTakeIndex (take);    // plain click on a take: audition that whole take
            repaint();
            return;
        }

        cm.addNewComp();                     // first swipe creates a comp and makes it active
        cm.setActiveTakeIndex (cm.getTotalNumTakes() - 1);
    }

    const auto range = cm.getCompRange();
    double a = timelineToCompTime (*w, juce::jmin (swipeStartBeat, swipeEndBeat));
    double b = click ? range.getEnd() : timelineToCompTime (*w, juce::jmax (swipeStartBeat, swipeEndBeat));
    a = juce::jlimit (range.getStart(), range.getEnd(), a);
    b = juce::jlimit (range.getStart(), range.getEnd(), b);

    if (b - a > 1.0e-4 || click)
    {
        if (a > range.getStart() + 1.0e-4)  cm.splitSectionAtTime (a);
        if (b < range.getEnd() - 1.0e-4)    cm.splitSectionAtTime (b);

        for (double t = a + 1.0e-4; t < b; )
        {
            cm.changeSectionIndexAtTime (t, take);
            auto sec = cm.findSectionAtTime (t);
            if (! sec.isValid())
                break;
            const double next = cm.getSectionTimes (sec).getEnd();
            if (next <= t)
                break;
            t = next + 1.0e-4;
        }
    }

    repaint();
}

void ArrangementView::deleteSelectedClips()
{
    edit.getUndoManager().beginNewTransaction();

    for (auto id : multiSel)
        if (auto* clip = te::findClipForID (edit, id))
            clip->removeFromParent();

    multiSel.clear();
    selectedClipID = {};
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    if (dragMode == DragMode::loopMove && ! loopMoved)     // a plain click on the loop bar toggles looping
        edit.getTransport().looping = ! edit.getTransport().looping.get();

    if (dragMode == DragMode::compSwipe)
    {
        commitSwipe();
        swipeTake = -1;
    }

    if (dragMode == DragMode::marquee)
    {
        marqueeRect = {};
        repaint();
    }

    saveLayout();
    dragAutoIndex = dragMarkerTempo = dragMarkerSig = -1;
    dragMode = DragMode::none;
    dragClipID = {};
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < rulerHeight && e.x >= headerWidth)
    {
        int ti, si;
        if (markerAt (e.x, ti, si))
        {
            if (ti >= 0)  editTempoMarker (ti);
            else          editSigMarker (si);
        }
        return;
    }

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
        if (juce::isPositiveAndBelow (ti, tr.size()) && (automationTracks.count (tr[ti]->itemID) > 0
                                                         || e.y - trackY (ti) >= trackHeight))
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
    const auto cmd = juce::ModifierKeys::commandModifier;

    if (isLiveLane (selParam) && selEndBeat > selStartBeat)
    {
        if (key == juce::KeyPress ('c', cmd, 0))  { copyAutoSelection (false); return true; }
        if (key == juce::KeyPress ('x', cmd, 0))  { copyAutoSelection (true);  return true; }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteAutoSelection(); return true; }
    }

    if (key == juce::KeyPress ('v', cmd, 0) && ! autoClipboard.empty())
    {
        pasteAuto();
        return true;
    }

    if (key == juce::KeyPress ('a', cmd, 0))
    {
        multiSel.clear();
        for (auto* t : te::getAudioTracks (edit))
            for (auto* cl : t->getClips())
                multiSel.insert (cl->itemID);
        selectedClipID = {};
        repaint();
        return true;
    }

    if (key == juce::KeyPress ('c', cmd, 0) || key == juce::KeyPress ('x', cmd, 0))
    {
        if (copyClips())
        {
            if (key.getKeyCode() == 'x')
                deleteSelectedClips();
            return true;
        }
    }

    if (key == juce::KeyPress ('v', cmd, 0) && ! clipClipboard.empty())
    {
        pasteClips();
        return true;
    }

    if (! multiSel.empty() && key == juce::KeyPress ('d', cmd, 0))
    {
        duplicateSelectedClip();
        return true;
    }

    if (! multiSel.empty() && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
    {
        deleteSelectedClips();
        return true;
    }

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

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        toggleExpandAllAutomation();
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
        if (formats.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr
            || juce::File (f).hasFileExtension ("mid;midi"))
            return true;

    return false;
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto start = beatToTime (juce::jmax (0.0, snapBeat (xToBeat (juce::jmax (x, headerWidth)), false)));
    int trackIndex = juce::jmax (0, yToTrackIndex (y));

    // Multiple files go onto consecutive tracks
    for (auto& f : files)
    {
        if (juce::File (f).hasFileExtension ("mid;midi"))
            importMidiFile (juce::File (f), trackIndex++, start);
        else
            insertAudioFile (juce::File (f), trackIndex++, start);
    }
}

void ArrangementView::importMidiFile (const juce::File& file, int trackIndex, te::TimePosition start)
{
    juce::FileInputStream in (file);
    juce::MidiFile mf;

    if (! in.openedOk() || ! mf.readFrom (in) || mf.getTimeFormat() <= 0)
        return;

    const double ppq = mf.getTimeFormat();
    double endBeat = 0.0;

    struct Note { int pitch, vel; double start, len; };
    struct CC { int type, value; double at; };
    std::vector<Note> notes;
    std::vector<CC> ccs;

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();

        for (auto* ev : seq)
        {
            const auto& m = ev->message;
            const double at = m.getTimeStamp() / ppq;

            if (m.isNoteOn() && ev->noteOffObject != nullptr)
            {
                const double len = juce::jmax (0.0625, ev->noteOffObject->message.getTimeStamp() / ppq - at);
                notes.push_back ({ m.getNoteNumber(), m.getVelocity(), at, len });
                endBeat = std::max (endBeat, at + len);
            }
            else if (m.isController())
                ccs.push_back ({ m.getControllerNumber(), m.getControllerValue(), at });
            else if (m.isPitchWheel())
                ccs.push_back ({ te::MidiControllerEvent::pitchWheelType, m.getPitchWheelValue(), at });
        }
    }

    if (notes.empty() && ccs.empty())
        return;

    const double bar = beatsPerBar();
    endBeat = std::ceil (juce::jmax (endBeat, 1.0) / bar) * bar;

    edit.ensureNumberOfAudioTracks (trackIndex + 1);
    auto* track = te::getAudioTracks (edit)[trackIndex];
    edit.getUndoManager().beginNewTransaction();

    const auto startBeat = edit.tempoSequence.toBeats (start);
    const te::TimeRange range { start, edit.tempoSequence.toTime (startBeat + te::BeatDuration::fromBeats (endBeat)) };

    if (auto clip = track->insertMIDIClip (file.getFileNameWithoutExtension(), range, nullptr))
    {
        auto* um = &edit.getUndoManager();

        for (auto& n : notes)
            clip->getSequence().addNote (n.pitch, te::BeatPosition::fromBeats (n.start), te::BeatDuration::fromBeats (n.len), n.vel, 0, um);

        for (auto& c2 : ccs)
            clip->getSequence().addControllerEvent (te::BeatPosition::fromBeats (c2.at), c2.type, c2.value, um);

        selectedClipID = clip->itemID;
    }

    repaint();
}

void ArrangementView::exportMidiClip (te::MidiClip& clip)
{
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI", juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                                                       .getChildFile (clip.getName() + ".mid"), "*.mid");
    const auto id = clip.itemID;

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, id] (const juce::FileChooser& fc)
                          {
                              auto* mc = dynamic_cast<te::MidiClip*> (te::findClipForID (edit, id));
                              auto f = fc.getResult();

                              if (mc == nullptr || f == juce::File())
                                  return;

                              constexpr int ppq = 960;
                              juce::MidiMessageSequence seq;
                              const auto bpm = edit.tempoSequence.getTempoAt (mc->getPosition().getStart()).getBpm();
                              seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0.0);

                              for (auto* n : mc->getSequence().getNotes())
                              {
                                  const double s = n->getStartBeat().inBeats() * ppq, e = s + n->getLengthBeats().inBeats() * ppq;
                                  seq.addEvent (juce::MidiMessage::noteOn (1, n->getNoteNumber(), (juce::uint8) n->getVelocity()), s);
                                  seq.addEvent (juce::MidiMessage::noteOff (1, n->getNoteNumber()), e);
                              }

                              for (auto* ev : mc->getSequence().getControllerEvents())
                              {
                                  const double at = ev->getBeatPosition().inBeats() * ppq;

                                  if (ev->getType() == te::MidiControllerEvent::pitchWheelType)
                                      seq.addEvent (juce::MidiMessage::pitchWheel (1, ev->getControllerValue()), at);
                                  else if (ev->getType() < 128)
                                      seq.addEvent (juce::MidiMessage::controllerEvent (1, ev->getType(), ev->getControllerValue()), at);
                              }

                              seq.sort();
                              seq.updateMatchedPairs();

                              juce::MidiFile mf;
                              mf.setTicksPerQuarterNote (ppq);
                              mf.addTrack (seq);

                              f = f.withFileExtension ("mid");
                              f.deleteFile();
                              juce::FileOutputStream out (f);
                              if (out.openedOk())
                                  mf.writeTo (out);
                          });
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

std::vector<te::Clip*> ArrangementView::selectedClips() const
{
    std::vector<te::Clip*> out;

    if (multiSel.empty())
    {
        if (auto* c = getSelectedClip())
            out.push_back (c);
    }
    else
        for (auto id : multiSel)
            if (auto* c = te::findClipForID (edit, id))
                out.push_back (c);

    return out;
}

bool ArrangementView::copyClips()
{
    auto clips = selectedClips();

    if (clips.empty())
        return false;

    double first = 1.0e12;
    for (auto* c : clips)
        first = juce::jmin (first, c->getPosition().getStart().inSeconds());

    const auto tracks = te::getAudioTracks (edit);
    clipClipboard.clear();

    for (auto* c : clips)
        clipClipboard.push_back ({ c->state.createCopy(), tracks.indexOf (dynamic_cast<te::AudioTrack*> (c->getTrack())),
                                   c->getPosition().getStart().inSeconds() - first });
    return true;
}

void ArrangementView::pasteClips()
{
    const auto tracks = te::getAudioTracks (edit);
    const auto at = edit.getTransport().getPosition();
    selectedClipID = {};
    multiSel.clear();
    edit.getUndoManager().beginNewTransaction();

    int base = 0;                                       // paste on the selected track if the copy came from one track
    bool singleTrack = true;
    for (auto& e : clipClipboard)
        singleTrack = singleTrack && e.track == clipClipboard.front().track;
    if (singleTrack)
        if (auto* st = getSelectedTrack())
            base = tracks.indexOf (st) - clipClipboard.front().track;

    for (auto& e : clipClipboard)
    {
        const int ti = e.track + base;

        if (! juce::isPositiveAndBelow (ti, tracks.size()))
            continue;

        auto state = e.state.createCopy();
        te::EditItemID::remapIDs (state, nullptr, edit);

        if (auto* inserted = tracks[ti]->insertClipWithState (state))
        {
            inserted->setStart (at + te::TimeDuration::fromSeconds (e.offset), false, true);
            multiSel.insert (inserted->itemID);
        }
    }

    if (multiSel.size() == 1)
    {
        selectedClipID = *multiSel.begin();
        multiSel.clear();
    }

    repaint();
}

void ArrangementView::duplicateSelectedClip()
{
    if (! multiSel.empty())                              // duplicate the group right after itself
    {
        auto clips = selectedClips();
        double first = 1.0e12, last = 0.0;
        for (auto* c : clips)
        {
            first = juce::jmin (first, c->getPosition().getStart().inSeconds());
            last = juce::jmax (last, c->getPosition().getEnd().inSeconds());
        }

        const auto shift = te::TimeDuration::fromSeconds (last - first);
        edit.getUndoManager().beginNewTransaction();
        std::set<te::EditItemID> added;

        for (auto* c : clips)
            if (auto* track = dynamic_cast<te::ClipTrack*> (c->getTrack()))
                if (auto copy = te::duplicateClip (*c))
                {
                    auto state = copy->state;
                    if (auto* ins = track->insertClipWithState (state))
                    {
                        ins->setStart (c->getPosition().getStart() + shift, false, true);
                        added.insert (ins->itemID);
                    }
                }

        multiSel = added;
        repaint();
        return;
    }

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
                                   std::abs (wave->getPitchChange() - (float) transposes[i]) < 0.01f);

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
        m.addItem (14, "Export MIDI File...");
    }

    m.addSeparator();
    m.addItem (15, "Bounce to New Track");
    m.addItem (16, "Consolidate in Place (render clip with its effects)");
    if (! isMidi)
        m.addItem (16, "Crossfade into Next Clip");

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
        else if (r == 16)
            consolidateClip (id);
        else if (r == 15)
        {
            if (auto* at = dynamic_cast<te::AudioTrack*> (c->getTrack()))
                bounceToNewTrack (*at, c->getEditTimeRange(), c->getName(), {}, c);

            if (auto* cc = te::findClipForID (edit, id))
                cc->setMuted (true);
        }
        else if (r == 16) crossfadeIntoNext (*c);
        else if (r == 14) { if (auto* mc = dynamic_cast<te::MidiClip*> (c)) exportMidiClip (*mc); }
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
                w->setPitchChange ((float) t[r - 500]);       // audio pitch shift (setTranspose only affects auto-pitch / key)

                if (t[r - 500] != 0 && w->getTimeStretchMode() == te::TimeStretcher::disabled)
                    w->setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
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
