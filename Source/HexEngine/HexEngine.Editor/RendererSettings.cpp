

#include "RendererSettings.hpp"
#include "ui_RendererSettings.h"
#include "Widgets\ColourPicker.hpp"

#include <HexEngine.Core\HexEngine.hpp>

RendererSettings::RendererSettings(QWidget* parent) :
    QDialog(parent)
{
    Ui_rendererSettings ui;
    ui.setupUi(this);

    // shallow colour
    auto shallowColourPicker = new ColourPicker(ui.formLayout->widget(), QColor(255, 0, 0));
    ui.formLayout->replaceWidget(ui.shallowColour, shallowColourPicker);
    ui.shallowColour = shallowColourPicker;
    connect((ColourPicker*)ui.shallowColour, &ColourPicker::ColourChanged, this, &RendererSettings::ShallowColourChanged);

    // deep colour
    auto deepColourPicker = new ColourPicker(ui.formLayout->widget(), QColor(255, 0, 0));
    ui.formLayout->replaceWidget(ui.deepColour, deepColourPicker);
    ui.deepColour = deepColourPicker;
    connect((ColourPicker*)ui.deepColour, &ColourPicker::ColourChanged, this, &RendererSettings::DeepColourChanged);

    // fresnel
    ui.fresnelPower->setValue((int)(HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean().fresnelPow * 1000.0f));
    connect(ui.fresnelPower, &QSlider::valueChanged, this, &RendererSettings::FresnelPowerChanged);

    // shore fade
    ui.shoreFadeStrength->setValue((int)(HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean().shoreFadeStrength * 100.0f));
    connect(ui.shoreFadeStrength, &QSlider::valueChanged, this, &RendererSettings::ShoreFadeStrengthChanged);

    // fade factor
    ui.fadeFactor->setValue((int)(HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean().fadeFactor * 1000.0f));
    connect(ui.fadeFactor, &QSlider::valueChanged, this, &RendererSettings::FadeFactorChanged);

    // reflection strength
    ui.reflectionStrength->setValue((int)(HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean().reflectionStrength * 1000.0f));
    connect(ui.reflectionStrength, &QSlider::valueChanged, this, &RendererSettings::ReflectionStrengthChanged);
}

void RendererSettings::ShallowColourChanged(QColor colour)
{
    auto& ocean = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean();

    ocean.shallowColour.x = colour.redF();
    ocean.shallowColour.y = colour.greenF();
    ocean.shallowColour.z = colour.blueF();
    ocean.shallowColour.w = colour.alphaF();
}

void RendererSettings::DeepColourChanged(QColor colour)
{
    auto& ocean = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean();

    ocean.deepColour.x = colour.redF();
    ocean.deepColour.y = colour.greenF();
    ocean.deepColour.z = colour.blueF();
    ocean.deepColour.w = colour.alphaF();
}

void RendererSettings::FresnelPowerChanged(int value)
{
    HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean().fresnelPow = (float)value / 1000.0f;
}

void RendererSettings::ShoreFadeStrengthChanged(int value)
{
    HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean().shoreFadeStrength = (float)value / 100.0f;
}

void RendererSettings::FadeFactorChanged(int value)
{
    HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean().fadeFactor = (float)value / 1000.0f;
}

void RendererSettings::ReflectionStrengthChanged(int value)
{
    HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean().reflectionStrength = (float)value / 1000.0f;
}