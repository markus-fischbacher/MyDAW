#pragma once

#include <JuceHeader.h>

namespace te = tracktion;
using namespace tracktion::literals;

// Shared colour palette for the UI. Tweak here, not in paint() calls.
namespace Palette
{
    const juce::Colour background   { 0xff1e1f22 };
    const juce::Colour panel        { 0xff2a2c30 };
    const juce::Colour laneA        { 0xff25272b };
    const juce::Colour laneB        { 0xff232428 };
    const juce::Colour gridBar      { 0xff3c3f45 };
    const juce::Colour gridBeat     { 0xff2e3035 };
    const juce::Colour text         { 0xffd8dade };
    const juce::Colour textDim      { 0xff8a8f98 };
    const juce::Colour accent       { 0xffffa53a };
    const juce::Colour playhead     { 0xffff5a4f };
    const juce::Colour clip         { 0xff4f8fd8 };
    const juce::Colour midiClip     { 0xff58b368 };
}

/** Runs the engine's progress-bar tasks (rendering, tempo detection) synchronously on the calling thread. */
struct SyncUIBehaviour : public te::UIBehaviour
{
    void showWarningMessage (const juce::String& m) override  {
        DBG ("engine warning: " << m);
        juce::MessageManager::callAsync ([m] { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Arranger", m); });
    }

    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& job) override
    {
        while (job.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {}
    }
};
