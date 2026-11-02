#include "PianoRoll.h"

static bool isBlackKey (int pitch)
{
    const int n = pitch % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

PianoRoll::PianoRoll (te::Edit& e)
    : edit (e), editState (e.state)
{
    editState.addListener (this);
    setOpaque (true);
    startTimerHz (60);
}

PianoRoll::~PianoRoll()
{
    editState.removeListener (this);
}

te::MidiClip* PianoRoll::getClip() const
{
    return clipID.isValid() ? dynamic_cast<te::MidiClip*> (te::findClipForID (edit, clipID)) : nullptr;
}

double PianoRoll::clipStartBeat (te::MidiClip& clip) const
{
    return edit.tempoSequence.toBeats (clip.getPosition().getStart()).inBeats();
}

void PianoRoll::setClip (te::EditItemID id)
{
    clipID = id;
    selectedNote = nullptr;
    dragMode = DragMode::none;
    scrollBeats = 0.0;
    scrollToNotes();
    repaint();
}

void PianoRoll::scrollToNotes()
{
    // Centre on the notes, or on middle C for an empty clip
    int centrePitch = 60;

    if (auto* clip = getClip())
    {
        auto& notes = clip->getSequence().getNotes();

        if (! notes.isEmpty())
        {
            int lo = 127, hi = 0;

            for (auto* n : notes)
            {
                lo = juce::jmin (lo, n->getNoteNumber());
                hi = juce::jmax (hi, n->getNoteNumber());
            }

            centrePitch = (lo + hi) / 2;
        }
    }

    const int viewHeight = juce::jmax (100, getHeight() - titleHeight - velHeight);
    scrollY = (numPitches - 1 - centrePitch) * rowHeight - viewHeight / 2;
    clampScroll();
}

void PianoRoll::clampScroll()
{
    const int maxY = juce::jmax (0, numPitches * rowHeight - (getHeight() - titleHeight - velHeight));
    scrollY = juce::jlimit (0, maxY, scrollY);
    scrollBeats = juce::jmax (0.0, scrollBeats);
}

double PianoRoll::getGridBeats() const
{
    for (auto g : { 0.0625, 0.125, 0.25, 0.5, 1.0, 4.0 })
        if (g * pixelsPerBeat >= 10.0)
            return g;

    return 4.0;
}

double PianoRoll::snapBeat (double beat, bool bypass) const
{
    if (bypass)
        return beat;

    const auto g = getGridBeats();
    return std::round (beat / g) * g;
}

juce::Rectangle<int> PianoRoll::getNoteBounds (const te::MidiNote& n) const
{
    const auto x1 = beatToX (n.getStartBeat().inBeats());
    const auto x2 = beatToX (n.getEndBeat().inBeats());
    return juce::Rectangle<double> (x1, pitchToY (n.getNoteNumber()) + 1.0, juce::jmax (3.0, x2 - x1), rowHeight - 2.0).toNearestInt();
}

bool PianoRoll::isLive (te::MidiClip& clip, const te::MidiNote* n) const
{
    return n != nullptr && clip.getSequence().getNotes().contains (const_cast<te::MidiNote*> (n));
}

te::MidiNote* PianoRoll::hitTestNote (te::MidiClip& clip, juce::Point<int> p, bool* onRightEdge) const
{
    auto& notes = clip.getSequence().getNotes();

    for (int i = notes.size(); --i >= 0;)
    {
        const auto b = getNoteBounds (*notes.getUnchecked (i));

        if (b.contains (p))
        {
            if (onRightEdge != nullptr)
                *onRightEdge = p.x >= b.getRight() - juce::jmin (edgeGrab, b.getWidth() / 3);

            return notes.getUnchecked (i);
        }
    }

    return nullptr;
}

//==============================================================================

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    auto* clip = getClip();

    if (clip == nullptr)
    {
        g.setColour (Palette::textDim);
        g.drawText ("Double-click a MIDI clip to edit its notes", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto grid = juce::Rectangle<int> (keyboardWidth, titleHeight, getWidth() - keyboardWidth, getHeight() - titleHeight - velHeight);

    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (grid);

        // Row backgrounds (black keys darker)
        for (int p = 0; p < numPitches; ++p)
        {
            const int y = pitchToY (p);

            if (y + rowHeight < grid.getY() || y > grid.getBottom())
                continue;

            g.setColour (isBlackKey (p) ? Palette::laneB : Palette::laneA);
            g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);
            g.setColour (p % 12 == 0 ? Palette::gridBar : Palette::background);
            g.drawHorizontalLine (y + rowHeight - 1, (float) grid.getX(), (float) grid.getRight());
        }

        // Beat grid
        const auto gridBeats = getGridBeats();
        const auto first = std::floor (xToBeat (grid.getX()) / gridBeats) * gridBeats;
        const auto last = xToBeat (grid.getRight());

        for (auto b = first; b <= last; b += gridBeats)
        {
            const bool isBar = std::fmod (b, beatsPerBar()) == 0.0, isBeat = std::fmod (b, 1.0) == 0.0;
            g.setColour (isBar ? Palette::gridBar.brighter (0.3f) : isBeat ? Palette::gridBar : Palette::gridBeat);
            g.drawVerticalLine (juce::roundToInt (beatToX (b)), (float) grid.getY(), (float) grid.getBottom());
        }

        // Clip extent: dim everything after the clip end
        const auto lengthBeats = edit.tempoSequence.toBeats (clip->getPosition().getEnd()).inBeats() - clipStartBeat (*clip);
        const auto endX = (int) beatToX (lengthBeats);

        if (endX < grid.getRight())
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (juce::jmax (endX, grid.getX()), grid.getY(), grid.getRight() - endX, grid.getHeight());
        }

        g.setColour (Palette::accent.withAlpha (0.7f));
        g.drawVerticalLine (endX, (float) grid.getY(), (float) grid.getBottom());

        // Notes
        for (auto* n : clip->getSequence().getNotes())
        {
            const auto b = getNoteBounds (*n);

            if (! b.intersects (grid))
                continue;

            const auto vel = juce::jlimit (0.0f, 1.0f, (float) n->getVelocity() / 127.0f);
            auto c = Palette::midiClip.withMultipliedBrightness (0.55f + 0.6f * vel);

            g.setColour (c);
            g.fillRoundedRectangle (b.toFloat(), 2.0f);
            g.setColour (n == selectedNote ? Palette::accent : c.darker (0.6f));
            g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 2.0f, n == selectedNote ? 2.0f : 1.0f);
        }

        // Playhead (clip-relative)
        const auto playBeat = edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats() - clipStartBeat (*clip);
        g.setColour (Palette::playhead);
        g.drawVerticalLine (juce::roundToInt (beatToX (playBeat)), (float) grid.getY(), (float) grid.getBottom());
    }

    // Keyboard
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, titleHeight, keyboardWidth, getHeight() - titleHeight - velHeight));
        g.setFont (juce::FontOptions (10.0f));

        for (int p = 0; p < numPitches; ++p)
        {
            const int y = pitchToY (p);

            if (y + rowHeight < titleHeight || y > gridBottom())
                continue;

            g.setColour (isBlackKey (p) ? juce::Colour (0xff17181a) : juce::Colour (0xffd9dbe0));
            g.fillRect (0, y, keyboardWidth - 1, rowHeight - 1);

            if (p % 12 == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.drawText ("C" + juce::String (p / 12 - 2), 4, y, keyboardWidth - 8, rowHeight, juce::Justification::centredRight, false);
            }
        }
    }

    // Velocity lane
    {
        const auto lane = juce::Rectangle<int> (0, gridBottom(), getWidth(), velHeight);
        g.setColour (Palette::panel);
        g.fillRect (lane);
        g.setColour (Palette::gridBar);
        g.drawHorizontalLine (lane.getY(), 0.0f, (float) getWidth());
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (laneName() + " (C)", 4, lane.getY(), keyboardWidth - 6, velHeight, juce::Justification::centredLeft, false);

        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (lane.withTrimmedLeft (keyboardWidth));

        if (ccType >= 0)
        {
            for (auto* ev : clip->getSequence().getControllerEvents())
            {
                if (ev->getType() != ccType)
                    continue;

                const auto x = (int) beatToX (ev->getBeatPosition().inBeats());
                const auto h = juce::roundToInt ((velHeight - 6) * (float) ev->getControllerValue() / (float) ccMax());
                g.setColour (ev == ccDrag ? Palette::accent : Palette::midiClip);
                g.fillRect (x, lane.getBottom() - 3 - h, 2, h);
                g.fillEllipse ((float) x - 2.0f, (float) (lane.getBottom() - 3 - h) - 2.0f, 6.0f, 6.0f);
            }
            return;
        }

        for (auto* n : clip->getSequence().getNotes())
        {
            const auto x = (int) beatToX (n->getStartBeat().inBeats());
            const auto h = juce::roundToInt ((velHeight - 6) * (float) n->getVelocity() / 127.0f);
            g.setColour (n == selectedNote ? Palette::accent : Palette::midiClip);
            g.fillRect (x, lane.getBottom() - 3 - h, 3, h);
            g.fillEllipse ((float) x - 2.0f, (float) (lane.getBottom() - 3 - h) - 2.0f, 7.0f, 7.0f);
        }
    }

    // Title bar
    g.setColour (Palette::panel);
    g.fillRect (0, 0, getWidth(), titleHeight);
    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("MIDI  -  " + clip->getName(), 10, 0, getWidth() / 2, titleHeight, juce::Justification::centredLeft, true);
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("click: add   right-click/delete: remove   Q: quantise   up/down: transpose   alt: no snap",
                getWidth() / 2, 0, getWidth() / 2 - 10, titleHeight, juce::Justification::centredRight, true);
}

