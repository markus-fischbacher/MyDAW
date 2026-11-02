#include "PluginUI.h"

namespace
{
    juce::File getCacheFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile (JUCE_APPLICATION_NAME_STRING).getChildFile ("plugins.xml");
    }

    // Generic editor for built-in plug-ins: one slider per automatable parameter
    class ParameterPanel : public juce::Component
    {
    public:
        explicit ParameterPanel (te::Plugin& p)
        {
            for (auto* param : p.getAutomatableParameters())
            {
                auto row = std::make_unique<Row> (*param);
                addAndMakeVisible (*row);
                rows.add (std::move (row));
            }
            setSize (360, juce::jmax (60, rows.size() * 28 + 12));
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 6);
            for (auto* row : rows)
                row->setBounds (r.removeFromTop (28));
        }

    private:
        struct Row : public juce::Component
        {
            explicit Row (te::AutomatableParameter& p) : param (&p)
            {
                label.setText (p.getParameterName(), juce::dontSendNotification);
                label.setColour (juce::Label::textColourId, Palette::text);
                slider.setSliderStyle (juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 20);
                slider.setRange (0.0, 1.0, 0.0);
                slider.onDragStart = [this] { param->getEdit().getUndoManager().beginNewTransaction(); };
                slider.setValue (p.getCurrentNormalisedValue(), juce::dontSendNotification);
                slider.textFromValueFunction = [this] (double v) { return param->valueToString (param->valueRange.convertFrom0to1 ((float) v)); };
                slider.onValueChange = [this] { param->setNormalisedParameter ((float) slider.getValue(), juce::sendNotificationSync); };
                addAndMakeVisible (label);
                addAndMakeVisible (slider);
            }

            void resized() override
            {
                auto r = getLocalBounds();
                label.setBounds (r.removeFromLeft (110));
                slider.setBounds (r);
            }

            te::AutomatableParameter::Ptr param;
            juce::Label label;
            juce::Slider slider;
        };

        juce::OwnedArray<Row> rows;
    };

    class PluginWindow : public juce::DocumentWindow
    {
    public:
        PluginWindow (te::Plugin& p, std::function<void (PluginWindow*)> onClose)
            : DocumentWindow (p.getName(), Palette::panel, DocumentWindow::closeButton),
              plugin (&p), closed (std::move (onClose))
        {
            setUsingNativeTitleBar (true);

            if (auto* ext = dynamic_cast<te::ExternalPlugin*> (&p))
            {
                if (auto* instance = ext->getAudioPluginInstance())
                {
                    std::unique_ptr<juce::AudioProcessorEditor> ed (instance->createEditorIfNeeded());
                    if (ed == nullptr)
                        ed = std::make_unique<juce::GenericAudioProcessorEditor> (*instance);
                    setContentOwned (ed.release(), true);
                }
            }

            if (getContentComponent() == nullptr)
                setContentOwned (new ParameterPanel (p), true);

            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override  { closed (this); }

        te::Plugin::Ptr plugin;

    private:
        std::function<void (PluginWindow*)> closed;
    };

    juce::OwnedArray<PluginWindow>& windows()
    {
        static juce::OwnedArray<PluginWindow> w;
        return w;
    }

    // Background plug-in scan; keeps itself alive until finished
    class Scanner : private juce::Thread
    {
    public:
        static void start (te::Engine& e)
        {
            if (instance == nullptr)
                instance.reset (new Scanner (e));
        }

        ~Scanner() override { stopThread (20000); }

    private:
        explicit Scanner (te::Engine& e) : Thread ("Plugin scan"), engine (e)
        {
            startThread();
        }

        void run() override
        {
            auto& pm = engine.getPluginManager();
            juce::KnownPluginList found;

            for (auto* format : pm.pluginFormatManager.getFormats())
            {
                if (threadShouldExit())
                    return;

                juce::PluginDirectoryScanner scanner (found, *format, format->getDefaultLocationsToSearch(), true, juce::File());
                juce::String name;
                while (! threadShouldExit() && scanner.scanNextFile (true, name))
                {}
            }

            auto xml = found.createXml();
            juce::MessageManager::callAsync ([this, xml = std::move (xml)]
            {
                auto& list = engine.getPluginManager().knownPluginList;
                if (xml != nullptr)
                    list.recreateFromXml (*xml);

                auto file = getCacheFile();
                file.getParentDirectory().createDirectory();
                if (auto x = list.createXml())
                    x->writeTo (file);

                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Plug-in scan",
                                                        "Found " + juce::String (list.getNumTypes()) + " plug-ins.");
                instance.reset();    // deletes this, which joins the (finished) thread
            });
        }

        te::Engine& engine;
        static inline std::unique_ptr<Scanner> instance;
    };

    int findInsertIndex (te::PluginList& list)
    {
        int i = 0;
        for (auto* p : list)
        {
            if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr)
                return i;
            ++i;
        }
        return -1;
    }

    bool isChainBasics (te::Plugin* p)
    {
        return dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr;
    }
}

namespace PluginUI
{
    void loadKnownPlugins (te::Engine& engine)
    {
        if (auto xml = juce::XmlDocument::parse (getCacheFile()))
            engine.getPluginManager().knownPluginList.recreateFromXml (*xml);
    }

