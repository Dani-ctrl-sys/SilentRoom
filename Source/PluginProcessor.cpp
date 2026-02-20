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
                     #if ! JucePlugin_IsMidiEffect
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    // --- FASE 2.3: CACHE DE PUNTEROS ---
    // Conectamos los punteros locales a la memoria interna del APVTS.
    // Esto es O(1) aquí, para que sea O(0) en el processBlock.

    thresholdParam = apvts.getRawParameterValue("THRESHOLD");
    ratioParam     = apvts.getRawParameterValue("RATIO");
    attackParam    = apvts.getRawParameterValue("ATTACK");
    releaseParam   = apvts.getRawParameterValue("RELEASE");

    // SAFETY CHECK:
    jassert(thresholdParam != nullptr);
    jassert(ratioParam != nullptr);
    jassert(attackParam != nullptr);
    jassert(releaseParam != nullptr);
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
    // Reiniciar estado del seguidor de envolvente al preparar la reproducción
    envelope = 0.0f;
    gainReduction.store (0.0f, std::memory_order_relaxed);
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
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Limpiar canales extra (rutina estándar)
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // --- LECTURA ATÓMICA DE PARÁMETROS (LOCK-FREE) ---
    const float threshold = thresholdParam->load (std::memory_order_relaxed);  // dB
    const float ratio     = ratioParam->load     (std::memory_order_relaxed);  // N:1
    const float attackMs  = attackParam->load     (std::memory_order_relaxed);  // ms
    const float releaseMs = releaseParam->load    (std::memory_order_relaxed);  // ms

    // --- COEFICIENTES DE BALÍSTICA (fuera del bucle de muestras) ---
    const double sr = getSampleRate();
    const float alphaAttack  = std::exp (-1.0f / static_cast<float>(attackMs  * 0.001 * sr));
    const float alphaRelease = std::exp (-1.0f / static_cast<float>(releaseMs * 0.001 * sr));

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = totalNumInputChannels;

    // Valor máximo de GR en este bloque (para el medidor de la GUI)
    float maxGR = 0.0f;

    // --- BUCLE DE MUESTRAS ---
    for (int sample = 0; sample < numSamples; ++sample)
    {
        // 1. Detección de nivel: Peak estéreo enlazado (máximo de ambos canales)
        float peakLevel = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float absSample = std::fabs (buffer.getReadPointer (ch)[sample]);
            if (absSample > peakLevel)
                peakLevel = absSample;
        }

        // 2. Convertir nivel peak a dB
        const float levelDb = juce::Decibels::gainToDecibels (peakLevel, -100.0f);

        // 3. Gain Computer: calcular la reducción de ganancia objetivo (en dB, <= 0)
        float targetGR = 0.0f; // sin atenuación por defecto

        if (levelDb < threshold)
        {
            // Cantidad que estamos por debajo del threshold
            const float belowThreshold = threshold - levelDb;  // positivo
            // GR = belowThreshold * (1 - 1/ratio)  →  negativo (atenuación)
            targetGR = -belowThreshold * (1.0f - (1.0f / ratio));
        }

        // 4. Balística Attack/Release con filtro de un polo
        const float alpha = (targetGR < envelope) ? alphaAttack : alphaRelease;
        envelope = targetGR + alpha * (envelope - targetGR);

        // 5. Convertir GR suavizada de dB a factor lineal
        const float gainLinear = juce::Decibels::decibelsToGain (envelope, -100.0f);

        // 6. Aplicar ganancia a todos los canales
        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.getWritePointer (ch)[sample] *= gainLinear;
        }

        // 7. Tracking del máximo GR para el medidor GUI
        if (envelope < maxGR)
            maxGR = envelope;
    }

    // Publicar la reducción de ganancia máxima del bloque (valor negativo en dB)
    gainReduction.store (maxGR, std::memory_order_relaxed);
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