void PianoRoll::timerCallback()
{
    auto* clip = getClip();

    if (clip == nullptr || ! isVisible())
        return;

    const auto beat = edit.tempoSequence.toBeats (edit.getTransport().getPosition()).inBeats() - clipStartBeat (*clip);
    const int x = juce::roundToInt (beatToX (beat));

    if (x != lastPlayheadX)
    {
        repaint (lastPlayheadX - 2, titleHeight, 5, gridBottom() - titleHeight);
        repaint (x - 2, titleHeight, 5, gridBottom() - titleHeight);
        lastPlayheadX = x;
    }
}

//==============================================================================

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    auto* clip = getClip();
    auto* n = clip != nullptr ? hitTestNote (*clip, e.getPosition(), &edge) : nullptr;

    setMouseCursor (n == nullptr ? juce::MouseCursor::NormalCursor
                                 : edge ? juce::MouseCursor::LeftRightResizeCursor
                                        : juce::MouseCursor::DraggingHandCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    dragMode = DragMode::none;
    auto* clip = getClip();
    velNote = nullptr;

    ccDrag = nullptr;

    if (clip != nullptr && ccType >= 0 && e.y >= gridBottom() && e.x >= keyboardWidth)
    {
        auto* um = &edit.getUndoManager();
        um->beginNewTransaction();
        auto* hit = hitTestCC (*clip, e.getPosition());

        if (e.mods.isPopupMenu() || e.mods.isAltDown())
        {
            if (hit != nullptr)
                clip->getSequence().removeControllerEvent (*hit, um);
        }
        else
        {
            if (hit == nullptr)
                hit = clip->getSequence().addControllerEvent (te::BeatPosition::fromBeats (juce::jmax (0.0, snapBeat (xToBeat (e.x), false))),
                                                              ccType, ccValueFromY (e.y), um);
            else
                hit->setControllerValue (ccValueFromY (e.y), um);

            ccDrag = hit;
        }

        repaint();
        return;
    }

    if (clip != nullptr && e.y >= gridBottom() && e.x >= keyboardWidth)
    {
        edit.getUndoManager().beginNewTransaction();
        velNote = hitTestVelocity (*clip, e.getPosition());

        if (velNote != nullptr)
        {
            selectedNote = velNote;
            setVelocityFromY (*velNote, e.y);
        }

        repaint();
        return;
    }

    if (clip == nullptr || e.x < keyboardWidth || e.y < titleHeight)
        return;

    auto* um = &edit.getUndoManager();
    bool onEdge = false;
    auto* hit = hitTestNote (*clip, e.getPosition(), &onEdge);

    um->beginNewTransaction();

    if (e.mods.isPopupMenu())
    {
        if (hit != nullptr)
        {
            clip->getSequence().removeNote (*hit, um);
            selectedNote = nullptr;
        }

        return;
    }

    if (hit == nullptr)
    {
        const auto start = juce::jmax (0.0, std::floor (xToBeat (e.x) / getGridBeats()) * getGridBeats());
        const auto len = juce::jmax (getGridBeats(), lastNoteLength);

        hit = clip->getSequence().addNote (yToPitch (e.y), te::BeatPosition::fromBeats (start),
                                           te::BeatDuration::fromBeats (len), 100, 0, um);
        onEdge = true;   // dragging right after adding a note stretches it
        dragMode = DragMode::resize;
    }
    else
    {
        dragMode = onEdge ? DragMode::resize : DragMode::move;
    }

    selectedNote = hit;

    if (hit != nullptr)
    {
        dragStartBeat = xToBeat (e.x);
        dragNoteStart = hit->getStartBeat().inBeats();
        dragNoteLength = hit->getLengthBeats().inBeats();
    }

    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    auto* clip = getClip();

    if (clip != nullptr && ccDrag != nullptr)
    {
        ccDrag->setControllerValue (ccValueFromY (e.y), &edit.getUndoManager());
        return;
    }

    if (clip != nullptr && velNote != nullptr)
    {
        if (isLive (*clip, velNote))
            setVelocityFromY (*velNote, e.y);
        return;
    }

    if (clip == nullptr || dragMode == DragMode::none || ! isLive (*clip, selectedNote))
        return;

    auto* um = &edit.getUndoManager();
    const bool bypass = e.mods.isAltDown();
    const auto delta = xToBeat (e.x) - dragStartBeat;
    const auto minLen = getGridBeats();

    if (dragMode == DragMode::move)
    {
        const auto start = juce::jmax (0.0, snapBeat (dragNoteStart + delta, bypass));
        selectedNote->setStartAndLength (te::BeatPosition::fromBeats (start), te::BeatDuration::fromBeats (dragNoteLength), um);

        const auto pitch = yToPitch (e.y);

        if (pitch != selectedNote->getNoteNumber())
            selectedNote->setNoteNumber (pitch, um);
    }
    else
    {
        const auto end = snapBeat (dragNoteStart + dragNoteLength + delta, bypass);
        const auto len = juce::jmax (minLen, end - dragNoteStart);
        selectedNote->setStartAndLength (te::BeatPosition::fromBeats (dragNoteStart), te::BeatDuration::fromBeats (len), um);
    }
}

