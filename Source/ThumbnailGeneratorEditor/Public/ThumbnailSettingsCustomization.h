// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

class THUMBNAILGENERATOREDITOR_API FThumbnailSettingsCustomization : public IDetailCustomization
{
private:
	TSharedPtr<class SNameComboBox> ComboBox;

public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& layoutBuilder) override;

	static TSharedRef<IDetailCustomization> MakeInstance();
};
