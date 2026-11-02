#include "MainComponent.h"
#include "PluginUI.h"

static constexpr int numInitialTracks = 8;

MainComponent::MainComponent()
{
    loadBindings();
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

    // --demo-auto: automation on volume + pan, all lanes expanded, a loop range
    if (args.contains ("--demo-auto"))
    {
        auto* vp = te::getAudioTracks (*edit)[0]->getVolumePlugin();
        vp->volParam->getCurve().addPoint (0_tp, 0.6f, 0.0f, nullptr);
        vp->volParam->getCurve().addPoint (4_tp, 0.9f, 0.0f, nullptr);
        vp->panParam->getCurve().addPoint (1_tp, -0.5f, 0.0f, nullptr);
        vp->panParam->getCurve().addPoint (6_tp, 0.5f, 0.0f, nullptr);
        edit->getTransport().setLoopRange ({ 2_tp, 6_tp });
        edit->getTransport().looping = true;
        arrangement->toggleExpandAllAutomation();
    }

    // --demo-freeze: bounce the demo MIDI track to a new audio track
    if (args.contains ("--demo-freeze"))
    {
        auto* track = te::getAudioTracks (*edit)[0];
        arrangement->bounceToNewTrack (*track, { 0_tp, 2_tp }, "Loop", [track] { track->setMute (true); });
    }

    // --demo-consolidate: render the first clip of track 2 in place
    if (args.contains ("--demo-consolidate"))
        if (auto* track = te::getAudioTracks (*edit)[1])
            if (track->getClips().size() > 0)
                arrangement->consolidateClip (track->getClips()[0]->itemID);

    // --demo-comp: add takes to the track 2 clip and swipe one over part of it
    if (args.contains ("--demo-comp"))
        arrangement->setupCompDemo (1);

    // --demo-bus: a return track, a send on track 1, track 2 routed into track 3
    if (args.contains ("--demo-bus"))
    {
        arrangement->addReturnTrack();
        auto tracks = te::getAudioTracks (*edit);
        if (auto p = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {}))
        {
            tracks[0]->pluginList.insertPlugin (p, -1, nullptr);
            if (auto* sp = tracks[0]->pluginList.findFirstPluginOfType<te::AuxSendPlugin>())
            {
                sp->busNumber = 0;
                sp->setGainDb (-6.0f);
            }
        }
        tracks[1]->getOutput().setOutputToTrack (tracks[2]);
        tracks[2]->setName ("Drum Bus");
        setMixerVisible (true);
        setBrowserVisible (true);
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
    browser = nullptr;
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

    if (browser != nullptr && browser->isVisible())
        browser->setBounds (r.removeFromLeft (260));

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
        struct Panel : juce::Component
        {
            Panel (te::Engine& e)
                : engine (e), selector (e.getDeviceManager().deviceManager, 0, 512, 1, 512, true, true, true, false)
            {
                addAndMakeVisible (selector);
                addAndMakeVisible (label);
                addAndMakeVisible (offset);
                label.setText ("Recording latency offset (ms) - shifts recorded audio earlier/later to compensate interface delay", juce::dontSendNotification);
                label.setFont (juce::FontOptions (12.0f));
                offset.setRange (-250.0, 250.0, 0.5);
                offset.setTextValueSuffix (" ms");
                offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 22);

                auto devices = engine.getDeviceManager().getWaveInputDevices();
                offset.setValue (devices.empty() ? 0.0 : devices.front()->getRecordAdjustmentMs(), juce::dontSendNotification);
                offset.onValueChange = [this]
                {
                    for (auto& d : engine.getDeviceManager().getWaveInputDevices())
                        d->setRecordAdjustmentMs (offset.getValue());
                };
                setSize (500, 480);
            }

            void resized() override
            {
                auto r = getLocalBounds();
                auto bottom = r.removeFromBottom (70).reduced (8, 4);
                label.setBounds (bottom.removeFromTop (30));
                offset.setBounds (bottom.removeFromTop (28));
                selector.setBounds (r);
            }

            te::Engine& engine;
            juce::AudioDeviceSelectorComponent selector;
            juce::Label label;
            juce::Slider offset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        };

        o.content.setOwned (new Panel (engine));
        o.launchAsync();
    };

    arrangement = std::make_unique<ArrangementView> (*edit);

    pianoRoll = std::make_unique<PianoRoll> (*edit);
    mixer = std::make_unique<MixerView> (*edit);
    browser = std::make_unique<BrowserPanel> (engine);
    browser->onInsertFile = [this] (const juce::File& f)
    {
        int index = 0;
        if (auto* t = arrangement->selectedTrack())
            index = juce::jmax (0, te::getAudioTracks (*edit).indexOf (t));
        arrangement->insertAudioFile (f, index, edit->getTransport().getPosition());
    };
    arrangement->getDraggedBrowserFile = [this] { return browser->getSelectedFile(); };
    addChildComponent (*browser);
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