// Same-pitch notes may not overlap. Like Ableton Live, the note just placed/moved/resized wins and the
// notes it lands on are shortened (or removed if completely covered).
void PianoRoll::resolveOverlaps (te::MidiClip& clip, te::MidiNote& placed)
{
    auto* um = &edit.getUndoManager();
    const double ns = placed.getStartBeat().inBeats(), ne = ns + placed.getLengthBeats().inBeats();
    std::vector<te::MidiNote*> hits;

    for (auto* o : clip.getSequence().getNotes())
        if (o != &placed && o->getNoteNumber() == placed.getNoteNumber()
             && o->getStartBeat().inBeats() < ne - 1.0e-6
             && o->getStartBeat().inBeats() + o->getLengthBeats().inBeats() > ns + 1.0e-6)
            hits.push_back (o);

    for (auto* o : hits)
    {
        const double os = o->getStartBeat().inBeats(), oe = os + o->getLengthBeats().inBeats();

        if (os < ns - 1.0e-6)
            o->setStartAndLength (o->getStartBeat(), te::BeatDuration::fromBeats (ns - os), um);                     // trim its end
        else if (oe <= ne + 1.0e-6)
            clip.getSequence().removeNote (*o, um);                                                                // fully covered
        else
            o->setStartAndLength (te::BeatPosition::fromBeats (ne), te::BeatDuration::fromBeats (oe - ne), um);   // trim its start
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    auto* clip = getClip();

    if (clip != nullptr && dragMode != DragMode::none && isLive (*clip, selectedNote))
    {
        lastNoteLength = selectedNote->getLengthBeats().inBeats();
        resolveOverlaps (*clip, *selectedNote);
    }

    dragMode = DragMode::none;
    velNote = nullptr;
    ccDrag = nullptr;
}

juce::String PianoRoll::laneName() const
{
    if (ccType < 0)                                         return "Velocity";
    if (ccType == te::MidiControllerEvent::pitchWheelType)  return "Pitch bend";
    if (ccType == 1)  return "Mod wheel";
    if (ccType == 7)  return "Volume";
    if (ccType == 10) return "Pan";
    if (ccType == 11) return "Expression";
    if (ccType == 64) return "Sustain";
    return "CC " + juce::String (ccType);
}

int PianoRoll::ccValueFromY (int y) const
{
    const auto norm = 1.0f - (float) (y - gridBottom() - 3) / (float) (velHeight - 6);
    return juce::roundToInt (juce::jlimit (0.0f, 1.0f, norm) * (float) ccMax());
}

te::MidiControllerEvent* PianoRoll::hitTestCC (te::MidiClip& clip, juce::Point<int> p) const
{
    for (auto* ev : clip.getSequence().getControllerEvents())
        if (ev->getType() == ccType && std::abs (beatToX (ev->getBeatPosition().inBeats()) - p.x) <= 5.0)
            return ev;

    return nullptr;
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        const auto anchor = xToBeat (e.position.x);
        pixelsPerBeat = juce::jlimit (8.0, 600.0, pixelsPerBeat * std::pow (2.0, (double) w.deltaY * 2.0));
        scrollBeats = anchor - (e.position.x - keyboardWidth) / pixelsPerBeat;
    }
    else
    {
        auto dx = (double) w.deltaX, dy = (double) w.deltaY;

        if (e.mods.isShiftDown() && dx == 0.0)
            std::swap (dx, dy);

        scrollBeats -= dx * 400.0 / pixelsPerBeat;
        scrollY -= juce::roundToInt (dy * 400.0);
    }

    clampScroll();
    repaint();
}

