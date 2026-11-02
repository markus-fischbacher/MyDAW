#include "BrowserPanel.h"

BrowserPanel::BrowserPanel (te::Engine& e)
    : engine (e),
      filter (e.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats(), "*", "Audio files"),
      contents (&filter, thread),
      tree (contents)
{
    thread.startThread (juce::Thread::Priority::background);
    contents.setDirectory (juce::File::getSpecialLocation (juce::File::userMusicDirectory), true, true);

    tree.setColour (juce::FileTreeComponent::backgroundColourId, Palette::background);
    tree.setColour (juce::FileTreeComponent::linesColourId, Palette::gridBar);
    tree.setColour (juce::DirectoryContentsDisplayComponent::textColourId, Palette::text);
    tree.setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, Palette::accent.withAlpha (0.35f));
    tree.setDragAndDropDescription (dragDescription);
    tree.addListener (this);
    addAndMakeVisible (tree);

    folderButton.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (folderButton);

    previewToggle.setToggleState (true, juce::dontSendNotification);
    previewToggle.setColour (juce::ToggleButton::textColourId, Palette::text);
    previewToggle.onClick = [this] { if (! previewToggle.getToggleState()) stopPreview(); };
    addAndMakeVisible (previewToggle);

    pathLabel.setColour (juce::Label::textColourId, Palette::textDim);
    pathLabel.setFont (juce::FontOptions (11.0f));
    pathLabel.setText (contents.getDirectory().getFileName(), juce::dontSendNotification);
    addAndMakeVisible (pathLabel);

    engine.getDeviceManager().deviceManager.addAudioCallback (&player);
    player.setSource (&transport);
}

BrowserPanel::~BrowserPanel()
{
    tree.removeListener (this);
    stopPreview();
    player.setSource (nullptr);
    engine.getDeviceManager().deviceManager.removeAudioCallback (&player);
    thread.stopThread (2000);
}

void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (Palette::panel);
    g.setColour (Palette::gridBar);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void BrowserPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (24);
    folderButton.setBounds (top.removeFromLeft (80));
    previewToggle.setBounds (top.removeFromRight (80));
    pathLabel.setBounds (top.reduced (6, 0));
    r.removeFromTop (6);
    tree.setBounds (r);
}

void BrowserPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Browser folder", contents.getDirectory());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();

                              if (f.isDirectory())
                              {
                                  contents.setDirectory (f, true, true);
                                  pathLabel.setText (f.getFileName(), juce::dontSendNotification);
                              }
                          });
}

void BrowserPanel::selectionChanged()
{
    auto f = tree.getSelectedFile (0);

    if (previewToggle.getToggleState() && f.existsAsFile())
        preview (f);
}

void BrowserPanel::fileDoubleClicked (const juce::File& f)
{
    stopPreview();

    if (f.existsAsFile() && onInsertFile)
        onInsertFile (f);
}

void BrowserPanel::preview (const juce::File& f)
{
    stopPreview();

    if (auto* reader = engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f))
    {
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        transport.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
        transport.start();
    }
}

void BrowserPanel::stopPreview()
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource = nullptr;
}