void MainComponent::setBrowserVisible (bool shouldBeVisible)
{
    browserVisible = shouldBeVisible;
    browser->setVisible (shouldBeVisible);
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

static juce::File findLame()
{
    for (auto* path : { "/opt/homebrew/bin/lame", "/usr/local/bin/lame", "/usr/bin/lame" })
        if (juce::File (path).existsAsFile())
            return juce::File (path);

    return {};
}

/** Encodes a rendered WAV to 320 kbps MP3 with the LAME command-line encoder, then deletes the WAV. */
static juce::File encodeMp3 (const juce::File& lame, const juce::File& wav, const juce::File& mp3)
{
    mp3.deleteFile();
    juce::ChildProcess p;
    p.start (juce::StringArray { lame.getFullPathName(), "-b", "320", "--silent", wav.getFullPathName(), mp3.getFullPathName() });
    p.waitForProcessToFinish (10 * 60 * 1000);
    wav.deleteFile();
    return mp3.existsAsFile() ? mp3 : juce::File();
}

void MainComponent::exportAudioFromChooser()
{
    const auto lame = findLame();
    auto* w = new juce::AlertWindow ("Export Audio", "Choose the format, loudness and what to render.", juce::MessageBoxIconType::NoIcon);
    juce::StringArray fmtNames { "WAV 16-bit", "WAV 24-bit", "AIFF 24-bit", "FLAC 24-bit", "OGG Vorbis" };
    if (lame.existsAsFile())
        fmtNames.add ("MP3 320 kbps");
    w->addComboBox ("fmt", fmtNames, "Format");
    w->addComboBox ("norm", { "Off", "Peak -0.3 dB", "Loudness -14 LUFS", "Loudness -9 LUFS" }, "Normalise");
    w->addComboBox ("mode", { "Mixdown (one file)", "Stems (one file per track)" }, "Render");
    w->addComboBox ("sr", { "44.1 kHz", "48 kHz", "96 kHz" }, "Sample rate");
    for (auto* name : { "fmt", "norm", "mode", "sr" })
        w->getComboBoxComponent (name)->setSelectedItemIndex (0);
    w->addButton ("Export...", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, lame] (int r)
    {
        if (r != 1)
            return;

        const int fmt = w->getComboBoxComponent ("fmt")->getSelectedItemIndex();
        const int norm = w->getComboBoxComponent ("norm")->getSelectedItemIndex();
        const bool stems = w->getComboBoxComponent ("mode")->getSelectedItemIndex() == 1;
        const double sr = std::array { 44100.0, 48000.0, 96000.0 }[(size_t) w->getComboBoxComponent ("sr")->getSelectedItemIndex()];

        auto& fm = engine.getAudioFileFormatManager();
        const bool mp3 = fmt == 5;
        juce::AudioFormat* format = (fmt <= 1 || mp3) ? fm.getWavFormat() : fmt == 2 ? fm.getAiffFormat() : fmt == 3 ? fm.getFlacFormat() : fm.getOggFormat();
        const int bits = fmt == 0 ? 16 : fmt == 4 ? 16 : 24;
        const juce::String ext = mp3 ? "mp3" : fmt <= 1 ? "wav" : fmt == 2 ? "aiff" : fmt == 3 ? "flac" : "ogg";

        // MP3: render to a temporary WAV beside the target, then encode
        auto render = [lame, mp3, ed = edit.get()] (const te::Renderer::Parameters& params) -> juce::File
        {
            ScopedNoProxies noProxies (*ed);
            if (! mp3)
                return te::Renderer::renderToFile ("Export", params);

            auto p2 = params;
            const auto target = params.destFile;
            p2.destFile = target.withFileExtension ("tmp.wav");
            const auto wav = te::Renderer::renderToFile ("Export", p2);
            return wav.existsAsFile() ? encodeMp3 (lame, wav, target) : juce::File();
        };

        auto makeParams = [this, format, bits, sr, norm] (const juce::File& dest, int trackBit)
        {
            te::Renderer::Parameters p (*edit);
            p.destFile = dest;
            p.audioFormat = format;
            p.bitDepth = bits;
            p.sampleRateForAudio = sr;
            p.time = { 0_tp, te::TimePosition() + (edit->getLength() + te::TimeDuration::fromSeconds (2.0)) };
            p.usePlugins = true;
            p.useMasterPlugins = trackBit < 0;
            p.ditheringEnabled = bits < 24;

            if (trackBit >= 0)
                p.tracksToDo.setBit (trackBit);
            else
                p.tracksToDo.setRange (0, te::getAllTracks (*edit).size(), true);

            if (norm == 1)       { p.shouldNormalise = true; p.normaliseToLevelDb = -0.3f; }
            else if (norm >= 2)  { p.shouldNormaliseByLUFS = true; p.normaliseToLevelDb = norm == 2 ? -14.0f : -9.0f; }

            return p;
        };

        edit->getTransport().stop (false, false);

        if (! stems)
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Export audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                                                                    .getChildFile (getProjectName() + "." + ext), "*." + ext);
            fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::warnAboutOverwriting,
                                      [this, ext, makeParams, render] (const juce::FileChooser& fc)
                                      {
                                          auto f = fc.getResult();
                                          if (f == juce::File())
                                              return;

                                          f = f.withFileExtension (ext);
                                          const auto out = render (makeParams (f, -1));
                                          juce::AlertWindow::showMessageBoxAsync (out.existsAsFile() ? juce::MessageBoxIconType::InfoIcon
                                                                                                     : juce::MessageBoxIconType::WarningIcon,
                                                                                  "Export", out.existsAsFile() ? "Exported " + out.getFileName() : "Export failed.");
                                      });
        }
        else
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Folder for stems", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                      [this, ext, makeParams, render] (const juce::FileChooser& fc)
                                      {
                                          auto dir = fc.getResult();
                                          if (! dir.isDirectory())
                                              return;

                                          int done = 0;
                                          auto all = te::getAllTracks (*edit);
                                          for (int i = 0; i < all.size(); ++i)
                                          {
                                              auto* t = dynamic_cast<te::AudioTrack*> (all[i]);
                                              if (t == nullptr || t->getClips().isEmpty())
                                                  continue;

                                              auto name = juce::String (++done).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (t->getName()) + "." + ext;
                                              if (render (makeParams (dir.getChildFile (name), i)).existsAsFile())
                                                  continue;
                                              --done;
                                          }

                                          juce::AlertWindow::showMessageBoxAsync (done > 0 ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                                                  "Export", done > 0 ? "Exported " + juce::String (done) + " stems to " + dir.getFileName()
                                                                                                     : "No stems exported.");
                                      });
        }
    }), true);
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
    addToRecent (file);
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
        addToRecent (file);
        autosaveFile().deleteFile();
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

    if (++autosaveTicks >= 4 * 60)      // every minute
    {
        autosaveTicks = 0;
        autosave();
    }
}