void PianoRoll::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    const auto anchor = xToBeat (e.position.x);
    pixelsPerBeat = juce::jlimit (8.0, 600.0, pixelsPerBeat * scaleFactor);
    scrollBeats = juce::jmax (0.0, anchor - (e.position.x - keyboardWidth) / pixelsPerBeat);
    repaint();
}

te::MidiNote* PianoRoll::hitTestVelocity (te::MidiClip& clip, juce::Point<int> p) const
{
    te::MidiNote* best = nullptr;
    double bestDist = 8.0;

    for (auto* n : clip.getSequence().getNotes())
    {
        const auto d = std::abs (beatToX (n->getStartBeat().inBeats()) + 1.0 - p.x);

        if (d <= bestDist)
        {
            bestDist = d;
            best = n;
        }
    }

    return best;
}

void PianoRoll::setVelocityFromY (te::MidiNote& n, int y)
{
    const auto norm = 1.0f - (float) (y - gridBottom() - 3) / (float) (velHeight - 6);
    n.setVelocity (juce::jlimit (1, 127, juce::roundToInt (norm * 127.0f)), &edit.getUndoManager());
    repaint();
}

double PianoRoll::beatsPerBar() const
{
    if (auto* clip = getClip())
        return juce::jmax (1, edit.tempoSequence.getTimeSigAt (clip->getPosition().getStart()).numerator.get());

    return 4.0;
}

