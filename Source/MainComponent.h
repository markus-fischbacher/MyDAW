#pragma once

#include "Common.h"
#include "ArrangementView.h"
#include "MixerView.h"
#include "PianoRoll.h"
#include "TransportBar.h"
#include "BrowserPanel.h"

// Owns the engine and the current project (te::Edit), and the File menu commands.
class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
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
                   addAudioTrack, addInstrumentTrack, addReturnTrack, deleteTrack, toggleAutomation,
                   toggleMixer, toggleBrowser, toggleLoop, toggleClick, trackPlugins,
                   insertTempo, resetTempoMap, setTimeSig, insertTimeSig, togglePunch, masterPlugins, zoomFit, recoverAutosave, importMidi, expandAutomation, editShortcuts, recentBase = 1000 };

    // Project handling
    void setEdit (std::unique_ptr<te::Edit>, const juce::File& file);
    void buildViews();
    void setPianoRollVisible (bool);
    void setMixerVisible (bool);
    void setBrowserVisible (bool);
    void exportAudioFromChooser();
    void askForText (const juce::String& title, const juce::String& message, const juce::String& initial,
                     std::function<void (juce::String)> done);
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
    void addToRecent (const juce::File&);
    juce::StringArray getRecent() const;
    juce::File autosaveFile() const;
    void autosave();
    int autosaveTicks = 0;
    juce::PropertiesFile& settings();
    std::unique_ptr<juce::PropertiesFile> settingsFile;

    // Rebindable keyboard shortcuts (stored in the settings file; defaults live in MainComponent.cpp)
    struct Binding { int menuID; juce::String name; juce::KeyPress key, defaultKey; };
    std::vector<Binding> bindings;
    void loadBindings();
    void saveBindings();
    juce::String shortcutText (int menuID) const;
    void showShortcutEditor();
    void startRecording();
    class ShortcutEditor;
    std::unique_ptr<juce::DialogWindow> shortcutDialog;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

    void timerCallback() override;

    te::Engine engine { JUCE_APPLICATION_NAME_STRING, std::make_unique<SyncUIBehaviour>(), std::make_unique<ArrangerEngineBehaviour>() };
    std::unique_ptr<te::Edit> edit;
    juce::File projectFile;     // empty while the project is untitled

    std::unique_ptr<TransportBar> transportBar;
    std::unique_ptr<ArrangementView> arrangement;
    std::unique_ptr<PianoRoll> pianoRoll;
    std::unique_ptr<MixerView> mixer;
    std::unique_ptr<BrowserPanel> browser;
    bool browserVisible = false;
    bool mixerVisible = false;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String lastTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