juce::PropertiesFile& MainComponent::settings()
{
    if (settingsFile == nullptr)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = JUCE_APPLICATION_NAME_STRING;
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settingsFile = std::make_unique<juce::PropertiesFile> (o);
    }

    return *settingsFile;
}

juce::StringArray MainComponent::getRecent() const
{
    auto list = juce::StringArray::fromLines (const_cast<MainComponent*> (this)->settings().getValue ("recent"));
    list.removeEmptyStrings();
    return list;
}

void MainComponent::addToRecent (const juce::File& f)
{
    if (f == juce::File())
        return;

    auto list = getRecent();
    list.removeString (f.getFullPathName());
    list.insert (0, f.getFullPathName());
    while (list.size() > 10)
        list.remove (list.size() - 1);

    settings().setValue ("recent", list.joinIntoString ("\n"));
    settings().saveIfNeeded();
}

juce::File MainComponent::autosaveFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (JUCE_APPLICATION_NAME_STRING).getChildFile ("Autosave")
        .getChildFile ((projectFile == juce::File() ? juce::String ("Untitled") : projectFile.getFileNameWithoutExtension()) + "-autosave" + projectSuffix);
}

void MainComponent::autosave()
{
    if (edit == nullptr || ! edit->hasChangedSinceSaved())
        return;

    auto f = autosaveFile();
    f.getParentDirectory().createDirectory();
    te::EditFileOperations (*edit).writeToFile (f, false);
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

    auto add = [this, &m] (int id, const juce::String& name, const juce::String& shortcut)
    {
        juce::PopupMenu::Item item (name);
        item.setID (id);
        item.shortcutKeyDescription = shortcutText (id).isNotEmpty() ? shortcutText (id) : shortcut;
        m.addItem (item);
    };

    if (index == 0)
    {
        add (newProject,    "New Project",       "cmd + N");
        add (openProject,   "Open Project...",   "cmd + O");

        juce::PopupMenu recent;
        auto list = getRecent();
        for (int i = 0; i < list.size(); ++i)
            recent.addItem (recentBase + i, juce::File (list[i]).getFileName());
        if (list.isEmpty())
            recent.addItem (-1, "(none)", false);
        m.addSubMenu ("Open Recent", recent);
        add (recoverAutosave, "Recover Autosave...", "");
        m.addSeparator();
        add (saveProject,   "Save",              "cmd + S");
        add (saveProjectAs, "Save As...",        "shift + cmd + S");
        m.addSeparator();
        add (importAudio,   "Import Audio...",   "cmd + I");
        add (importMidi,    "Import MIDI File...", "");
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
        add (addReturnTrack,     "Add Return Track (Send Bus)", "");
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
        add (toggleBrowser, "Show/Hide Browser", "cmd + B");
        add (zoomFit,     "Zoom to Fit Project", "shift + cmd + F");
        add (expandAutomation, "Expand / Collapse All Automation", "shift + cmd + A");
        add (toggleLoop,  "Loop",              "cmd + L");
        add (toggleClick, "Metronome",         "cmd + K");
        m.addSeparator();
        add (editShortcuts, "Keyboard Shortcuts...", "");
    }

    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    if (id >= recentBase && id < recentBase + 10)
    {
        auto list = getRecent();
        if (id - recentBase < list.size())
            confirmDiscardChanges ([this, f = juce::File (list[id - recentBase])] { openProjectFile (f); });
        return;
    }

    switch (id)
    {
        case newProject:     confirmDiscardChanges ([this] { newEmptyProject(); }); break;
        case openProject:    chooseAndOpenProject(); break;
        case saveProject:    saveCurrentProject(); break;
        case saveProjectAs:  saveProjectAsFromChooser(); break;
        case importAudio:    addAudioFileFromChooser(); break;
        case importMidi:
            fileChooser = std::make_unique<juce::FileChooser> ("Import MIDI", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.mid;*.midi");
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc)
                                      {
                                          if (! fc.getResult().existsAsFile())
                                              return;
                                          auto* t = arrangement->selectedTrack();
                                          const auto tracks = te::getAudioTracks (*edit);
                                          const int idx = t != nullptr ? tracks.indexOf (t) : 0;
                                          arrangement->importMidiFile (fc.getResult(), juce::jmax (0, idx),
                                                                       edit->getTransport().getPosition());
                                      });
            break;
        case exportAudio:    exportAudioFromChooser(); break;
        case addAudioTrack:  arrangement->addTrack (false); break;
        case addInstrumentTrack: arrangement->addTrack (true); break;
        case addReturnTrack:     arrangement->addReturnTrack(); break;
        case deleteTrack:    arrangement->deleteSelectedTrack(); break;
        case toggleAutomation: arrangement->toggleAutomationOnSelectedTrack(); break;
        case toggleMixer:    setMixerVisible (! mixerVisible); break;
        case toggleBrowser:  setBrowserVisible (! browserVisible); break;
        case zoomFit:        arrangement->zoomToFit(); break;
        case expandAutomation: arrangement->toggleExpandAllAutomation(); break;
        case recoverAutosave:
        {
            auto dir = autosaveFile().getParentDirectory();
            fileChooser = std::make_unique<juce::FileChooser> ("Recover autosave", dir, juce::String ("*") + projectSuffix);
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc)
                                      {
                                          if (fc.getResult().existsAsFile())
                                              confirmDiscardChanges ([this, f = fc.getResult()] { openProjectFile (f); });
                                      });
            break;
        }
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
        case editShortcuts: showShortcutEditor(); break;
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
// Keyboard shortcut bindings