void PianoRoll::quantiseNotes()
{
    auto* clip = getClip();

    if (clip == nullptr)
        return;

    auto* um = &edit.getUndoManager();
    um->beginNewTransaction();
    const auto g = getGridBeats();

    for (auto* n : clip->getSequence().getNotes())
    {
        const auto start = std::round (n->getStartBeat().inBeats() / g) * g;
        n->setStartAndLength (te::BeatPosition::fromBeats (juce::jmax (0.0, start)), n->getLengthBeats(), um);
    }

    repaint();
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('c', 0, 0))
    {
        static const int lanes[] = { -1, 1, 7, 10, 11, 64, te::MidiControllerEvent::pitchWheelType };
        size_t i = 0;
        while (i < std::size (lanes) && lanes[i] != ccType)
            ++i;
        ccType = lanes[(i + 1) % std::size (lanes)];
        repaint();
        return true;
    }

    auto* clip = getClip();

    if (clip != nullptr && key == juce::KeyPress ('q'))
    {
        quantiseNotes();
        return true;
    }

    if (clip != nullptr && isLive (*clip, selectedNote) && (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey))
    {
        edit.getUndoManager().beginNewTransaction();
        const int step = key == juce::KeyPress::upKey ? 1 : -1;
        selectedNote->setNoteNumber (juce::jlimit (0, 127, selectedNote->getNoteNumber() + step), &edit.getUndoManager());
        resolveOverlaps (*clip, *selectedNote);
        return true;
    }

    if (clip != nullptr && isLive (*clip, selectedNote)
         && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
    {
        auto* um = &edit.getUndoManager();
        um->beginNewTransaction();
        clip->getSequence().removeNote (*selectedNote, um);
        selectedNote = nullptr;
        return true;
    }

    return false;
}
