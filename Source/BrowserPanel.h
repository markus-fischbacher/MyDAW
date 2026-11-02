#pragma once

#include "Common.h"

/*  File browser: audio files from a chosen folder, with click-to-preview,
    double-click to insert at the playhead and drag onto a track lane.
*/
class BrowserPanel : public juce::Component,
                     private juce::FileBrowserListener
{
public:
    explicit BrowserPanel (te::Engine&);
    ~BrowserPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::File getSelectedFile() const      { return tree.getSelectedFile (0); }

    /** Called on double-click of an audio file. */
    std::function<void (const juce::File&)> onInsertFile;

    static constexpr const char* dragDescription = "BrowserFile";

private:
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File&) override;
    void browserRootChanged (const juce::File&) override {}

    void chooseFolder();
    void preview (const juce::File&);
    void stopPreview();

    te::Engine& engine;
    juce::TimeSliceThread thread { "Browser scan" };
    juce::WildcardFileFilter filter;
    juce::DirectoryContentsList contents;
    juce::FileTreeComponent tree;
    juce::TextButton folderButton { "Folder..." };
    juce::ToggleButton previewToggle { "Preview" };
    juce::Label pathLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::AudioSourcePlayer player;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
};