static const std::vector<juce::String>& reservedKeyNames()
{
    // Handled outside the binding table (transport, piano roll and arrangement editing)
    static const std::vector<juce::String> names { "spacebar", "return", "home", "escape", "cmd + R", "delete", "backspace",
                                                   "cmd + C", "cmd + X", "cmd + V", "cmd + D" };
    return names;
}

void MainComponent::loadBindings()
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    struct D { int id; const char* name; juce::KeyPress key; };
    const D defaults[] =
    {
        { newProject,          "New Project",              { 'n', cmd, 0 } },
        { openProject,         "Open Project",             { 'o', cmd, 0 } },
        { saveProject,         "Save",                     { 's', cmd, 0 } },
        { saveProjectAs,       "Save As",                  { 's', cmd | shift, 0 } },
        { importAudio,         "Import Audio",             { 'i', cmd, 0 } },
        { exportAudio,         "Export Audio",             { 'e', cmd | shift, 0 } },
        { addAudioTrack,       "Add Audio Track",          { 't', cmd, 0 } },
        { addInstrumentTrack,  "Add Instrument Track",     { 't', cmd | shift, 0 } },
        { toggleAutomation,    "Show/Hide Automation",     { 'a', cmd | juce::ModifierKeys::altModifier, 0 } },
        { expandAutomation,    "Expand All Automation",    { 'a', cmd | shift, 0 } },
        { toggleMixer,         "Show/Hide Mixer",          { 'm', cmd, 0 } },
        { toggleBrowser,       "Show/Hide Browser",        { 'b', cmd, 0 } },
        { toggleLoop,          "Loop",                     { 'l', cmd, 0 } },
        { toggleClick,         "Metronome",                { 'k', cmd, 0 } },
        { trackPlugins,        "Track Plug-ins",           { 'p', cmd, 0 } },
        { togglePunch,         "Punch In/Out",             { 'p', cmd | shift, 0 } },
        { zoomFit,             "Zoom to Fit",              { 'f', cmd | shift, 0 } },
        { undo,                "Undo",                     { 'z', cmd, 0 } },
        { redo,                "Redo",                     { 'z', cmd | shift, 0 } },
    };

    bindings.clear();

    for (auto& d : defaults)
    {
        Binding b { d.id, d.name, d.key, d.key };
        const auto saved = settings().getValue ("key_" + juce::String (d.id), "default");

        if (saved == "none")
            b.key = juce::KeyPress();
        else if (saved != "default")
            if (auto k = juce::KeyPress::createFromDescription (saved); k.isValid())
                b.key = k;

        bindings.push_back (b);
    }
}

