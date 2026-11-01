#pragma once

#include "Common.h"
#include "ArrangementView.h"
#include "MixerView.h"
#include "PianoRoll.h"
#include "TransportBar.h"

// Owns the engine and the current project (te::Edit), and the File menu commands.
class MainComponent : public juce::Component,
                      private juce::MenuBarModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Asks about unsaved changes, then quits the app. */
    void requestQuit();

    static constexpr const char* projectSuffix = ".tracktionedit";

private:
    enum MenuIDs { newProject = 1, openProject, saveProject, saveProjectAs, importAudio, exportAudio, undo, redo,
                   addAudioTrack, addInstrumentTrack, deleteTrack, toggleAutomation,
                   toggleMixer, toggleLoop, toggleClick };

    // Project handling
    void setEdit (std::unique_ptr<te::Edit>, const juce::File& file);
    void buildViews();
    void setPianoRollVisible (bool);
    void setMixerVisible (bool);
    void exportAudioFromChooser();
    void newEmptyProject();
    void openProjectFile (const juce::File&);
    void chooseAndOpenProject();
    void saveCurrentProject (std::function<void (bool)> done = nullptr);
    void saveProjectAsFromChooser (std::function<void (bool)> done = nullptr);
    void saveProjectTo (const juce::File&, std::function<void (bool)> done);
    void confirmDiscardChanges (std::function<void()> proceed);
    void addAudioFileFromChooser();
    juce::String getProjectName() const;
    void updateWindowTitle();

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

    void timerCallback() override;

    te::Engine engine { JUCE_APPLICATION_NAME_STRING };
    std::unique_ptr<te::Edit> edit;
    juce::File projectFile;     // empty while the project is untitled

    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<ArrangementView> arrangement;
    std::unique_ptr<PianoRoll> pianoRoll;
    std::unique_ptr<MixerView> mixer;
    bool mixerVisible = false;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String lastTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
