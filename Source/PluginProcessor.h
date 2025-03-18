/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <array>
#include <cstdint>
#include <JuceHeader.h>

//==============================================================================
/**
*/
class CinemixAutomationBridgeAudioProcessor  : public juce::AudioProcessor
{
public:
	//==============================================================================
	CinemixAutomationBridgeAudioProcessor();
	~CinemixAutomationBridgeAudioProcessor() override;

	//==============================================================================
	void prepareToPlay (double sampleRate, int samplesPerBlock) override;
	void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
	bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

	void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

	//==============================================================================
	juce::AudioProcessorEditor* createEditor() override;
	bool hasEditor() const override;

	//==============================================================================
	const juce::String getName() const override;

	bool acceptsMidi() const override;
	bool producesMidi() const override;
	bool isMidiEffect() const override;
	double getTailLengthSeconds() const override;

	//==============================================================================
	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram (int index) override;
	const juce::String getProgramName (int index) override;
	void changeProgramName (int index, const juce::String& newName) override;

	//==============================================================================
	void getStateInformation (juce::MemoryBlock& destData) override;
	void setStateInformation (const void* data, int sizeInBytes) override;

	void testModeToggle();

private:
	//==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CinemixAutomationBridgeAudioProcessor)

	bool _testMode;
	bool _pedalDown;
	juce::Array<int> _paramControllers;
	juce::Array<int> _faderControllers;
	juce::Array<int> _faderModeLeft;
	juce::Array<int> _faderModeRight;
	int _fadeModeMaster;
	juce::Array<int> _midiChannel;
	juce::Array<int> _prevCCVal;
	int _faderSpeed;
	int _muteSpeed;
	std::array<int, 2> _countSamples;
	juce::Array<float> _animRamp;


	static void sendMidiEvent(juce::MidiBuffer &midiMsgs, std::array<uint8_t> &bytes)
	{
		midiMsgs.addEvent(juce::MidiMessage(bytes[0], bytes[1], bytes[2]));
	}


	static void sendMidiCC(juce::MidiBuffer &midiMsgs, uint8_t channel, uint8_t ccNum,
		uint8_t value)
	{
		sendMidiEvent({127 + channel, ccNum, value});
	}
};
