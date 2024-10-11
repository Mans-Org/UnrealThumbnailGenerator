// Copyright Mans Isaksson. All Rights Reserved.

#include "SThumbnailGeneratorEditor.h"
#include "ThumbnailGeneratorEditor.h"
#include "ThumbnailGenerator.h"
#include "ThumbnailGeneratorCompatibilityLayer.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#include "Modules/ModuleManager.h"
#include "DesktopPlatformModule.h"
#include "ImageWriteBlueprintLibrary.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Engine/Texture2D.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"
#include "EditorDirectories.h"

#define LOCTEXT_NAMESPACE "SThumbnailGeneratorEditor"

class SSaveThumbnailDialog : public SCompoundWidget
{
private:
	TWeakObjectPtr<UThumbnailOutputSettings> ThumbnailOutputSettings;
	TAttribute<TSharedPtr<SWindow>> ParentWindow;
	TAttribute<UTexture2D*> OutputTexture;
	TAttribute<TSubclassOf<AActor>> ActorClass;

public:

	SLATE_BEGIN_ARGS(SSaveThumbnailDialog)
		: _ParentWindow(nullptr)
	{}
	
	SLATE_ATTRIBUTE(TSharedPtr<SWindow>, ParentWindow)

	SLATE_ATTRIBUTE(UTexture2D*, OutputTexture)

	SLATE_ATTRIBUTE(TSubclassOf<AActor>, ActorClass)

	SLATE_END_ARGS()


	~SSaveThumbnailDialog()
	{
		if (ThumbnailOutputSettings.IsValid())
		{
			ThumbnailOutputSettings->RemoveFromRoot();
			ThumbnailOutputSettings->MarkAsGarbage();
		}
	}
public:

	void Construct(const FArguments& InArgs)
	{
		ParentWindow = InArgs._ParentWindow;
		OutputTexture = InArgs._OutputTexture;
		ActorClass = InArgs._ActorClass;

		// Update the default thumbnail name (so user can reset name)
		FString ThumbnailName = ActorClass.Get() ? ActorClass.Get()->GetName() : TEXT("Error");
		ThumbnailName.RemoveFromEnd("_C");

		GetMutableDefault<UThumbnailOutputSettings>()->ThumbnailName = ThumbnailName;

		ThumbnailOutputSettings = NewObject<UThumbnailOutputSettings>(GetTransientPackage());
		ThumbnailOutputSettings->AddToRoot();
		ThumbnailOutputSettings->ThumbnailName = ThumbnailName;

		FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bAllowSearch = false;

		TSharedPtr<IDetailsView> SettingDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
		SettingDetailsView->SetObject(ThumbnailOutputSettings.Get());
		
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.VAlign(EVerticalAlignment::VAlign_Fill)
			[
				SettingDetailsView.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(EVerticalAlignment::VAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(EHorizontalAlignment::HAlign_Left)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked(FOnClicked::CreateLambda([&]()
					{
						ParentWindow.Get()->RequestDestroyWindow();
						return FReply::Handled();
					}))
				]
				+ SHorizontalBox::Slot()
				.HAlign(EHorizontalAlignment::HAlign_Right)
				[
					SNew(SButton)
					.Text(LOCTEXT("Save", "Save"))
					.OnClicked(FOnClicked::CreateLambda([&]()
					{
						FThumbnailGeneratorEditorModule::SaveThumbnail(OutputTexture.Get(), ThumbnailOutputSettings->OutputPath.Path, ThumbnailOutputSettings->ThumbnailName, true);
						ParentWindow.Get()->RequestDestroyWindow();
						return FReply::Handled();
					}))
				]
			]
		];
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			ParentWindow.Get()->RequestDestroyWindow();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
};

