#pragma once

#include "Common.h"

/*  Plug-in handling: built-in effects, scanned AU/VST3 plug-ins, and editor windows. */
namespace PluginUI
{
    /** Loads the cached plug-in list from disk (call once at startup). */
    void loadKnownPlugins (te::Engine&);

    /** Popup for a track's plug-in chain: open/bypass/remove existing plug-ins, add effects or instruments, scan. */
    void showTrackMenu (te::Engine&, te::AudioTrack&);

    /** Same for the master chain (effects only). */
    void showMasterMenu (te::Engine&, te::Edit&);

    void openEditor (te::Plugin&);

    /** Closes every plug-in window; call before the Edit is destroyed. */
    void closeAllWindows();
}
