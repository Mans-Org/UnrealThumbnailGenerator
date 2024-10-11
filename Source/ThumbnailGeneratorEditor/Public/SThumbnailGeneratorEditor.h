// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailGenerator/Public/ThumbnailGenerator.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Engine/EngineBaseTypes.h"
#include "SThumbnailGeneratorEditor.generated.h"

class SThumbnailGeneratorEditor : public SCompoundWidget
{
private:
	FSlateBrush ImageBrush;
	
	TSharedPtr<class IDetailsView> SettingDetailsView;
	
	TSharedPtr<FUICommandList> CommandList;

	FThumbnailGenerator ThumbnailGenerator;

public:
	TWeakObjectPtr<class UTexture2D> PreviewTexture;
	TWeakObjectPtr<class UThumbnailGeneratorEditorSettings> ThumbnailGeneratorEditorSettings;

public:
	SLATE_BEGIN_ARGS(SThumbnailGeneratorEditor)
	{
	}

	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	~SThumbnailGeneratorEditor();

	void GenerateThumbnail();

	void SaveThumbnail();

	void ExportThumbnail();

	void ResetThumbnailGeneratorEditorSettings();

	void BindCommands();
};

UCLASS()
class UThumbnailGeneratorEditorSettings : public UObject
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Category = "Thumbnail Settings")
	TSubclassOf<AActor> ActorClass;

	UPROPERTY(EditAnywhere, Category = "Thumbnail Settings")
	FThumbnailSettings ThumbnailSettings;

};

UCLASS()
class UThumbnailOutputSettings : public UObject
{
	GENERATED_BODY()
public:

	// Where to place the generated thumbnail
	UPROPERTY(EditAnywhere, Category = "Thumbnail Output Settings", meta=(LongPackageName))
	FDirectoryPath OutputPath = FDirectoryPath{ TEXT("/Game/ThumbnailGenerator") };

	// The name of the generated thumbnail
	UPROPERTY(EditAnywhere, Category = "Thumbnail Output Settings")
	FString ThumbnailName;
};