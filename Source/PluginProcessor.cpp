/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SilentRoomAudioProcessor::SilentRoomAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       ),
       // AQUI INICIALIZAMOS EL APVTS:
       apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    // El cuerpo del constructor puede estar vacío por ahora.
}

SilentRoomAudioProcessor::~SilentRoomAudioProcessor()
{
}

//==============================================================================
const juce::String SilentRoomAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SilentRoomAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SilentRoomAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SilentRoomAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SilentRoomAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SilentRoomAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SilentRoomAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SilentRoomAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SilentRoomAudioProcessor::getProgramName (int index)
{
    return {};
}

void SilentRoomAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SilentRoomAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
}

void SilentRoomAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SilentRoomAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

juce::AudioProcessorValueTreeState::ParameterLayout SilentRoomAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- 1. THRESHOLD (Umbral) ---
    // Rango: -60dB a 0dB.
    // Default: -60dB (puerta abierta/inactiva al inicio para no asustar).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "THRESHOLD",      // Parameter ID (Interno)
        "Threshold",      // Parameter Name (Visible)
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), // Range & Step
        -60.0f            // Default Value
    ));

    // --- 2. RATIO (Proporción) ---
    // Rango: 1:1 (nada) a 50:1 (casi silencio total).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "RATIO",
        "Ratio",
        juce::NormalisableRange<float>(1.0f, 50.0f, 0.1f),
        1.0f
    ));

    // --- 3. ATTACK (Ataque) ---
    // Rango: 1ms a 100ms.
    // Skew Factor: 0.5 (nos da más precisión en valores bajos, ej: 1-10ms).
    auto attackRange = juce::NormalisableRange<float>(1.0f, 100.0f, 0.1f);
    attackRange.setSkewForCentre(20.0f); // 20ms en el centro del slider

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "ATTACK",
        "Attack",
        attackRange,
        10.0f // Default 10ms
    ));

    // --- 4. RELEASE (Relajación) ---
    // Rango: 10ms a 2000ms (2s).
    // Skew Factor: Priorizamos tiempos cortos/medios.
    auto releaseRange = juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f);
    releaseRange.setSkewForCentre(200.0f);

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "RELEASE",
        "Release",
        releaseRange,
        100.0f // Default 100ms
    ));

    return layout;
}

void SilentRoomAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // 1. PROTECCIÓN FPU (Floating Point Unit)
    // Evita que números extremadamente pequeños (denormals) consuman CPU innecesaria.
    // Esto es estándar en la industria.
    juce::ScopedNoDenormals noDenormals;

    // 2. DATOS DEL DAW
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // 3. LIMPIEZA DE CANALES BASURA (La lógica que discutimos)
    // Si Entradas < Salidas (ej: Mono -> Stereo), limpiamos el exceso.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // 4. BUCLE PRINCIPAL DE PROCESAMIENTO
    // Iteramos SOLO sobre los canales que tienen audio válido.
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        // Obtenemos el puntero de escritura directo a la RAM de audio.
        auto* channelData = buffer.getWritePointer (channel);

        // --- AQUÍ IRÁ EL DSP (Fase 3) ---
        // Por ahora, el plugin actúa como un cable transparente (Pass-Through).
        // El audio entra y sale sin cambios.
        
        /* NOTA: Para probar que controlas el audio, puedes descomentar 
           la siguiente línea temporalmente para silenciar todo:
           
           juce::FloatVectorOperations::clear(channelData, buffer.getNumSamples());
        */
    }
}

//==============================================================================
bool SilentRoomAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SilentRoomAudioProcessor::createEditor()
{
    return new SilentRoomAudioProcessorEditor (*this);
}

//==============================================================================
void SilentRoomAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void SilentRoomAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SilentRoomAudioProcessor();
}
