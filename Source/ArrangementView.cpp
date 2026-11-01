#include "ArrangementView.h"

ArrangementView::ArrangementView (te::Edit& e)
    : edit (e), editState (e.state)
{
    editState.addListener (this);
    setWantsKeyboardFocus (false);
    setOpaque (true);
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
        const bool isBar = std::fmod (beat, 4.0) == 0.0;
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

        for (auto* clip : tracks[i]->getClips())
        {
            const auto b = getClipBounds (*clip, i);

            if (b.intersects (lanes))
                paintClip (g, *clip, b);
        }
    }
}

void ArrangementView::paintClip (juce::Graphics& g, te::Clip& clip, juce::Rectangle<int> b)
{
    const bool selected = clip.itemID == selectedClipID;
    auto* midi = dynamic_cast<te::MidiClip*> (&clip);
    const auto colour = midi != nullptr ? Palette::midiClip : Palette::clip;

    g.setColour (colour.withAlpha (0.35f));
    g.fillRoundedRectangle (b.toFloat(), 3.0f);

    auto header = b.withHeight (clipHeaderHeight);
    g.setColour (colour);
    g.fillRoundedRectangle (header.toFloat(), 3.0f);

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (clip.getName(), header.reduced (5, 0), juce::Justification::centredLeft, true);

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
            thumb->drawChannels (g, body, sourceRange, 1.0f);
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

    // Bar numbers, thinned out as you zoom out
    int barStep = 1;
    while (barStep * 4.0 * pixelsPerBeat < 40.0)
        barStep *= 2;

    const int firstBar = juce::jmax (0, (int) std::floor (xToBeat (ruler.getX()) / 4.0));
    const int lastBar = (int) std::ceil (xToBeat (ruler.getRight()) / 4.0);

    g.setFont (juce::FontOptions (11.0f));

    for (int bar = firstBar - (firstBar % barStep); bar <= lastBar; bar += barStep)
    {
        const auto x = juce::roundToInt (beatToX (bar * 4.0));

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
        auto r = juce::Rectangle<int> (0, trackY (i), headerWidth, trackHeight);

        g.setColour (Palette::clip);
        g.fillRect (r.removeFromLeft (4).reduced (0, 1));

        g.setColour (Palette::text);
        g.drawText (tracks[i]->getName(), r.reduced (8, 6).removeFromTop (18), juce::Justification::centredLeft, true);

        g.setColour (Palette::background);
        g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) headerWidth);
    }

    g.setColour (Palette::background);
    g.drawVerticalLine (headerWidth - 1, 0.0f, (float) getHeight());
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
        case DragMode::trimEnd:   setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case DragMode::move:      setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case DragMode::none:
        case DragMode::scrub:     setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    dragMode = DragMode::none;

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

        case DragMode::none:
        case DragMode::scrub:
            break;
    }
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    dragMode = DragMode::none;
    dragClipID = {};
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < rulerHeight || e.x < headerWidth)
        return;

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

    const auto startBeat = juce::jmax (0.0, std::floor (xToBeat (e.x) / 4.0) * 4.0);
    const te::TimeRange range (beatToTime (startBeat), beatToTime (startBeat + 4.0));

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
