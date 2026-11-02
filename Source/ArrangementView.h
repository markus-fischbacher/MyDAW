#pragma once

#include "Common.h"

/*  The arrangement timeline: ruler, track headers, lanes, clips, playhead.

    Everything is painted directly by this one component (no child component per
    clip) so it stays fast with thousands of clips. The horizontal axis is musical
    time (beats), converted to/from seconds through the Edit's tempo sequence.
*/
class ArrangementView : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        public juce::DragAndDropTarget,
                        private juce::ValueTree::Listener,
                        private juce::AsyncUpdater,
                        private juce::Timer
{
public:
    explicit ArrangementView (te::Edit&);
    ~ArrangementView() override;

    void insertAudioFile (const juce::File&, int trackIndex, te::TimePosition start);

    /** Appends a track; instrument tracks get a synth so MIDI clips are audible. */
    void addTrack (bool instrument);
    void addReturnTrack();
    void bounceToNewTrack (te::AudioTrack&, te::TimeRange, const juce::String& name, std::function<void()> muteOriginal);
    void importMidiFile (const juce::File&, int trackIndex, te::TimePosition start);
    void exportMidiClip (te::MidiClip&);
    void zoomToFit();
    void deleteSelectedTrack();
    bool hasSelectedTrack() const           { return getSelectedTrack() != nullptr; }
    void toggleAutomationOnSelectedTrack();
    void toggleExpandAllAutomation();
    void selectTrackIndex (int index);
    te::AudioTrack* selectedTrack() const   { return getSelectedTrack(); }
    double beatsPerBar() const;
    void toggleArm (te::AudioTrack&);
    bool isArmed (te::AudioTrack&) const;

    /** Called when a MIDI clip should be opened for editing (double-click, or just created). */
    std::function<void (te::EditItemID)> onOpenMidiClip;

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    std::function<juce::File()> getDraggedBrowserFile;

private:
    enum class DragMode { none, scrub, move, trimStart, trimEnd, automation, loopRange, fadeIn, fadeOut, loopStart, loopEnd, loopMove };

    struct ClipHit
    {
        te::Clip* clip = nullptr;
        DragMode mode = DragMode::none;
    };

    static constexpr int headerWidth = 170, rulerHeight = 28, trackHeight = 84, clipHeaderHeight = 16, edgeGrabWidth = 6, autoLaneHeight = 60, loopBarTop = 14;

    // Coordinate mapping
    double beatToX (double beat) const;
    double xToBeat (double x) const;
    double timeToX (te::TimePosition) const;
    te::TimePosition beatToTime (double beat) const;
    double timeToBeat (te::TimePosition) const;
    int trackY (int trackIndex) const;
    int rowHeight (int trackIndex) const;
    int totalTracksHeight() const;
    int yToTrackIndex (int y) const;
    juce::Rectangle<int> getClipBounds (te::Clip&, int trackIndex) const;
    juce::Rectangle<int> getLanesArea() const;

    double barToBeat (int bar) const;
    int beatToBar (double beat) const;
    void showRulerMenu (const juce::MouseEvent&);
    void showClipMenu (te::EditItemID);
    void duplicateSelectedClip();
    void crossfadeIntoNext (te::Clip&);
    void setClipWarp (te::WaveAudioClip&, bool on);
public:
    void setClipWarpForDemo (te::WaveAudioClip& w) { setClipWarp (w, true); }
private:
    int fadeHandleX (te::WaveAudioClip&, bool fadeIn, juce::Rectangle<int> bounds) const;

    double getGridBeats() const;
    double snapBeat (double beat, bool bypass) const;
    void zoomAround (double factor, float mouseX);
    void scrollBy (double deltaBeats, int deltaY);

    ClipHit hitTestClip (juce::Point<int>) const;
    te::Clip* getSelectedClip() const;
    te::AudioTrack* getSelectedTrack() const;

    // Track headers
    enum class HeaderButton { none, mute, solo, arm, automation, param };
    juce::Rectangle<int> headerButtonRect (int trackIndex, HeaderButton) const;
    HeaderButton hitTestHeaderButton (juce::Point<int>, int trackIndex) const;
    void headerClicked (const juce::MouseEvent&);
    void showTrackMenu (int trackIndex);
    void beginRename (int trackIndex);

    // Automation (volume / pan breakpoints, edited directly in the lane)
    te::AutomatableParameter* getAutoParam (te::AudioTrack&) const;
    struct ExpandedLane { te::AutomatableParameter* param; juce::String label; };
    std::vector<ExpandedLane> expandedLanes (te::AudioTrack&) const;
    bool isExpanded (const te::AudioTrack& t) const    { return expandedTracks.count (t.itemID) > 0; }
    float autoValueToY (te::AutomatableParameter&, float value, juce::Rectangle<int> lane) const;
    float autoYToValue (te::AutomatableParameter&, int y, juce::Rectangle<int> lane) const;
    int hitTestAutoPoint (te::AutomatableParameter&, juce::Rectangle<int> lane, juce::Point<int>) const;
    void paintAutomationLane (juce::Graphics&, te::AutomatableParameter&, juce::Rectangle<int> lane, const juce::String& label);
    void paintAutomation (juce::Graphics&, te::AudioTrack&, int trackIndex);
    void automationMouseDown (const juce::MouseEvent&, te::AudioTrack&, te::AutomatableParameter&, juce::Rectangle<int> lane);

    void paintGrid (juce::Graphics&, juce::Rectangle<int> lanes);
    void paintClips (juce::Graphics&, juce::Rectangle<int> lanes);
    void paintClip (juce::Graphics&, te::Clip&, juce::Rectangle<int>);
    void paintRuler (juce::Graphics&);
    void paintHeaders (juce::Graphics&);
    void paintPlayhead (juce::Graphics&);

    te::SmartThumbnail* getThumbnailFor (te::WaveAudioClip&);

    // ValueTree::Listener — any model change triggers a coalesced repaint
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override  { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override               { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override        { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override                { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void timerCallback() override;

    te::Edit& edit;
    juce::ValueTree editState;

    double pixelsPerBeat = 24.0;
    double scrollBeats = 0.0;
    int scrollY = 0;
    int lastPlayheadX = -1;

    te::EditItemID selectedClipID, selectedTrackID;
    std::set<te::EditItemID> automationTracks;
    std::map<te::EditItemID, std::pair<te::EditItemID, int>> pluginAutoParam;   // track -> (plugin, parameter index)
    void showAutomationParamMenu (te::AudioTrack&);
    std::map<te::EditItemID, int> automationParam;      // 0 = volume (default), 1 = pan
    int dragAutoIndex = -1;
    te::AutomatableParameter* dragParam = nullptr;
    juce::Rectangle<int> dragLane;
    std::set<te::EditItemID> expandedTracks;
    std::map<te::EditItemID, std::set<std::pair<te::EditItemID, int>>> pinnedParams;
    double loopGrabBeat = 0.0, loopOrigStart = 0.0, loopOrigEnd = 0.0;
    bool loopMoved = false;
    double loopAnchorBeat = 0.0;
    te::EditItemID dragAutoTrack;
    juce::TextEditor renameEditor;
    std::unique_ptr<juce::FileChooser> chooser;
    te::EditItemID renameTrackID;
    std::map<te::EditItemID, std::unique_ptr<te::SmartThumbnail>> thumbnails;

    // Drag state
    DragMode dragMode = DragMode::none;
    te::EditItemID dragClipID;
    double dragMouseStartBeat = 0.0, dragClipStartBeat = 0.0, dragClipEndBeat = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
