// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "ThumbnailSettingsCustomization.h"
#include "ThumbnailGeneratorSettings.h"

#include "IDetailsView.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#include "Widgets/Text/STextBlock.h"
#include "SNameComboBox.h"

#define LOCTEXT_NAMESPACE "ThumbnailSettingsCustomization"

void FThumbnailSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& layoutBuilder)
{
	static TArray<TSharedPtr<FName>> Options = []() 
	{
		TArray<TSharedPtr<FName>> OutArray;
		for (const FName &PresetName : UThumbnailGeneratorSettings::GetPresetList())
			OutArray.Add(MakeShareable(new FName(PresetName)));

		return OutArray;
	}();

	const static FText ApplyPresetText = LOCTEXT("apply_preset", "Apply Preset");

	layoutBuilder.EditCategory("Thumbnail Generator")
		.AddCustomRow(ApplyPresetText)
		.NameContent()
		[
			SNew(STextBlock).Text(ApplyPresetText)
		]
		.ValueContent()
		.HAlign(EHorizontalAlignment::HAlign_Left)
		[
			SAssignNew(ComboBox, SNameComboBox)
			.OptionsSource(&Options)
			.InitiallySelectedItem(nullptr)
			.OnSelectionChanged_Lambda([&](TSharedPtr<FName> NewValue, ESelectInfo::Type) {
				if (NewValue.IsValid())
				{
					UThumbnailGeneratorSettings::ApplyPreset(*NewValue);
					ComboBox->SetSelectedItem(nullptr);
				}
			})
			.OnGetNameLabelForItem_Lambda([](TSharedPtr<FName> InValue)->FString {
				return InValue.IsValid() ? InValue->ToString() : TEXT("Error");
			})
		];
}

TSharedRef<IDetailCustomization> FThumbnailSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FThumbnailSettingsCustomization);
}

#undef LOCTEXT_NAMESPACE