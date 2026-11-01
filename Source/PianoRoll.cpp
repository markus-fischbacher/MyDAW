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

    const int viewHeight = juce::jmax (100, getHeight() - titleHeight);
    scrollY = (numPitches - 1 - centrePitch) * rowHeight - viewHeight / 2;
    clampScroll();
}

void PianoRoll::clampScroll()
{
    const int maxY = juce::jmax (0, numPitches * rowHeight - (getHeight() - titleHeight));
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

    const auto grid = juce::Rectangle<int> (keyboardWidth, titleHeight, getWidth() - keyboardWidth, getHeight() - titleHeight);

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
            const bool isBar = std::fmod (b, 4.0) == 0.0, isBeat = std::fmod (b, 1.0) == 0.0;
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
        g.reduceClipRegion (juce::Rectangle<int> (0, titleHeight, keyboardWidth, getHeight() - titleHeight));
        g.setFont (juce::FontOptions (10.0f));

        for (int p = 0; p < numPitches; ++p)
        {
            const int y = pitchToY (p);

            if (y + rowHeight < titleHeight || y > getHeight())
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

    // Title bar
    g.setColour (Palette::panel);
    g.fillRect (0, 0, getWidth(), titleHeight);
    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("MIDI  -  " + clip->getName(), 10, 0, getWidth() / 2, titleHeight, juce::Justification::centredLeft, true);
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("click: add note   right-click / delete: remove   alt: no snap   esc: close",
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
        repaint (lastPlayheadX - 2, titleHeight, 5, getHeight());
        repaint (x - 2, titleHeight, 5, getHeight());
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

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    auto* clip = getClip();

    if (clip != nullptr && dragMode != DragMode::none && isLive (*clip, selectedNote))
        lastNoteLength = selectedNote->getLengthBeats().inBeats();

    dragMode = DragMode::none;
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

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    auto* clip = getClip();

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
