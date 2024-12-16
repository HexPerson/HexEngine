
#pragma once

#include <qdialog.h>

class RendererSettings : public QDialog
{
public:
	RendererSettings(QWidget* parent);

public slots:
	void ShallowColourChanged(QColor colour);
	void DeepColourChanged(QColor colour);
	void FresnelPowerChanged(int value);
	void ShoreFadeStrengthChanged(int value);
	void FadeFactorChanged(int value);
	void ReflectionStrengthChanged(int value);
};