void MainComponent::saveBindings()
{
    for (auto& b : bindings)
    {
        const auto name = "key_" + juce::String (b.menuID);

        if (b.key == b.defaultKey)             settings().removeValue (name);
        else if (! b.key.isValid())            settings().setValue (name, "none");
        else                                   settings().setValue (name, b.key.getTextDescription());
    }

    settings().saveIfNeeded();
}

juce::String MainComponent::shortcutText (int menuID) const
{
    for (auto& b : bindings)
        if (b.menuID == menuID)
            return b.key.isValid() ? b.key.getTextDescriptionWithIcons() : juce::String();

    return {};
}

class MainComponent::ShortcutEditor : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ShortcutEditor (MainComponent& o) : owner (o)
    {
        list.setModel (this);
        list.setRowHeight (24);
        list.setWantsKeyboardFocus (false);
        addAndMakeVisible (list);

        status.setJustificationType (juce::Justification::centredLeft);
        status.setText ("Select a command, then press the new key combination. Delete clears it, Esc cancels.", juce::dontSendNotification);
        addAndMakeVisible (status);

        reset.onClick = [this]
        {
            for (auto& b : owner.bindings)
                b.key = b.defaultKey;
            owner.saveBindings();
            list.updateContent();
            list.repaint();
            status.setText ("All shortcuts reset to their defaults.", juce::dontSendNotification);
        };
        addAndMakeVisible (reset);
        setWantsKeyboardFocus (true);
        setSize (460, 520);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto bottom = r.removeFromBottom (28);
        reset.setBounds (bottom.removeFromRight (140));
        status.setBounds (bottom);
        r.removeFromBottom (6);
        list.setBounds (r);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const int row = list.getSelectedRow();
        if (! juce::isPositiveAndBelow (row, (int) owner.bindings.size()))
            return false;

        auto& b = owner.bindings[(size_t) row];

        if (key == juce::KeyPress::escapeKey)
        {
            list.deselectAllRows();
            status.setText ("Cancelled.", juce::dontSendNotification);
            return true;
        }

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            b.key = juce::KeyPress();
            finish (b, "Cleared the shortcut for " + b.name + ".");
            return true;
        }

        if (! key.getModifiers().isAnyModifierKeyDown() && key.getKeyCode() < 32 && key.getKeyCode() != 0)
            return true;

        for (auto& name : reservedKeyNames())
            if (juce::KeyPress::createFromDescription (name) == key)
            {
                status.setText (key.getTextDescription() + " is reserved (" + name + "). Try another key.", juce::dontSendNotification);
                return true;
            }

        for (auto& other : owner.bindings)
            if (&other != &b && other.key == key)
            {
                status.setText (key.getTextDescription() + " is already used by \"" + other.name + "\". Clear that one first.", juce::dontSendNotification);
                return true;
            }

        b.key = key;
        finish (b, b.name + " = " + key.getTextDescription());
        return true;
    }