void SThumbnailGeneratorEditor::Construct(const FArguments& Args)
{
	CommandList = MakeShareable(new FUICommandList);

	BindCommands();

	ThumbnailGeneratorEditorSettings = NewObject<UThumbnailGeneratorEditorSettings>();
	ThumbnailGeneratorEditorSettings->AddToRoot();

	UThumbnailGeneratorSettings* ThumbnailGeneratorSettings = UThumbnailGeneratorSettings::Get();

	const auto DefaultSettings = FThumbnailSettings::MergeThumbnailSettings(ThumbnailGeneratorEditorSettings->ThumbnailSettings, ThumbnailGeneratorSettings->DefaultThumbnailSettings);

	PreviewTexture = UTexture2D::CreateTransient(
		DefaultSettings.ThumbnailTextureWidth, 
		DefaultSettings.ThumbnailTextureHeight, 
		DefaultSettings.ThumbnailBitDepth == EThumbnailBitDepth::E8 ? EPixelFormat::PF_B8G8R8A8 : EPixelFormat::PF_FloatRGBA
	);
	PreviewTexture->NeverStream = true;
	PreviewTexture->VirtualTextureStreaming = false;
	PreviewTexture->SRGB = true;
	PreviewTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
	PreviewTexture->LODGroup = TextureGroup::TEXTUREGROUP_UI;
	PreviewTexture->AddToRoot();

	ImageBrush.SetResourceObject(PreviewTexture.Get());

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	SettingDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	SettingDetailsView->SetObject(ThumbnailGeneratorEditorSettings.Get());

	FToolBarBuilder ToolBarBuilder(CommandList, FMultiBoxCustomization::None);

	ToolBarBuilder.BeginSection(TEXT("GenerateThumbnail"));
	{
		ToolBarBuilder.AddToolBarButton(FThumbnailGeneratorEditorCommands::Get().GenerateThumbnail,
			NAME_None,
			LOCTEXT("Generate", "Generate"),
			LOCTEXT("GenerateToolTip", "Re-generate the thumbnail using the current settings"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Recompile")
		);
	}
	ToolBarBuilder.EndSection();

	ToolBarBuilder.BeginSection(TEXT("SaveThumbnail"));
	{
		ToolBarBuilder.AddToolBarButton(FThumbnailGeneratorEditorCommands::Get().SaveThumbnail,
			NAME_None,
			LOCTEXT("Save", "Save"),
			LOCTEXT("SaveThumbnailTooltip", "Save the currently displayed thumbnail"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.SaveAsset")
		);

		ToolBarBuilder.AddToolBarButton(FThumbnailGeneratorEditorCommands::Get().ExportThumbnail,
			NAME_None,
			LOCTEXT("Export", "Export"),
			LOCTEXT("ExportThumbnailTooltip", "Export the currently displayed thumbnail as a PNG"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.SaveAssetAs")
		);
	}
	ToolBarBuilder.EndSection();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0.f, 5.f, 0.f, 0.f)
		.AutoHeight()
		[
			ToolBarBuilder.MakeWidget()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(0.f, 5.f, 0.f, 0.f)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
			+ SSplitter::Slot()
			.SizeRule(SSplitter::ESizeRule::FractionOfParent)
			.Value(0.25)
			[
				SettingDetailsView.ToSharedRef()
			]
			+ SSplitter::Slot()
			.SizeRule(SSplitter::ESizeRule::FractionOfParent)
			.Value(0.75)
			[
				SNew(SBorder)
				.Padding(15.f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SScaleBox)
					.Stretch(EStretch::ScaleToFit)
					[
						SNew(SImage)
						.Image(&ImageBrush)
					]
				]
			]
		]
	];
}

SThumbnailGeneratorEditor::~SThumbnailGeneratorEditor()
{
	if (PreviewTexture.IsValid())
	{
		PreviewTexture->RemoveFromRoot();
		PreviewTexture->MarkAsGarbage();
	}

	if (ThumbnailGeneratorEditorSettings.IsValid())
	{
		ThumbnailGeneratorEditorSettings->RemoveFromRoot();
		ThumbnailGeneratorEditorSettings->MarkAsGarbage();
	}
}

void SThumbnailGeneratorEditor::GenerateThumbnail()
{
	if (!ThumbnailGeneratorEditorSettings->ActorClass.Get())
	{
		const FText MsgTitle = LOCTEXT("Error", "Error");
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidActorClass", "Invalid Actor Class"), MsgTitle);
		return;
	}

	ThumbnailGenerator.GenerateActorThumbnail(
		ThumbnailGeneratorEditorSettings->ActorClass, 
		FThumbnailSettings::MergeThumbnailSettings(UThumbnailGeneratorSettings::Get()->DefaultThumbnailSettings, ThumbnailGeneratorEditorSettings->ThumbnailSettings), 
		PreviewTexture.Get()
	);
}

