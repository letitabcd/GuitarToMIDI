#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

class GuitarToMidiApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr) window->toFront (true);
    }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);
        auto holder = std::make_unique<juce::StandalonePluginHolder> (
            properties.getUserSettings(), false);
        // JUCE defaults to muting the input for effects. We need the DI input;
        // the processor independently mutes its output unless Monitor DI is on.
        holder->getMuteInputValue().setValue (false);
        window = std::make_unique<juce::StandaloneFilterWindow> (
            getApplicationName(), juce::Colour (0xff171b20), std::move (holder));
        window->setUsingNativeTitleBar (true);
        window->setVisible (true);
    }

    void systemRequestedQuit() override { quit(); }
    void shutdown() override
    {
        if (window != nullptr) window->pluginHolder->savePluginState();
        window.reset();
        properties.saveIfNeeded();
    }

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
};

START_JUCE_APPLICATION (GuitarToMidiApplication)