private:
    void finish (Binding&, const juce::String& message)
    {
        owner.saveBindings();
        status.setText (message, juce::dontSendNotification);
        list.deselectAllRows();
        list.repaint();
    }

    int getNumRows() override { return (int) owner.bindings.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) owner.bindings.size()))
            return;

        auto& b = owner.bindings[(size_t) row];
        g.fillAll (selected ? Palette::accent.withAlpha (0.35f) : juce::Colours::transparentBlack);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (14.0f);
        g.drawText (b.name, 8, 0, w - 180, h, juce::Justification::centredLeft);
        g.setColour (selected ? juce::Colours::white : Palette::accent);
        g.drawText (selected ? "press a key..." : (b.key.isValid() ? b.key.getTextDescriptionWithIcons() : "-"), w - 170, 0, 160, h,
                    juce::Justification::centredRight);
    }

    void selectedRowsChanged (int row) override
    {
        if (row >= 0)
            grabKeyboardFocus();
    }

    MainComponent& owner;
    juce::ListBox list;
    juce::Label status;
    juce::TextButton reset { "Reset All" };
};

void MainComponent::showShortcutEditor()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ShortcutEditor (*this));
    o.dialogTitle = "Keyboard Shortcuts";
    o.dialogBackgroundColour = juce::Colour (0xff22262b);
    o.escapeKeyTriggersCloseButton = false;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
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

    for (auto& bnd : bindings)
    {
        if (bnd.key.isValid() && key == bnd.key)
        {
            menuItemSelected (bnd.menuID, 0);
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