void SThumbnailGeneratorEditor::SaveThumbnail()
{
	if (!PreviewTexture.IsValid())
	{
		const FText MsgTitle = LOCTEXT("Error", "Error");
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidTexture", "Invalid Texture"), MsgTitle);
		return;
	}

	TSharedRef<SWindow> SaveThumbnailWindow = SNew(SWindow)
		.Title(LOCTEXT("SaveThumbnail", "Save Thumbnail"))
		.ClientSize(FVector2D(400, 150));

	TSharedRef<SSaveThumbnailDialog> SaveDialog =
		SNew(SSaveThumbnailDialog)
		.ParentWindow(SaveThumbnailWindow)
		.OutputTexture(PreviewTexture.Get())
		.ActorClass(ThumbnailGeneratorEditorSettings->ActorClass);

	SaveThumbnailWindow->SetContent(SaveDialog);

	// Show the dialog window as a modal window
	GEditor->EditorAddModalWindow(SaveThumbnailWindow);
}

void SThumbnailGeneratorEditor::ExportThumbnail()
{
	if (!PreviewTexture.IsValid())
	{
		const FText MsgTitle = LOCTEXT("Error", "Error");
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InvalidTexture", "Invalid Texture"), MsgTitle);
		return;
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		return;
	}

	static FText BrowseFileTitle = LOCTEXT("ExportPath", "Export Path");
	FString ThumbnailName = ThumbnailGeneratorEditorSettings->ActorClass.Get() ? ThumbnailGeneratorEditorSettings->ActorClass->GetName() : TEXT("Error");
	ThumbnailName.RemoveFromEnd("_C");

	const FString ExportPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_EXPORT);

	const FString FileTypeFilter = [&]() 
	{
		switch (PreviewTexture->GetPixelFormat())
		{
		case PF_FloatRGBA:
		case PF_A32B32G32R32F:
			return "EXR|*.exr";

		case PF_R8G8B8A8:
		case PF_B8G8R8A8:
			return "PNG|*.png";
		}
		return "";
	}();

	// show the file browse dialog
	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	void* ParentWindowHandle = (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid())
		? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	TArray<FString> OutFiles;
	if (DesktopPlatform->SaveFileDialog(ParentWindowHandle, BrowseFileTitle.ToString(), ExportPath, ThumbnailName, FileTypeFilter, EFileDialogFlags::None, OutFiles))
	{
		for (const FString &FilePath : OutFiles)
		{
			FImageWriteOptions ImageWriteOptions;
			ImageWriteOptions.bAsync = false;
			ImageWriteOptions.bOverwriteFile = true;
			ImageWriteOptions.Format = [&]() 
			{
				switch (PreviewTexture->GetPixelFormat())
				{
				case PF_FloatRGBA:
				case PF_A32B32G32R32F:
					return EDesiredImageFormat::EXR;

				case PF_R8G8B8A8:
				case PF_B8G8R8A8:
					return EDesiredImageFormat::PNG;
				}
				return EDesiredImageFormat::PNG;
			}();

			UImageWriteBlueprintLibrary::ExportToDisk(PreviewTexture.Get(), FilePath, ImageWriteOptions);
			return;
		}
	}
}

void SThumbnailGeneratorEditor::ResetThumbnailGeneratorEditorSettings()
{
	if (ThumbnailGeneratorEditorSettings.IsValid())
	{
		ThumbnailGeneratorEditorSettings->RemoveFromRoot();
		ThumbnailGeneratorEditorSettings->MarkAsGarbage();
	}

	ThumbnailGeneratorEditorSettings = NewObject<UThumbnailGeneratorEditorSettings>();
	ThumbnailGeneratorEditorSettings->AddToRoot();

	SettingDetailsView->SetObject(ThumbnailGeneratorEditorSettings.Get());
}

void SThumbnailGeneratorEditor::BindCommands()
{
	CommandList->MapAction(FThumbnailGeneratorEditorCommands::Get().GenerateThumbnail,
		FExecuteAction::CreateSP(this, &SThumbnailGeneratorEditor::GenerateThumbnail)
	);

	CommandList->MapAction(FThumbnailGeneratorEditorCommands::Get().SaveThumbnail,
		FExecuteAction::CreateSP(this, &SThumbnailGeneratorEditor::SaveThumbnail)
	);

	CommandList->MapAction(FThumbnailGeneratorEditorCommands::Get().ExportThumbnail,
		FExecuteAction::CreateSP(this, &SThumbnailGeneratorEditor::ExportThumbnail)
	);
}

#undef LOCTEXT_NAMESPACE