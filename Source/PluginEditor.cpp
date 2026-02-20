/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Helper local: configura un slider rotatorio y su label asociado.
static void setupRotarySlider (juce::Slider& slider, juce::Label& label,
                               const juce::String& labelText,
                               juce::Component* parent)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
    parent->addAndMakeVisible (slider);

    label.setText (labelText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.attachToComponent (&slider, false);
    parent->addAndMakeVisible (label);
}

//==============================================================================
SilentRoomAudioProcessorEditor::SilentRoomAudioProcessorEditor (SilentRoomAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // --- 1. Configurar Sliders y Labels ---
    setupRotarySlider (thresholdSlider, thresholdLabel, "Threshold", this);
    setupRotarySlider (ratioSlider,     ratioLabel,     "Ratio",     this);
    setupRotarySlider (attackSlider,    attackLabel,     "Attack",    this);
    setupRotarySlider (releaseSlider,   releaseLabel,    "Release",   this);

    // --- 2. Sufijos de unidad ---
    thresholdSlider.setTextValueSuffix (" dB");
    ratioSlider.setTextValueSuffix     (":1");
    attackSlider.setTextValueSuffix    (" ms");
    releaseSlider.setTextValueSuffix   (" ms");

    // --- 3. APVTS Attachments (DESPUÉS de configurar los sliders) ---
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "THRESHOLD", thresholdSlider);
    ratioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "RATIO", ratioSlider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "ATTACK", attackSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "RELEASE", releaseSlider);

    // --- 4. Timer a 60 FPS para el medidor de GR ---
    startTimerHz (60);

    // --- 5. Tamaño de ventana ---
    setSize (500, 400);
}

SilentRoomAudioProcessorEditor::~SilentRoomAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SilentRoomAudioProcessorEditor::timerCallback()
{
    // Lectura atómica desde el procesador
    const float newGR = audioProcessor.gainReduction.load (std::memory_order_relaxed);

    // Suavizado exponencial (80% valor anterior, 20% valor nuevo)
    currentGR = currentGR * 0.8f + newGR * 0.2f;

    repaint();
}

//==============================================================================
void SilentRoomAudioProcessorEditor::paint (juce::Graphics& g)
{
    // --- Fondo oscuro ---
    g.fillAll (juce::Colour (0xff1a1a2e));

    // --- Título ---
    g.setColour (juce::Colour (0xffe0e0ff));
    g.setFont (juce::FontOptions (20.0f));
    g.drawText ("SilentRoom – Noise Gate", getLocalBounds().removeFromTop (30),
                juce::Justification::centred, true);

    // --- Medidor de Gain Reduction ---
    // Área del medidor: franja inferior
    const int meterHeight = 30;
    const int meterMargin = 20;
    auto meterArea = getLocalBounds().removeFromBottom (meterHeight + meterMargin)
                         .reduced (meterMargin, 0)
                         .removeFromTop (meterHeight);

    // Fondo del medidor
    g.setColour (juce::Colour (0xff0d0d1a));
    g.fillRoundedRectangle (meterArea.toFloat(), 4.0f);

    // Borde
    g.setColour (juce::Colour (0xff3a3a5c));
    g.drawRoundedRectangle (meterArea.toFloat(), 4.0f, 1.0f);

    // Barra de GR: currentGR va de 0 (sin reducción) a -60 (máxima reducción)
    // Normalizamos a 0..1 donde 0 = sin reducción, 1 = -60 dB de reducción
    const float grNorm = juce::jlimit (0.0f, 1.0f, -currentGR / 60.0f);

    if (grNorm > 0.001f)
    {
        auto barArea = meterArea.toFloat().reduced (2.0f);
        barArea.setWidth (barArea.getWidth() * grNorm);

        // Gradiente de verde a rojo según la intensidad
        auto barColour = juce::Colour::fromHSV (
            0.33f * (1.0f - grNorm),  // Hue: verde(0.33) → rojo(0.0)
            0.8f,                      // Saturación
            0.9f,                      // Brillo
            1.0f                       // Alpha
        );

        g.setColour (barColour);
        g.fillRoundedRectangle (barArea, 3.0f);
    }

    // Texto del medidor
    g.setColour (juce::Colour (0xffccccee));
    g.setFont (juce::FontOptions (12.0f));

    juce::String grText = "GR: " + juce::String (currentGR, 1) + " dB";
    g.drawText (grText, meterArea, juce::Justification::centred, true);
}

//==============================================================================
void SilentRoomAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Reservar espacio para el título
    bounds.removeFromTop (40);

    // Reservar espacio para el medidor de GR
    bounds.removeFromBottom (60);

    // Área central para los sliders (4 en fila)
    // Dejar margen para los labels (que están encima de los sliders)
    bounds.removeFromTop (20); // espacio para labels

    const int sliderWidth = bounds.getWidth() / 4;

    thresholdSlider.setBounds (bounds.removeFromLeft (sliderWidth));
    ratioSlider.setBounds     (bounds.removeFromLeft (sliderWidth));
    attackSlider.setBounds    (bounds.removeFromLeft (sliderWidth));
    releaseSlider.setBounds   (bounds);
}
