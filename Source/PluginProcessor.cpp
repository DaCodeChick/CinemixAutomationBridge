/*
  ==============================================================================

    CinemixAutomationBridge - PluginProcessor.cpp
    JUCE-based replication of D&R Cinemix VST Automation Bridge
    
    Copyright (c) 2012 Guido Scognamiglio (original VST 2.4 version)
    Copyright (c) 2026 (JUCE 8.0.12 replication)
    
    MIT License - See LICENSE file for details

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
CinemixBridgeProcessor::CinemixBridgeProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Initialize animation phases
    animPhase.fill(0.0f);
    
    // Add parameter listeners for all 161 parameters
    for (int i = 0; i < 72; ++i)
        apvts.addParameterListener("fader_" + juce::String(i), this);
    
    for (int i = 0; i < 72; ++i)
        apvts.addParameterListener("mute_" + juce::String(i), this);
    
    for (int i = 0; i < 10; ++i)
        apvts.addParameterListener("aux_mute_" + juce::String(i), this);
    
    apvts.addParameterListener("joy1_x", this);
    apvts.addParameterListener("joy1_y", this);
    apvts.addParameterListener("joy1_mute", this);
    apvts.addParameterListener("joy2_x", this);
    apvts.addParameterListener("joy2_y", this);
    apvts.addParameterListener("joy2_mute", this);
    apvts.addParameterListener("master_fader", this);
    
    // Setup callback from MidiRouter for incoming MIDI
    midiRouter.onParameterChangeFromConsole = [this](int paramIndex, float value)
    {
        // Update parameter from console MIDI input
        juce::String paramId;
        if (paramIndex < 72)
            paramId = "fader_" + juce::String(paramIndex);
        else if (paramIndex < 144)
            paramId = "mute_" + juce::String(paramIndex - 72);
        else if (paramIndex < 154)
            paramId = "aux_mute_" + juce::String(paramIndex - 144);
        else if (paramIndex == 154)
            paramId = "joy1_x";
        else if (paramIndex == 155)
            paramId = "joy1_y";
        else if (paramIndex == 156)
            paramId = "joy1_mute";
        else if (paramIndex == 157)
            paramId = "joy2_x";
        else if (paramIndex == 158)
            paramId = "joy2_y";
        else if (paramIndex == 159)
            paramId = "joy2_mute";
        else if (paramIndex == 160)
            paramId = "master_fader";
        
        if (auto* param = apvts.getParameter(paramId))
            param->setValueNotifyingHost(value);
    };
}

CinemixBridgeProcessor::~CinemixBridgeProcessor()
{
    // Remove parameter listeners
    for (int i = 0; i < 72; ++i)
        apvts.removeParameterListener("fader_" + juce::String(i), this);
    
    for (int i = 0; i < 72; ++i)
        apvts.removeParameterListener("mute_" + juce::String(i), this);
    
    for (int i = 0; i < 10; ++i)
        apvts.removeParameterListener("aux_mute_" + juce::String(i), this);
    
    apvts.removeParameterListener("joy1_x", this);
    apvts.removeParameterListener("joy1_y", this);
    apvts.removeParameterListener("joy1_mute", this);
    apvts.removeParameterListener("joy2_x", this);
    apvts.removeParameterListener("joy2_y", this);
    apvts.removeParameterListener("joy2_mute", this);
    apvts.removeParameterListener("master_fader", this);
}

//==============================================================================
const juce::String CinemixBridgeProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CinemixBridgeProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool CinemixBridgeProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool CinemixBridgeProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double CinemixBridgeProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CinemixBridgeProcessor::getNumPrograms()
{
    return 1;
}

int CinemixBridgeProcessor::getCurrentProgram()
{
    return 0;
}

void CinemixBridgeProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String CinemixBridgeProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void CinemixBridgeProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void CinemixBridgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    
    // Calculate animation rates for test mode
    faderAnimRate = static_cast<int>(sampleRate / 25.0);  // 25 Hz
    muteAnimRate = static_cast<int>(sampleRate / 10.0);   // 10 Hz
}

void CinemixBridgeProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CinemixBridgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // Only stereo layouts are supported
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Input and output layouts must match
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void CinemixBridgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any output channels that didn't have input
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Test mode animation (if enabled)
    if (testModeEnabled)
    {
        sampleCounter++;
        if (sampleCounter >= faderAnimRate)
        {
            sampleCounter = 0;
            animateFaders();
        }
        
        if (sampleCounter % muteAnimRate == 0)
        {
            animateMutes();
        }
    }
    
    // For now, we're just passing audio through (or outputting silence)
    // MIDI handling will be added in Phase 3
    
    // Clear MIDI output for now
    midiMessages.clear();
}

//==============================================================================
bool CinemixBridgeProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* CinemixBridgeProcessor::createEditor()
{
    return new CinemixBridgeEditor (*this);
}

//==============================================================================
void CinemixBridgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Save parameter state
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void CinemixBridgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore parameter state
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// Parameter Layout Creation
//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout CinemixBridgeProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Faders: 0-71 (72 total)
    for (int i = 0; i < 72; ++i)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "fader_" + juce::String(i),
            getFaderParameterName(i),
            0.0f, 1.0f, 0.754f));  // Default at 0.754 (unity gain position)
    }
    
    // Mutes: 72-143 (72 total)
    for (int i = 0; i < 72; ++i)
    {
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "mute_" + juce::String(i),
            getMuteParameterName(i),
            false));
    }
    
    // AUX Mutes: 144-153 (10 total)
    for (int i = 0; i < 10; ++i)
    {
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "aux_mute_" + juce::String(i),
            "AUX " + juce::String(i + 1) + " Mute",
            false));
    }
    
    // Master Section: 154-160
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "joy1_x", "Joystick 1 X", 0.0f, 1.0f, 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "joy1_y", "Joystick 1 Y", 0.0f, 1.0f, 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "joy1_mute", "Joystick 1 Mute", false));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "joy2_x", "Joystick 2 X", 0.0f, 1.0f, 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "joy2_y", "Joystick 2 Y", 0.0f, 1.0f, 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "joy2_mute", "Joystick 2 Mute", false));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "master_fader", "Master Fader", 0.0f, 1.0f, 1.0f));  // Default at max
    
    return { params.begin(), params.end() };
}

juce::String CinemixBridgeProcessor::getFaderParameterName(int index)
{
    // Determine which channel (0-35) and which row (upper/lower)
    int channel = index / 2;
    bool isUpper = (index % 2 == 0);
    
    juce::String channelName;
    
    // Channels 24-27 are stereo channels (S1-S4)
    if (channel >= 24 && channel < 28)
    {
        channelName = "S" + juce::String(channel - 23);
    }
    else
    {
        // Mono channels - adjust numbering
        int monoNum = (channel < 24) ? (channel + 1) : (channel - 3);
        channelName = "M" + juce::String(monoNum);
    }
    
    return "Fader " + juce::String(isUpper ? "Upper " : "Lower ") + channelName;
}

juce::String CinemixBridgeProcessor::getMuteParameterName(int index)
{
    // Same logic as fader names
    int channel = index / 2;
    bool isUpper = (index % 2 == 0);
    
    juce::String channelName;
    
    if (channel >= 24 && channel < 28)
    {
        channelName = "S" + juce::String(channel - 23);
    }
    else
    {
        int monoNum = (channel < 24) ? (channel + 1) : (channel - 3);
        channelName = "M" + juce::String(monoNum);
    }
    
    return "Mute " + juce::String(isUpper ? "Upper " : "Lower ") + channelName;
}

//==============================================================================
// Console Control Methods (Stubs for Phase 1)
//==============================================================================

void CinemixBridgeProcessor::activateConsole()
{
    consoleActive = true;
    
    // Send full initialization sequence
    midiRouter.sendInitializationSequence();
    
    // Send current snapshot of all parameters
    sendSnapshot();
}

void CinemixBridgeProcessor::deactivateConsole()
{
    consoleActive = false;
    midiRouter.sendDeactivateCommand();
}

void CinemixBridgeProcessor::resetAll()
{
    // Set all faders to 0 (except master to 1.0)
    for (int i = FaderStart; i <= FaderEnd; ++i)
    {
        if (auto* param = apvts.getParameter("fader_" + juce::String(i)))
            param->setValueNotifyingHost(0.0f);
    }
    
    // Set master fader to max
    if (auto* param = apvts.getParameter("master_fader"))
        param->setValueNotifyingHost(1.0f);
    
    // Set all mutes to OFF
    for (int i = MuteStart; i <= MuteEnd; ++i)
    {
        if (auto* param = apvts.getParameter("mute_" + juce::String(i - MuteStart)))
            param->setValueNotifyingHost(0.0f);
    }
    
    // Set AUX mutes to OFF
    for (int i = AuxMuteStart; i <= AuxMuteEnd; ++i)
    {
        if (auto* param = apvts.getParameter("aux_mute_" + juce::String(i - AuxMuteStart)))
            param->setValueNotifyingHost(0.0f);
    }
    
    // Set joystick mutes to OFF
    if (auto* param = apvts.getParameter("joy1_mute"))
        param->setValueNotifyingHost(0.0f);
    if (auto* param = apvts.getParameter("joy2_mute"))
        param->setValueNotifyingHost(0.0f);
    
    allMutesState = false;
}

void CinemixBridgeProcessor::toggleAllMutes()
{
    allMutesState = !allMutesState;
    
    // Toggle all channel mutes
    for (int i = MuteStart; i <= MuteEnd; ++i)
    {
        if (auto* param = apvts.getParameter("mute_" + juce::String(i - MuteStart)))
            param->setValueNotifyingHost(allMutesState ? 1.0f : 0.0f);
    }
    
    // Toggle AUX mutes
    for (int i = AuxMuteStart; i <= AuxMuteEnd; ++i)
    {
        if (auto* param = apvts.getParameter("aux_mute_" + juce::String(i - AuxMuteStart)))
            param->setValueNotifyingHost(allMutesState ? 1.0f : 0.0f);
    }
    
    // Toggle joystick mutes
    if (auto* param = apvts.getParameter("joy1_mute"))
        param->setValueNotifyingHost(allMutesState ? 1.0f : 0.0f);
    if (auto* param = apvts.getParameter("joy2_mute"))
        param->setValueNotifyingHost(allMutesState ? 1.0f : 0.0f);
}

void CinemixBridgeProcessor::sendSnapshot()
{
    // Collect all current parameter values
    std::array<float, 161> paramValues;
    
    for (int i = 0; i < 161; ++i)
    {
        juce::String paramId;
        if (i < 72)
            paramId = "fader_" + juce::String(i);
        else if (i < 144)
            paramId = "mute_" + juce::String(i - 72);
        else if (i < 154)
            paramId = "aux_mute_" + juce::String(i - 144);
        else if (i == 154)
            paramId = "joy1_x";
        else if (i == 155)
            paramId = "joy1_y";
        else if (i == 156)
            paramId = "joy1_mute";
        else if (i == 157)
            paramId = "joy2_x";
        else if (i == 158)
            paramId = "joy2_y";
        else if (i == 159)
            paramId = "joy2_mute";
        else if (i == 160)
            paramId = "master_fader";
        
        if (auto* param = apvts.getRawParameterValue(paramId))
            paramValues[i] = param->load();
        else
            paramValues[i] = 0.0f;
    }
    
    // Send snapshot to console
    midiRouter.sendFullSnapshot(paramValues);
}

void CinemixBridgeProcessor::setTestMode(bool enable)
{
    testModeEnabled = enable;
    
    if (enable)
    {
        // Initialize animation phases with offset
        float phaseOffset = -2.0f;
        float phaseIncrement = 4.0f / 72.0f;
        for (int i = 0; i < 72; ++i)
        {
            animPhase[i] = phaseOffset + (phaseIncrement * i);
        }
        sampleCounter = 0;
    }
}

void CinemixBridgeProcessor::animateFaders()
{
    // Animate faders with sine wave pattern
    for (int i = 0; i < 72; ++i)
    {
        animPhase[i] += 0.05f;
        if (animPhase[i] > 1.0f)
            animPhase[i] -= 2.0f;
        
        // Calculate sine wave value (0.0 to 1.0)
        float ramp = animPhase[i];
        float value = (ramp * (1.0f - std::abs(ramp)) * 2.0f) + 0.5f;
        
        if (auto* param = apvts.getParameter("fader_" + juce::String(i)))
            param->setValueNotifyingHost(value);
    }
}

void CinemixBridgeProcessor::animateMutes()
{
    // Randomize mutes
    juce::Random random;
    
    for (int i = 0; i < 72; ++i)
    {
        bool muteState = random.nextFloat() > 0.5f;
        if (auto* param = apvts.getParameter("mute_" + juce::String(i)))
            param->setValueNotifyingHost(muteState ? 1.0f : 0.0f);
    }
}

void CinemixBridgeProcessor::syncParameterToMidi(int paramIndex, float value)
{
    // Only send if console is active
    if (consoleActive)
    {
        midiRouter.sendParameterUpdate(paramIndex, value);
    }
}

//==============================================================================
// Parameter Change Listener Implementation

void CinemixBridgeProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // Convert parameter ID to index and sync to MIDI
    int paramIndex = getParameterIndex(parameterID);
    if (paramIndex >= 0)
    {
        syncParameterToMidi(paramIndex, newValue);
    }
}

int CinemixBridgeProcessor::getParameterIndex(const juce::String& parameterID) const
{
    // Faders: "fader_0" to "fader_71"
    if (parameterID.startsWith("fader_"))
    {
        int index = parameterID.substring(6).getIntValue();
        if (index >= 0 && index < 72)
            return index;
    }
    
    // Mutes: "mute_0" to "mute_71"
    if (parameterID.startsWith("mute_"))
    {
        int index = parameterID.substring(5).getIntValue();
        if (index >= 0 && index < 72)
            return 72 + index;
    }
    
    // AUX Mutes: "aux_mute_0" to "aux_mute_9"
    if (parameterID.startsWith("aux_mute_"))
    {
        int index = parameterID.substring(9).getIntValue();
        if (index >= 0 && index < 10)
            return 144 + index;
    }
    
    // Master section
    if (parameterID == "joy1_x") return 154;
    if (parameterID == "joy1_y") return 155;
    if (parameterID == "joy1_mute") return 156;
    if (parameterID == "joy2_x") return 157;
    if (parameterID == "joy2_y") return 158;
    if (parameterID == "joy2_mute") return 159;
    if (parameterID == "master_fader") return 160;
    
    return -1;  // Invalid parameter ID
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CinemixBridgeProcessor();
}
