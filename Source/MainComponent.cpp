#include "MainComponent.h"

static constexpr int numInitialTracks = 8;

MainComponent::MainComponent()
{
    newEmptyProject();

    setWantsKeyboardFocus (true);
    setSize (1280, 760);

    const auto args = juce::JUCEApplication::getCommandLineParameterArray();

    // Command line: a project file opens it, audio files go onto consecutive tracks at bar 1
    int trackIndex = 0;

    for (auto& arg : args)
    {
        if (! juce::File::isAbsolutePath (arg) || ! juce::File (arg).existsAsFile())
            continue;

        if (juce::File (arg).hasFileExtension (projectSuffix))
            openProjectFile (juce::File (arg));
        else
            arrangement->insertAudioFile (juce::File (arg), trackIndex++, 0_tp);
    }

    edit->getUndoManager().clearUndoHistory();

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif

    startTimerHz (4);

    // --save-and-quit <file>: save the project to a file and exit (scripted checks)
    const int saveIndex = args.indexOf ("--save-and-quit");

    if (saveIndex >= 0 && saveIndex + 1 < args.size())
    {
        const juce::File target (args[saveIndex + 1]);

        juce::Timer::callAfterDelay (1500, [this, target]
        {
            saveProjectTo (target, [] (bool ok) { juce::JUCEApplication::getInstance()->setApplicationReturnValue (ok ? 0 : 1);
                                                  juce::JUCEApplication::quit(); });
        });
    }

    // --snapshot <file.png>: render the window to an image once thumbnails are ready, then quit.
    // Handy for visual checks and CI without touching the screen.
    const int snapIndex = args.indexOf ("--snapshot");

    if (snapIndex >= 0 && snapIndex + 1 < args.size())
    {
        const juce::File target (args[snapIndex + 1]);

        juce::Timer::callAfterDelay (3000, [this, target]
        {
            target.deleteFile();
            juce::FileOutputStream out (target);
            juce::PNGImageFormat().writeImageToStream (createComponentSnapshot (getLocalBounds(), true, 2.0f), out);
            juce::JUCEApplication::quit();
        });
    }
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif

    edit->getTransport().stop (false, false);
    arrangement = nullptr;
    transportBar = nullptr;
    edit = nullptr;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transportBar->setBounds (r.removeFromTop (44));
    arrangement->setBounds (r);
}

//==============================================================================
// Project lifecycle

void MainComponent::setEdit (std::unique_ptr<te::Edit> newEdit, const juce::File& file)
{
    if (edit != nullptr)
        edit->getTransport().stop (false, false);

    // Views reference the edit, so they have to go before it does
    arrangement = nullptr;
    transportBar = nullptr;

    edit = std::move (newEdit);
    projectFile = file;

    edit->getTransport().ensureContextAllocated();
    buildViews();
    updateWindowTitle();
}

void MainComponent::buildViews()
{
    transportBar = std::make_unique<TransportBar> (*edit);
    transportBar->onAddFile = [this] { addAudioFileFromChooser(); };
    transportBar->onShowSettings = [this]
    {
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Audio Settings";
        o.dialogBackgroundColour = Palette::panel;
        o.content.setOwned (new juce::AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager,
                                                                     0, 512, 1, 512, true, true, true, false));
        o.content->setSize (500, 420);
        o.launchAsync();
    };

    arrangement = std::make_unique<ArrangementView> (*edit);

    addAndMakeVisible (*transportBar);
    addAndMakeVisible (*arrangement);
    resized();
}

void MainComponent::newEmptyProject()
{
    // Untitled projects live in a unique temp file until the first "Save As"
    auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("Arranger")
                        .getChildFile (juce::Uuid().toDashedString() + projectSuffix);
    tempFile.getParentDirectory().createDirectory();

    auto newEdit = te::createEmptyEdit (engine, tempFile);
    newEdit->ensureNumberOfAudioTracks (numInitialTracks);
    newEdit->getUndoManager().clearUndoHistory();
    newEdit->resetChangedStatus();

    setEdit (std::move (newEdit), {});
}

void MainComponent::openProjectFile (const juce::File& file)
{
    auto loaded = te::loadEditFromFile (engine, file);

    if (loaded == nullptr)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Couldn't open project")
                                          .withMessage (file.getFullPathName())
                                          .withButton ("OK", 0)
                                          .withAssociatedComponent (this),
                                      nullptr);
        return;
    }

    loaded->getUndoManager().clearUndoHistory();
    loaded->resetChangedStatus();
    setEdit (std::move (loaded), file);
}

void MainComponent::chooseAndOpenProject()
{
    confirmDiscardChanges ([this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Open project",
                                                           projectFile.exists() ? projectFile.getParentDirectory()
                                                                                : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                           juce::String ("*") + projectSuffix);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult().existsAsFile())
                                          openProjectFile (fc.getResult());
                                  });
    });
}

void MainComponent::saveCurrentProject (std::function<void (bool)> done)
{
    if (projectFile == juce::File())
    {
        saveProjectAsFromChooser (std::move (done));
        return;
    }

    te::EditFileOperations (*edit).save (true, true, false, [this, done] (bool ok)
    {
        if (ok)
            edit->resetChangedStatus();

        updateWindowTitle();

        if (done)
            done (ok);
    });
}

