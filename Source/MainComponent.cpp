#include "MainComponent.h"
#include "PluginUI.h"

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

    PluginUI::loadKnownPlugins (engine);
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

    // --demo-midi: add a MIDI clip with an arpeggio on track 4 and open it in the piano roll
    if (args.contains ("--demo-midi"))
    {
        auto* track = te::getAudioTracks (*edit)[3];
        auto clip = track->insertMIDIClip ("Arp", { 0_tp, 8_tp }, nullptr);

        for (int i = 0; i < 16; ++i)
        {
            const int pitches[] = { 57, 60, 64, 67 };
            clip->getSequence().addNote (pitches[i % 4] + (i / 8) * 5, te::BeatPosition::fromBeats (i * 0.5),
                                         te::BeatDuration::fromBeats (0.5), 90, 0, nullptr);
        }

        arrangement->onOpenMidiClip (clip->itemID);
    }

    // --demo-mixer: open the mixer, tweak some strips and draw volume automation on track 1
    if (args.contains ("--demo-mixer"))
    {
        auto tracks = te::getAudioTracks (*edit);
        tracks[0]->getVolumePlugin()->setSliderPos (te::decibelsToVolumeFaderPosition (-6.0f));
        tracks[1]->getVolumePlugin()->setPan (-0.6f);
        tracks[2]->setMute (true);
        tracks[3]->setSolo (true);

        auto& curve = tracks[0]->getVolumePlugin()->volParam->getCurve();
        curve.addPoint (te::TimePosition::fromSeconds (0.0), 0.3f, 0.0f, nullptr);
        curve.addPoint (te::TimePosition::fromSeconds (4.0), 0.8f, 0.0f, nullptr);
        curve.addPoint (te::TimePosition::fromSeconds (8.0), 0.5f, 0.0f, nullptr);

        arrangement->selectTrackIndex (0);
        arrangement->toggleAutomationOnSelectedTrack();
        setMixerVisible (true);
    }

    // --demo-record: arm an audio and an instrument track
    if (args.contains ("--demo-record"))
    {
        arrangement->selectTrackIndex (1);
        arrangement->addTrack (true);
        juce::Timer::callAfterDelay (1000, [this]
        {
            arrangement->toggleArm (*te::getAudioTracks (*edit)[0]);
            arrangement->toggleArm (*te::getAudioTracks (*edit).getLast());
        });
    }

    // --demo-extras: reverb on track 1, 3/4 time, a tempo change at bar 3, punch range
    if (args.contains ("--demo-extras"))
    {
        auto* t = te::getAudioTracks (*edit)[0];
        if (auto p = edit->getPluginCache().createNewPlugin (te::ReverbPlugin::xmlTypeName, {}))
            t->pluginList.insertPlugin (p, 0, nullptr);

        edit->tempoSequence.getTimeSig (0)->setStringTimeSig ("3/4");
        edit->tempoSequence.insertTempo (te::BeatPosition::fromBeats (6.0), 90.0, 1.0f);
        if (auto ts = edit->tempoSequence.insertTimeSig (te::BeatPosition::fromBeats (12.0)))
            ts->setStringTimeSig ("7/8");
        setMixerVisible (true);
        edit->recordingPunchInOut = true;
        edit->getTransport().setLoopRange ({ te::TimePosition::fromSeconds (2.0), te::TimePosition::fromSeconds (6.0) });
        edit->getTransport().looping = true;
    }

    // --demo-clip: a generated 2s drum-ish loop with fades, gain, colour and warp at 100 BPM
    if (args.contains ("--demo-clip"))
    {
        const auto wav = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Arranger").getChildFile ("demo_loop.wav");
        wav.getParentDirectory().createDirectory();
        wav.deleteFile();
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), 44100.0, 1, 16, {}, 0));
            juce::AudioBuffer<float> b (1, 88200);
            for (int i = 0; i < 88200; ++i)
            {
                const double beatPhase = std::fmod (i / 22050.0, 1.0);
                b.setSample (0, i, (float) (std::sin (i * 0.06) * std::exp (-beatPhase * 8.0)));
            }
            w->writeFromAudioSampleBuffer (b, 0, 88200);
        }
        edit->tempoSequence.getTempo (0)->setBpm (100.0);
        arrangement->insertAudioFile (wav, 0, te::TimePosition());
        arrangement->insertAudioFile (wav, 1, te::TimePosition());

        if (auto* c0 = dynamic_cast<te::WaveAudioClip*> (te::getAudioTracks (*edit)[0]->getClips()[0]))
        {
            c0->setFadeIn (te::TimeDuration::fromSeconds (0.4));
            c0->setFadeOut (te::TimeDuration::fromSeconds (0.6));
            c0->setGainDB (-3.0f);
            c0->state.setProperty ("arrColour", juce::Colour (0xffd8704f).toString(), nullptr);
            c0->setName ("Fades + gain");
        }
        if (auto* c1 = dynamic_cast<te::WaveAudioClip*> (te::getAudioTracks (*edit)[1]->getClips()[0]))
        {
            arrangement->setClipWarpForDemo (*c1);
            std::cout << "warp clip length " << c1->getPosition().getLength().inSeconds() << " speed " << c1->getSpeedRatio()
                      << " bpm " << c1->getLoopInfo().getBpm (c1->getAudioFile().getInfo()) << std::endl;
        }
    }

    // --snapshot <file.png>: render the window to an image once thumbnails are ready, then quit.
    // Handy for visual checks and CI without touching the screen.
    const int snapIndex = args.indexOf ("--snapshot");

    if (snapIndex >= 0 && snapIndex + 1 < args.size())
    {
        const juce::File target (args[snapIndex + 1]);

        juce::Timer::callAfterDelay (5000, [this, target]
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
    PluginUI::closeAllWindows();
    pianoRoll = nullptr;
    mixer = nullptr;
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

    if (mixer->isVisible())
        mixer->setBounds (r.removeFromBottom (juce::jmin (260, r.getHeight() / 2)));

    if (pianoRoll->isVisible())
        pianoRoll->setBounds (r.removeFromBottom (juce::jmin (320, r.getHeight() / 2)));

    arrangement->setBounds (r);
}

//==============================================================================
// Project lifecycle

void MainComponent::setEdit (std::unique_ptr<te::Edit> newEdit, const juce::File& file)
{
    if (edit != nullptr)
        edit->getTransport().stop (false, false);

    // Views reference the edit, so they have to go before it does
    PluginUI::closeAllWindows();
    pianoRoll = nullptr;
    mixer = nullptr;
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

    pianoRoll = std::make_unique<PianoRoll> (*edit);
    mixer = std::make_unique<MixerView> (*edit);
    transportBar->onToggleMixer = [this] { setMixerVisible (! mixerVisible); };
    arrangement->onOpenMidiClip = [this] (te::EditItemID id)
    {
        pianoRoll->setClip (id);
        setPianoRollVisible (true);
    };

    addAndMakeVisible (*transportBar);
    addAndMakeVisible (*arrangement);
    addChildComponent (*pianoRoll);
    addChildComponent (*mixer);
    mixer->setVisible (mixerVisible);
    transportBar->setMixerShown (mixerVisible);
    resized();
}

void MainComponent::setMixerVisible (bool shouldBeVisible)
{
    mixerVisible = shouldBeVisible;
    mixer->setVisible (shouldBeVisible);
    transportBar->setMixerShown (shouldBeVisible);
    resized();
}

void MainComponent::askForText (const juce::String& title, const juce::String& message, const juce::String& initial,
                                std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("text", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, cb = std::move (done)] (int r)
    {
        if (r == 1)
            cb (w->getTextEditorContents ("text"));
    }), true);
}

