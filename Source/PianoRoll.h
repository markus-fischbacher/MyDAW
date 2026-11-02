#pragma once

#include "Common.h"

/*  Note editor for one MIDI clip: keyboard on the left, note grid on the right.

    Note positions in a te::MidiList are in beats relative to the clip start, so the
    view works in clip-relative beats and only converts to edit time for the playhead.
*/
class PianoRoll : public juce::Component,
                  private juce::ValueTree::Listener,
                  private juce::AsyncUpdater,
                  private juce::Timer
{
public:
    explicit PianoRoll (te::Edit&);
    ~PianoRoll() override;

    void setClip (te::EditItemID);
    te::EditItemID getClipID() const    { return clipID; }

    /** Call after the component has been given its bounds, to centre the view on the notes. */
    void scrollToNotesOnShow()          { scrollToNotes(); repaint(); }

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    enum class DragMode { none, move, resize };

    static constexpr int keyboardWidth = 64, titleHeight = 24, velHeight = 56, rowHeight = 16, numPitches = 128, edgeGrab = 6;

    te::MidiClip* getClip() const;
    double clipStartBeat (te::MidiClip&) const;

    double beatToX (double clipBeat) const      { return keyboardWidth + (clipBeat - scrollBeats) * pixelsPerBeat; }
    double xToBeat (double x) const             { return (x - keyboardWidth) / pixelsPerBeat + scrollBeats; }
    int pitchToY (int pitch) const              { return titleHeight + (numPitches - 1 - pitch) * rowHeight - scrollY; }
    int yToPitch (int y) const                  { return juce::jlimit (0, numPitches - 1, numPitches - 1 - (y - titleHeight + scrollY) / rowHeight); }
    juce::Rectangle<int> getNoteBounds (const te::MidiNote&) const;

    double getGridBeats() const;
    double snapBeat (double beat, bool bypass) const;
    te::MidiNote* hitTestNote (te::MidiClip&, juce::Point<int>, bool* onRightEdge) const;
    bool isLive (te::MidiClip&, const te::MidiNote*) const;
    int gridBottom() const                      { return getHeight() - velHeight; }
    te::MidiNote* hitTestVelocity (te::MidiClip&, juce::Point<int>) const;
    void setVelocityFromY (te::MidiNote&, int y);
    void quantiseNotes();
    void resolveOverlaps (te::MidiClip&, te::MidiNote&);
    int ccMax() const                           { return ccType == te::MidiControllerEvent::pitchWheelType ? 16383 : 127; }
    juce::String laneName() const;
    te::MidiControllerEvent* hitTestCC (te::MidiClip&, juce::Point<int>) const;
    int ccValueFromY (int y) const;
    int ccType = -1;                            // -1 = note velocity, otherwise a controller number
    te::MidiControllerEvent* ccDrag = nullptr;
    double beatsPerBar() const;
    void clampScroll();
    void scrollToNotes();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override  { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override               { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override        { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override                { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override   { repaint(); }
    void timerCallback() override;

    te::Edit& edit;
    juce::ValueTree editState;
    te::EditItemID clipID;

    double pixelsPerBeat = 64.0, scrollBeats = 0.0, lastNoteLength = 1.0;
    int scrollY = 0, lastPlayheadX = -1;

    te::MidiNote* selectedNote = nullptr;
    te::MidiNote* velNote = nullptr;
    DragMode dragMode = DragMode::none;
    double dragStartBeat = 0.0, dragNoteStart = 0.0, dragNoteLength = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