    void openEditor (te::Plugin& p)
    {
        for (auto* w : windows())
            if (w->plugin.get() == &p)
            {
                w->toFront (true);
                return;
            }

        windows().add (new PluginWindow (p, [] (PluginWindow* w) { windows().removeObject (w); }));
    }

    void closeAllWindows()
    {
        windows().clear();
    }

    static void showChainMenu (te::Engine&, te::Edit&, te::PluginList&, bool allowInstruments);

    void showTrackMenu (te::Engine& engine, te::AudioTrack& track)
    {
        showChainMenu (engine, track.edit, track.pluginList, true);
    }

    void showMasterMenu (te::Engine& engine, te::Edit& edit)
    {
        showChainMenu (engine, edit, edit.getMasterPluginList(), false);
    }

    static void showChainMenu (te::Engine& engine, te::Edit& edit, te::PluginList& list, bool allowInstruments)
    {
        struct Builtin { const char* label; const char* type; };
        static const Builtin builtins[] =
        {
            { "Reverb", te::ReverbPlugin::xmlTypeName },       { "Delay", te::DelayPlugin::xmlTypeName },
            { "Chorus", te::ChorusPlugin::xmlTypeName },       { "Phaser", te::PhaserPlugin::xmlTypeName },
            { "Compressor", te::CompressorPlugin::xmlTypeName }, { "Equaliser", te::EqualiserPlugin::xmlTypeName },
            { "Low/High-pass filter", te::LowPassPlugin::xmlTypeName }, { "Pitch shift", te::PitchShiftPlugin::xmlTypeName },
        };

        auto types = engine.getPluginManager().knownPluginList.getTypes();
        auto chain = std::make_shared<juce::Array<te::Plugin::Ptr>>();
        for (auto* p : list)
            if (! isChainBasics (p))
                chain->add (p);

        constexpr int openBase = 100, bypassBase = 200, removeBase = 300, builtinBase = 400, sidechainBase = 100000, externalBase = 1000, scanID = 1;
        auto scNames = std::make_shared<juce::Array<juce::StringArray>>();
        juce::PopupMenu menu;

        for (int i = 0; i < chain->size(); ++i)
        {
            juce::PopupMenu sub;
            auto p = chain->getUnchecked (i);
            sub.addItem (openBase + i, "Open Editor");
            sub.addItem (bypassBase + i, "Bypass", true, ! p->isEnabled());
            sub.addItem (removeBase + i, "Remove");
            scNames->add (p->canSidechain() ? p->getSidechainSourceNames (true) : juce::StringArray());
            if (! scNames->getReference (i).isEmpty())
            {
                juce::PopupMenu sc;
                const auto current = p->getSidechainSourceName();
                for (int s = 0; s < scNames->getReference (i).size(); ++s)
                    sc.addItem (sidechainBase + i * 20 + s, scNames->getReference (i)[s], true, scNames->getReference (i)[s] == current);
                sub.addSubMenu ("Sidechain Source", sc);
            }
            menu.addSubMenu (p->getName(), sub);
        }

        if (chain->isEmpty())
            menu.addItem (-1, "No plug-ins on this track", false);

        menu.addSeparator();

        juce::PopupMenu effects, instruments;
        for (int i = 0; i < (int) std::size (builtins); ++i)
            effects.addItem (builtinBase + i, builtins[i].label);

        effects.addSeparator();
        for (int i = 0; i < types.size(); ++i)
            (types.getReference (i).isInstrument ? instruments : effects).addItem (externalBase + i,
                                                                                    types[i].name + "  (" + types[i].manufacturerName + ")");

        menu.addSubMenu ("Add Effect", effects);
        if (allowInstruments)
            menu.addSubMenu ("Add Instrument", instruments, instruments.getNumItems() > 0);
        menu.addSeparator();
        menu.addItem (scanID, "Scan for Plug-ins (AU / VST3)...");

        menu.showMenuAsync ({}, [&engine, &edit, &list, chain, scNames, types, builtins = builtins] (int r)
        {
            if (r == 0)
                return;

            edit.getUndoManager().beginNewTransaction();

            auto insert = [&] (te::Plugin::Ptr p, bool instrument)
            {
                if (p == nullptr)
                    return;
                list.insertPlugin (p, instrument ? 0 : findInsertIndex (list), nullptr);
                openEditor (*p);
            };

            if (r == scanID)                         Scanner::start (engine);
            else if (r >= sidechainBase)             chain->getUnchecked ((r - sidechainBase) / 20)->setSidechainSourceByName (scNames->getReference ((r - sidechainBase) / 20)[(r - sidechainBase) % 20]);
            else if (r >= externalBase)
            {
                auto& d = types.getReference (r - externalBase);
                insert (edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, d), d.isInstrument);
            }
            else if (r >= builtinBase)               insert (edit.getPluginCache().createNewPlugin (builtins[r - builtinBase].type, {}), false);
            else if (r >= removeBase)                chain->getUnchecked (r - removeBase)->deleteFromParent();
            else if (r >= bypassBase)                { auto p = chain->getUnchecked (r - bypassBase); p->setEnabled (! p->isEnabled()); }
            else if (r >= openBase)                  openEditor (*chain->getUnchecked (r - openBase));
        });
    }
}