void MainComponent::exportAudioFromChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Export audio (WAV)",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (getProjectName() + ".wav"),
                                                       "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File())
                                      return;

                                  f = f.withFileExtension ("wav");
                                  edit->getTransport().stop (false, false);

                                  // Render the arrangement from bar 1 to the end of the last clip (plus a short tail)
                                  auto end = edit->getLength() + te::TimeDuration::fromSeconds (2.0);
                                  juce::BigInteger tracks;
                                  tracks.setRange (0, te::getAllTracks (*edit).size(), true);

                                  const bool ok = te::Renderer::renderToFile ("Export", f, *edit, { 0_tp, te::TimePosition() + end },
                                                                              tracks, true, false, {}, false);

                                  juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon
                                                                             : juce::MessageBoxIconType::WarningIcon,
                                                                          "Export", ok ? "Exported " + f.getFileName()
                                                                                       : "Export failed.");
                              });
}

void MainComponent::setPianoRollVisible (bool shouldBeVisible)
{
    pianoRoll->setVisible (shouldBeVisible);
    resized();

    if (shouldBeVisible)
        pianoRoll->scrollToNotesOnShow();
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
    return { "File", "Edit", "Track", "View", "Project" };
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
        add (exportAudio,   "Export Audio...",   "shift + cmd + E");
    }
    else if (index == 1)
    {
        add (undo, "Undo", "cmd + Z");
        add (redo, "Redo", "shift + cmd + Z");
    }
    else if (index == 2)
    {
        add (addAudioTrack,      "Add Audio Track",       "cmd + T");
        add (addInstrumentTrack, "Add Instrument Track",  "shift + cmd + T");
        add (deleteTrack,        "Delete Selected Track", "");
        m.addSeparator();
        add (toggleAutomation,   "Show/Hide Automation",  "cmd + A");
        add (trackPlugins,       "Plug-ins...",           "cmd + P");
    }
    else if (index == 4)
    {
        add (insertTempo,   "Insert Tempo Change at Playhead...", "");
        add (resetTempoMap, "Remove Tempo Changes",                "");
        add (setTimeSig,    "Set Time Signature...",               "");
        add (insertTimeSig, "Insert Time Signature Change at Playhead...", "");
        add (masterPlugins, "Master Plug-ins...",                          "");
        m.addSeparator();
        add (togglePunch,   "Punch In/Out Recording",              "shift + cmd + P");
    }
    else
    {
        add (toggleMixer, "Show/Hide Mixer",   "cmd + M");
        add (toggleLoop,  "Loop",              "cmd + L");
        add (toggleClick, "Metronome",         "cmd + K");
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
        case exportAudio:    exportAudioFromChooser(); break;
        case addAudioTrack:  arrangement->addTrack (false); break;
        case addInstrumentTrack: arrangement->addTrack (true); break;
        case deleteTrack:    arrangement->deleteSelectedTrack(); break;
        case toggleAutomation: arrangement->toggleAutomationOnSelectedTrack(); break;
        case toggleMixer:    setMixerVisible (! mixerVisible); break;
        case toggleLoop:
        {
            auto& t = edit->getTransport();
            t.looping = ! t.looping.get();
            if (t.looping && t.getLoopRange().getLength().inSeconds() <= 0.0)
                t.setLoopRange ({ 0_tp, edit->tempoSequence.toTime (te::BeatPosition::fromBeats (16.0)) });
            break;
        }
        case trackPlugins:
            if (auto* t = arrangement->selectedTrack())
                PluginUI::showTrackMenu (engine, *t);
            break;
        case insertTempo:
        {
            const auto pos = edit->getTransport().getPosition();
            askForText ("Tempo change", "BPM at the playhead:", juce::String (edit->tempoSequence.getBpmAt (pos), 2),
                        [this, pos] (juce::String text)
                        {
                            const auto bpm = text.getDoubleValue();
                            if (bpm < 20.0 || bpm > 400.0)
                                return;
                            edit->getUndoManager().beginNewTransaction();
                            auto beat = edit->tempoSequence.toBeats (pos);
                            if (pos.inSeconds() <= 0.0)
                                edit->tempoSequence.getTempo (0)->setBpm (bpm);
                            else
                                edit->tempoSequence.insertTempo (beat, bpm, 1.0f);
                        });
            break;
        }
        case resetTempoMap:
            edit->getUndoManager().beginNewTransaction();
            while (edit->tempoSequence.getNumTempos() > 1)
                edit->tempoSequence.removeTempo (edit->tempoSequence.getNumTempos() - 1, false);
            break;
        case setTimeSig:
            askForText ("Time signature", "Time signature (e.g. 3/4, 6/8):",
                        edit->tempoSequence.getTimeSig (0)->getStringTimeSig(),
                        [this] (juce::String text)
                        {
                            if (text.containsChar ('/'))
                            {
                                edit->getUndoManager().beginNewTransaction();
                                edit->tempoSequence.getTimeSig (0)->setStringTimeSig (text.trim());
                            }
                        });
            break;
        case insertTimeSig:
        {
            const auto bar = edit->tempoSequence.toBarsAndBeats (edit->getTransport().getPosition()).bars;
            askForText ("Time signature change", "New time signature from bar " + juce::String (bar + 1) + " (e.g. 7/8):", "4/4",
                        [this, bar] (juce::String text)
                        {
                            if (! text.containsChar ('/'))
                                return;
                            edit->getUndoManager().beginNewTransaction();
                            auto start = edit->tempoSequence.toBeats (te::tempo::BarsAndBeats { bar, te::BeatDuration() });
                            if (start.inBeats() <= 0.0)
                                edit->tempoSequence.getTimeSig (0)->setStringTimeSig (text.trim());
                            else if (auto ts = edit->tempoSequence.insertTimeSig (start))
                                ts->setStringTimeSig (text.trim());
                        });
            break;
        }
        case masterPlugins:  PluginUI::showMasterMenu (engine, *edit); break;
        case togglePunch:
            edit->recordingPunchInOut = ! edit->recordingPunchInOut.get();
            if (edit->recordingPunchInOut.get() && edit->getTransport().getLoopRange().getLength().inSeconds() <= 0.0)
                edit->getTransport().setLoopRange ({ 0_tp, edit->tempoSequence.toTime (te::BeatPosition::fromBeats (16.0)) });
            break;
        case toggleClick:    edit->clickTrackEnabled = ! edit->clickTrackEnabled.get(); break;
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

    if (key == juce::KeyPress ('r', cmd, 0))
    {
        if (transport.isRecording())
            transport.stop (false, false);
        else
            transport.record (false, true);
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
        { { 'e', cmd | shift, 0 }, exportAudio },
        { { 't', cmd, 0 },         addAudioTrack },
        { { 't', cmd | shift, 0 }, addInstrumentTrack },
        { { 'a', cmd, 0 },         toggleAutomation },
        { { 'm', cmd, 0 },         toggleMixer },
        { { 'l', cmd, 0 },         toggleLoop },
        { { 'k', cmd, 0 },         toggleClick },
        { { 'p', cmd, 0 },         trackPlugins },
        { { 'p', cmd | shift, 0 }, togglePunch },
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

    if (key == juce::KeyPress::escapeKey && pianoRoll->isVisible())
    {
        setPianoRollVisible (false);
        return true;
    }

    // Delete goes to a selected note first, otherwise to the selected clip
    if (pianoRoll->isVisible() && pianoRoll->keyPressed (key))
        return true;

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