void MainComponent::saveProjectAsFromChooser (std::function<void (bool)> done)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Save project as",
                                                       projectFile.exists() ? projectFile
                                                                            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                                  .getChildFile ("Untitled" + juce::String (projectSuffix)),
                                                       juce::String ("*") + projectSuffix);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, done] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();

                                  if (f == juce::File())
                                  {
                                      if (done)
                                          done (false);

                                      return;
                                  }

                                  saveProjectTo (f.withFileExtension (projectSuffix), done);
                              });
}

void MainComponent::saveProjectTo (const juce::File& file, std::function<void (bool)> done)
{
    // Write a copy, then reopen from it so the edit is properly bound to its new file
    const auto position = edit->getTransport().getPosition();

    if (! te::EditFileOperations (*edit).writeToFile (file, false))
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Couldn't save project")
                                          .withMessage (file.getFullPathName())
                                          .withButton ("OK", 0)
                                          .withAssociatedComponent (this),
                                      nullptr);

        if (done)
            done (false);

        return;
    }

    auto reopened = te::loadEditFromFile (engine, file);

    const bool reopenedOK = reopened != nullptr;

    if (reopenedOK)
    {
        reopened->getTransport().setPosition (position);
        reopened->resetChangedStatus();
        setEdit (std::move (reopened), file);
    }

    if (done)
        done (reopenedOK);
}

void MainComponent::confirmDiscardChanges (std::function<void()> proceed)
{
    if (! edit->hasChangedSinceSaved())
    {
        proceed();
        return;
    }

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Save changes to \"" + getProjectName() + "\"?")
                                      .withMessage ("Your changes will be lost if you don't save them.")
                                      .withButton ("Save", 1)
                                      .withButton ("Don't Save", 2)
                                      .withButton ("Cancel", 0)
                                      .withAssociatedComponent (this),
                                  [this, proceed] (int result)
                                  {
                                      if (result == 1)
                                          saveCurrentProject ([proceed] (bool ok) { if (ok) proceed(); });
                                      else if (result == 2)
                                          proceed();
                                  });
}

void MainComponent::requestQuit()
{
    confirmDiscardChanges ([] { juce::JUCEApplication::quit(); });
}

juce::String MainComponent::getProjectName() const
{
    return projectFile == juce::File() ? juce::String ("Untitled") : projectFile.getFileNameWithoutExtension();
}

void MainComponent::updateWindowTitle()
{
    auto title = getProjectName();

    if (edit != nullptr && edit->hasChangedSinceSaved())
        title << " \xe2\x80\xa2";   // bullet marks unsaved changes

    title << " - " << JUCE_APPLICATION_NAME_STRING;

    if (title == lastTitle)
        return;

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        window->setName (title);
        lastTitle = title;
    }
}

void MainComponent::timerCallback()
{
    updateWindowTitle();
}

//==============================================================================
// Menu

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;

    auto add = [&m] (int id, const juce::String& name, const juce::String& shortcut)
    {
        juce::PopupMenu::Item item (name);
        item.setID (id);
        item.shortcutKeyDescription = shortcut;
        m.addItem (item);
    };

    if (index == 0)
    {
        add (newProject,    "New Project",       "cmd + N");
        add (openProject,   "Open Project...",   "cmd + O");
        m.addSeparator();
        add (saveProject,   "Save",              "cmd + S");
        add (saveProjectAs, "Save As...",        "shift + cmd + S");
        m.addSeparator();
        add (importAudio,   "Import Audio...",   "cmd + I");
    }
    else
    {
        add (undo, "Undo", "cmd + Z");
        add (redo, "Redo", "shift + cmd + Z");
    }

    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case newProject:     confirmDiscardChanges ([this] { newEmptyProject(); }); break;
        case openProject:    chooseAndOpenProject(); break;
        case saveProject:    saveCurrentProject(); break;
        case saveProjectAs:  saveProjectAsFromChooser(); break;
        case importAudio:    addAudioFileFromChooser(); break;
        case undo:           edit->undo(); break;
        case redo:           edit->redo(); break;
        default:             break;
    }
}

//==============================================================================
// Keyboard

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    auto& transport = edit->getTransport();
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    if (key == juce::KeyPress::spaceKey)
    {
        if (transport.isPlaying())
            transport.stop (false, false);
        else
            transport.play (false);

        return true;
    }

    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::homeKey)
    {
        transport.setPosition (0_tp);
        return true;
    }

    struct Shortcut { juce::KeyPress key; int menuID; };

    const Shortcut shortcuts[] =
    {
        { { 'n', cmd, 0 },         newProject },
        { { 'o', cmd, 0 },         openProject },
        { { 's', cmd, 0 },         saveProject },
        { { 's', cmd | shift, 0 }, saveProjectAs },
        { { 'i', cmd, 0 },         importAudio },
        { { 'z', cmd, 0 },         undo },
        { { 'z', cmd | shift, 0 }, redo },
    };

    for (auto& s : shortcuts)
    {
        if (key == s.key)
        {
            menuItemSelected (s.menuID, 0);
            return true;
        }
    }

    return arrangement->keyPressed (key);
}

void MainComponent::addAudioFileFromChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Add audio file",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();

                                  if (f.existsAsFile())
                                      arrangement->insertAudioFile (f, 0, edit->getTransport().getPosition());
                              });
}
