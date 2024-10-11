// Copyright Mans Isaksson. All Rights Reserved.

#include "ThumbnailGeneratorEditor.h"
#include "ThumbnailGeneratorSettings.h"
#include "ThumbnailGeneratorModule.h"
#include "ThumbnailGenerator.h"
#include "SThumbnailGeneratorEditor.h"
#include "ThumbnailGeneratorCompatibilityLayer.h"
#include "ISettingsModule.h"
#include "ISettingsContainer.h"
#include "ISettingsSection.h"
#include "ObjectTools.h"
#if ENGINE_VERSION_HIGHER_THAN(5, 0)
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Texture2D.h"
#include "Engine/Selection.h"

#include "Misc/MessageDialog.h"
#include "Brushes/SlateImageBrush.h"
#include "PropertyEditorModule.h"
#include "ThumbnailSettingsCustomization.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "LevelEditor.h"
#include "ContentBrowserDelegates.h"
#include "ContentBrowserModule.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

DEFINE_LOG_CATEGORY(LogThumbnailGeneratorEd);

#define LOCTEXT_NAMESPACE "ThumbnailGeneratorModule"

namespace ThumbnailGeneratorEdHelpers
{
	bool ContainsActorClass(const TArray<FAssetData>& AssetData)
	{
		for (const auto& Asset : AssetData)
		{
			// Load the Native Class to avoid unnecessary loading blueprints
			const auto NativeParentClassPath = Asset.TagsAndValues.FindTag("NativeParentClass");
			if (NativeParentClassPath.IsSet())
			{
				return FSoftClassPath(NativeParentClassPath.GetValue()).TryLoadClass<AActor>() != nullptr;
			}
		}

		return false;
	}

	TSubclassOf<AActor> LoadFirstValidActorClass(const TArray<FAssetData> &AssetData)
	{
		for (const auto& Asset : AssetData)
		{
			// Load the Native Class to avoid unnecessary loading blueprints
			const auto NativeParentClassPath = Asset.TagsAndValues.FindTag("NativeParentClass");
			if (NativeParentClassPath.IsSet())
			{
				if (UClass* ActorClass = FSoftClassPath(NativeParentClassPath.GetValue()).TryLoadClass<AActor>())
				{
					UBlueprint* BlueprintAsset = Cast<UBlueprint>(Asset.ToSoftObjectPath().TryLoad());
					if (BlueprintAsset && BlueprintAsset->GeneratedClass)
					{
						return (TSubclassOf<AActor>)BlueprintAsset->GeneratedClass;
					}
				}
			}
		}

		return nullptr;
	}

};

FThumbnailGeneratorEditorCommands::FThumbnailGeneratorEditorCommands() 
	: TCommands<FThumbnailGeneratorEditorCommands>("ThumbnailGeneratorEditorCommands", LOCTEXT("ThumbnailGeneratorEditor", "Thumbnail Generator"), NAME_None, FAppStyle::GetAppStyleSetName())
{
}

void FThumbnailGeneratorEditorCommands::RegisterCommands()
{
	UI_COMMAND(GenerateThumbnail, "Generate Thumbnail...", "Launches the Thumbnail Generator using the selected asset", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SaveThumbnail, "Save Thumbnail", "Saves the Generated Thumbnail", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ExportThumbnail, "Export Thumbnail", "Saves the Generated Thumbnail as a PNG", EUserInterfaceActionType::Button, FInputChord());
}

static const FName ThumbnailGenEditorTabName(TEXT("ThumbnailGeneratorEditor"));

void FThumbnailGeneratorEditorModule::StartupModule()
{
	FThumbnailGeneratorEditorCommands::Register();

	RegisterSettings();
	RegisterDetailsCustomization();
	RegisterThumbnailGeneratorEvents();
	RegisterTabSpawner();
	RegisterLevelEditorCommandExtensions();
	RegisterContentBrowserCommandExtensions();
}

void FThumbnailGeneratorEditorModule::ShutdownModule()
{
	UnregisterSettings();
	UnregisterDetailsCustomization();
	UnregisterThumbnailGeneratorEvents();
	UnregisterTabSpawner();
	UnregisterLevelEditorCommandExtensions();
	UnregisterContentBrowserCommandExtensions();

	FThumbnailGeneratorEditorCommands::Unregister();
}

void FThumbnailGeneratorEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		const ISettingsContainerPtr SettingsContainer = SettingsModule->GetContainer("Project");

		const ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings(
			"Project",
			"Plugins",
			"Thumbnail Generator",
			LOCTEXT("RuntimeGeneralSettingsName", "Thumbnail Generator"),
			LOCTEXT("RuntimeGeneralSettingsDescription", "Configuration for Thumbnail Generator Plugin"),
			UThumbnailGeneratorSettings::Get()
		);

		if (SettingsSection.IsValid())
		{
			SettingsSection->OnModified().BindRaw(this, &FThumbnailGeneratorEditorModule::HandleSettingsSaved);
		}
	}
}

void FThumbnailGeneratorEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Thumbnail Generator");
	}
}

bool FThumbnailGeneratorEditorModule::HandleSettingsSaved()
{
	return false; // Saving of the config is handled by UThumbnailGeneratorSettings::PostEditChangeProperty
}

void FThumbnailGeneratorEditorModule::RegisterDetailsCustomization()
{
	FPropertyEditorModule& propertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	propertyEditorModule.RegisterCustomClassLayout(
		UThumbnailGeneratorSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FThumbnailSettingsCustomization::MakeInstance)
	);
}

void FThumbnailGeneratorEditorModule::UnregisterDetailsCustomization()
{
	if (FPropertyEditorModule* propertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		propertyEditorModule->UnregisterCustomClassLayout(UThumbnailGeneratorSettings::StaticClass()->GetFName());
	}
}

void FThumbnailGeneratorEditorModule::RegisterThumbnailGeneratorEvents()
{
	FThumbnailGeneratorModule::SaveThumbnailDelegate = FThumbnailGeneratorModule::FSaveThumbnailDelegate::CreateStatic(&FThumbnailGeneratorEditorModule::SaveThumbnail, false);
}

void FThumbnailGeneratorEditorModule::UnregisterThumbnailGeneratorEvents()
{
	FThumbnailGeneratorModule::SaveThumbnailDelegate.Unbind();
}

void FThumbnailGeneratorEditorModule::RegisterTabSpawner()
{
	const auto RegisterTabSpawner = []()
	{
		const auto SpawnTab = [](const FSpawnTabArgs& Args)->TSharedRef<SDockTab>
		{
			TSharedPtr<SDockTab> MajorTab;
			SAssignNew(MajorTab, SDockTab)
				.TabRole(ETabRole::NomadTab);

			MajorTab->SetTabIcon(FAppStyle::GetBrush("ClassIcon.CameraComponent"));
			MajorTab->SetContent(SNew(SThumbnailGeneratorEditor));

			return MajorTab.ToSharedRef();
		};

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ThumbnailGenEditorTabName, FOnSpawnTab::CreateLambda(SpawnTab))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetStructureRoot())
			.SetDisplayName(LOCTEXT("ThumbnailGenEditorTabTitle", "Thumbnail Generator"))
			.SetTooltipText(LOCTEXT("ThumbnailGenEditorTooltipText", "Open the Thumbnail Generator Window."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "HighresScreenshot.SpecifyCaptureRectangle"));
	};

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

	if (LevelEditorModule.GetLevelEditorTabManager().IsValid())
	{
		RegisterTabSpawner();
	}
	else
	{
		LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda(RegisterTabSpawner);
	}
}

void FThumbnailGeneratorEditorModule::UnregisterTabSpawner()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ThumbnailGenEditorTabName);
	}

	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
	}
}

void FThumbnailGeneratorEditorModule::RegisterLevelEditorCommandExtensions()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	// Extend Level Editor Commands
	{
		struct Local
		{
			static void ResetSceneComponentAfterCopy(USceneComponent* Component)
			{
				Component->SetRelativeLocation_Direct(FVector::ZeroVector);
				Component->SetRelativeRotation_Direct(FRotator::ZeroRotator);

				// Clear out the attachment info after having copied the properties from the source actor
				Component->SetupAttachment(nullptr);

				// HACK: Fetch Component->AttachChildren via reflection and clear it.
				// The origional code used FDirectAttachChildrenAccessor::Get(Component).Empty() (Kismet2.cpp:FResetSceneComponentAfterCopy)
				// which they have friended in the original class
				{
					FProperty* Property = Component->GetClass()->PropertyLink;
					while (Property)
					{
						if (Property->GetName().Equals("AttachChildren"))
						{
							TArray<USceneComponent>* AttachChildren = Property->ContainerPtrToValuePtr<TArray<USceneComponent>>(Component);
							AttachChildren->Empty();
							break;
						}
						Property = Property->PropertyLinkNext;
					}
				}

				// Ensure the light mass information is cleaned up
				Component->InvalidateLightingCache();
			}

			// Modified version of FKismetEditorUtilities::CreateBlueprintFromActor
			static UBlueprint* CreateTmpBlueprintFromActor(AActor* Actor)
			{
				const bool bKeepMobility = false;
				if (!IsValid(Actor))
				{
					UE_LOG(LogThumbnailGeneratorEd, Error, TEXT("CreateTmpBlueprintFromActor - Invalid Actor"));
					return nullptr;
				}

				FString ActorClassName = Actor->GetClass()->GetName();
				ActorClassName.RemoveFromEnd("_C");

				const FString ActorBlueprintName = FString::Printf(TEXT("%s_GEN"), *ActorClassName);
				const static FString ActorBlueprintPackagePath = TEXT("/Engine/ThumbnailGenerator/Thumbnail_Generated_Actor_Class");

				// CreateBlueprint is unable to over-write existing blueprints, so delete the package if it already exists, then re-create it
				UPackage* ActorBlueprintPackage = FindPackage(nullptr, *ActorBlueprintPackagePath);
				if (ActorBlueprintPackage)
				{
					if (UObject* ExistingAsset = (UObject*)FindObjectWithOuter(ActorBlueprintPackage, UBlueprint::StaticClass(), NAME_None))
					{
						if (ObjectTools::ForceDeleteObjects({ ExistingAsset }, false) == 0)
						{
							UE_LOG(LogThumbnailGeneratorEd, Warning, TEXT("Could not delete existing asset %s"), *ExistingAsset->GetFullName());
							return nullptr;
						}

						ActorBlueprintPackage = nullptr;
					}
					else
					{
						UE_LOG(LogThumbnailGeneratorEd, Warning, TEXT("Could not find blueprint object in package %s"), *ActorBlueprintPackagePath);
						return nullptr;
					}
				}

				if (!ActorBlueprintPackage)
				{
					ActorBlueprintPackage = CreatePackage(*ActorBlueprintPackagePath);
					ActorBlueprintPackage->SetFlags(EObjectFlags::RF_Transient);
				}

				UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(Actor->GetClass(), ActorBlueprintPackage, *ActorBlueprintName, EBlueprintType::BPTYPE_Normal, 
					                                                   UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName("CreateFromActor"));

				if (NewBlueprint == nullptr)
				{
					UE_LOG(LogThumbnailGeneratorEd, Error, TEXT("Failed to generate blueprint for actor %s"), *Actor->GetFullName());
					return nullptr;
				}

				if (Actor->GetInstanceComponents().Num() > 0)
				{
					#if ENGINE_VERSION_HIGHER_THAN(4, 25)
					//! NOTE: Defaults for FAddComponentsToBlueprintParams match what we want, so can skip the parameter
					FKismetEditorUtilities::AddComponentsToBlueprint(NewBlueprint, Actor->GetInstanceComponents());
					#elif ENGINE_VERSION_HIGHER_THAN(4, 24)
					FKismetEditorUtilities::AddComponentsToBlueprint(NewBlueprint, Actor->GetInstanceComponents(), FKismetEditorUtilities::EAddComponentToBPHarvestMode::None, nullptr, false);
					#else
					FKismetEditorUtilities::AddComponentsToBlueprint(NewBlueprint, Actor->GetInstanceComponents(), false, nullptr, false);
					#endif
				}

				if (NewBlueprint->GeneratedClass != nullptr)
				{
					AActor* CDO = CastChecked<AActor>(NewBlueprint->GeneratedClass->GetDefaultObject());
					const EditorUtilities::ECopyOptions::Type CopyOptions = (EditorUtilities::ECopyOptions::Type)(EditorUtilities::ECopyOptions::OnlyCopyEditOrInterpProperties 
						| EditorUtilities::ECopyOptions::PropagateChangesToArchetypeInstances 
						| EditorUtilities::ECopyOptions::SkipInstanceOnlyProperties);

					EditorUtilities::CopyActorProperties(Actor, CDO, CopyOptions);

					if (USceneComponent* DstSceneRoot = CDO->GetRootComponent())
					{
						ResetSceneComponentAfterCopy(DstSceneRoot);

						// Copy relative scale from source to target.
						if (USceneComponent* SrcSceneRoot = Actor->GetRootComponent())
						{
							DstSceneRoot->SetRelativeScale3D_Direct(SrcSceneRoot->GetRelativeScale3D());
						}
					}
				}

				FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

				return NewBlueprint;
			}
		};

		TSharedRef<FUICommandList> CommandList = LevelEditorModule.GetGlobalLevelEditorActions();
		CommandList->MapAction(
			FThumbnailGeneratorEditorCommands::Get().GenerateThumbnail, 
			FExecuteAction::CreateLambda([this]
			{
				for (auto It = GEditor->GetSelectedActorIterator(); It; ++It)
				{
					if (AActor* SelectedActor = Cast<AActor>(*It))
					{
						if (UBlueprint* GeneratedBlueprint = Local::CreateTmpBlueprintFromActor(SelectedActor))
						{
							OpenThumbnailGenerator(GeneratedBlueprint->GeneratedClass.Get());
						}
						break;
					}
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				for (auto It = GEditor->GetSelectedActorIterator(); It; ++It)
				{
					if (Cast<AActor>(*It))
					{
						return true;
					}
				}

				return false;
			})
		);
	}

	const auto OnExtendLevelEditor = FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateLambda([this](const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors)
	{
		TSharedRef<FExtender> Extender(new FExtender());

		if (SelectedActors.Num() > 0)
		{
			Extender->AddMenuExtension(
				"ActorAsset",
				EExtensionHook::After,
				CommandList,
				FMenuExtensionDelegate::CreateRaw(this, &FThumbnailGeneratorEditorModule::CreateAssetContextMenu)
			);
		}

		return Extender;
	});

	auto& LevelEditorMenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
	LevelEditorExtenderDelegateHandle = LevelEditorMenuExtenderDelegates[LevelEditorMenuExtenderDelegates.Add(OnExtendLevelEditor)].GetHandle();
}

void FThumbnailGeneratorEditorModule::UnregisterLevelEditorCommandExtensions()
{
	if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		auto& LevelEditorMenuExtenderDelegates = LevelEditorModule->GetAllLevelViewportContextMenuExtenders();
		LevelEditorMenuExtenderDelegates.RemoveAll([&](const auto& Delegate) { return Delegate.GetHandle() == LevelEditorExtenderDelegateHandle; });
	}
}

void FThumbnailGeneratorEditorModule::RegisterContentBrowserCommandExtensions()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	const auto OnExtendContentBrowserCommands = FContentBrowserCommandExtender::CreateLambda([&](TSharedRef<FUICommandList> CommandList, FOnContentBrowserGetSelection GetSelectionDelegate)
	{
		CommandList->MapAction(
			FThumbnailGeneratorEditorCommands::Get().GenerateThumbnail, 
			FExecuteAction::CreateLambda([&, GetSelectionDelegate]
			{
				TArray<FAssetData> SelectedAssets;
				TArray<FString> SelectedPaths;
				GetSelectionDelegate.ExecuteIfBound(SelectedAssets, SelectedPaths);
				OpenThumbnailGenerator(ThumbnailGeneratorEdHelpers::LoadFirstValidActorClass(SelectedAssets));
			})
		);
	});
	TArray<FContentBrowserCommandExtender>& CBCommandExtenderDelegates = ContentBrowserModule.GetAllContentBrowserCommandExtenders();
	ContentBrowserCommandExtenderDelegateHandle = CBCommandExtenderDelegates[CBCommandExtenderDelegates.Add(OnExtendContentBrowserCommands)].GetHandle();


	const auto OnExtendContentBrowserAssetSelectionMenu = FContentBrowserMenuExtender_SelectedAssets::CreateLambda([&](const TArray<FAssetData>& SelectedAssets)->TSharedRef<FExtender>
	{
		TSharedRef<FExtender> Extender(new FExtender());

		if (ThumbnailGeneratorEdHelpers::ContainsActorClass(SelectedAssets))
		{
			Extender->AddMenuExtension(
				"Delete",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateRaw(this, &FThumbnailGeneratorEditorModule::CreateAssetContextMenu)
			);
		}

		return Extender;
	});
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	ContentBrowserAssetExtenderDelegateHandle = CBAssetMenuExtenderDelegates[CBAssetMenuExtenderDelegates.Add(OnExtendContentBrowserAssetSelectionMenu)].GetHandle();


	const auto OnExtendAssetEditor = FAssetEditorExtender::CreateLambda([&](const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects)->TSharedRef<FExtender>
	{
		const TSubclassOf<AActor> ActorClass = [&]()->TSubclassOf<AActor>
		{
			for (UObject* EditedAsset : ContextSensitiveObjects)
			{
				UBlueprint* BlueprintAsset = Cast<UBlueprint>(EditedAsset);
				if (BlueprintAsset && BlueprintAsset->GeneratedClass && BlueprintAsset->GeneratedClass->IsChildOf<AActor>())
				{
					return (TSubclassOf<AActor>)BlueprintAsset->GeneratedClass;
				}
			}

			return nullptr;
		}();

		TSharedRef<FExtender> Extender(new FExtender());

		if (ActorClass.Get())
		{
			CommandList->MapAction(
				FThumbnailGeneratorEditorCommands::Get().GenerateThumbnail,
				FExecuteAction::CreateRaw(this, &FThumbnailGeneratorEditorModule::OpenThumbnailGenerator, ActorClass)
			);

			Extender->AddMenuExtension(
				"FindInContentBrowser",
				EExtensionHook::After,
				CommandList,
				FMenuExtensionDelegate::CreateRaw(this, &FThumbnailGeneratorEditorModule::CreateAssetContextMenu));
		}

		return Extender;
	});
	TArray<FAssetEditorExtender>& AssetEditorMenuExtenderDelegates = FAssetEditorToolkit::GetSharedMenuExtensibilityManager()->GetExtenderDelegates();
	AssetEditorExtenderDelegateHandle = AssetEditorMenuExtenderDelegates[AssetEditorMenuExtenderDelegates.Add(OnExtendAssetEditor)].GetHandle();
}

void FThumbnailGeneratorEditorModule::UnregisterContentBrowserCommandExtensions()
{
	if (FContentBrowserModule* ContentBrowserModule = FModuleManager::GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser")))
	{
		TArray<FContentBrowserCommandExtender>& CBCommandExtenderDelegates = ContentBrowserModule->GetAllContentBrowserCommandExtenders();
		CBCommandExtenderDelegates.RemoveAll([&](const FContentBrowserCommandExtender& Delegate) { return Delegate.GetHandle() == ContentBrowserCommandExtenderDelegateHandle; });

		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule->GetAllAssetViewContextMenuExtenders();
		CBAssetMenuExtenderDelegates.RemoveAll([&](const FContentBrowserMenuExtender_SelectedAssets& Delegate) { return Delegate.GetHandle() == ContentBrowserAssetExtenderDelegateHandle; });
	}

	const auto SharedMenuExtensibilityManager = FAssetEditorToolkit::GetSharedMenuExtensibilityManager();
	if (SharedMenuExtensibilityManager.IsValid())
	{
		TArray<FAssetEditorExtender>& AssetEditorMenuExtenderDelegates = FAssetEditorToolkit::GetSharedMenuExtensibilityManager()->GetExtenderDelegates();
		AssetEditorMenuExtenderDelegates.RemoveAll([&](const FAssetEditorExtender& Delegate) { return Delegate.GetHandle() == AssetEditorExtenderDelegateHandle; });
	}
}

void FThumbnailGeneratorEditorModule::OpenThumbnailGenerator(const TSubclassOf<AActor> ActorClass)
{
#if ENGINE_VERSION_HIGHER_THAN(4, 25)
	TSharedPtr<SDockTab> NewTab = FGlobalTabmanager::Get()->TryInvokeTab(ThumbnailGenEditorTabName);
	if (!NewTab.IsValid())
		return;
#else
	TSharedRef<SDockTab> NewTab = FGlobalTabmanager::Get()->InvokeTab(ThumbnailGenEditorTabName);
#endif

	TSharedRef<SThumbnailGeneratorEditor> ThumbnailGeneratorEditor = StaticCastSharedRef<SThumbnailGeneratorEditor>(NewTab->GetContent());
	ThumbnailGeneratorEditor->ResetThumbnailGeneratorEditorSettings();
	ThumbnailGeneratorEditor->ThumbnailGeneratorEditorSettings->ActorClass = ActorClass;
	ThumbnailGeneratorEditor->GenerateThumbnail();
}

void FThumbnailGeneratorEditorModule::CreateAssetContextMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		FThumbnailGeneratorEditorCommands::Get().GenerateThumbnail,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "HighresScreenshot.SpecifyCaptureRectangle")
	);
}

UTexture2D* FThumbnailGeneratorEditorModule::SaveThumbnail(UTexture2D* Thumbnail, const FString &OutputDirectory, const FString &OutputName, bool bWithOverwriteUI)
{
	struct Local
	{
		// Taken from UFactory
		static UObject* CreateOrOverwriteAsset(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InTemplate, bool bWithOverwriteUI)
		{
			// create an asset if it doesn't exist
			UObject* ExistingAsset = StaticFindObject(nullptr, InParent, *InName.ToString());

			if (!ExistingAsset)
			{
				return NewObject<UObject>(InParent, InClass, InName, InFlags, InTemplate);
			}

			if (bWithOverwriteUI)
			{
				const FText DialogueTitle = LOCTEXT("Warning", "Warning");
				const FText DialogueText = FText::FormatNamed(LOCTEXT("AssetAlreadyExists", "Texture Asset {AssetName} Already Exists, Overwrite?"), TEXT("AssetName"), FText::FromName(InName));
				if (FMessageDialog::Open(EAppMsgType::YesNo, DialogueText, DialogueTitle) != EAppReturnType::Yes)
				{
					return nullptr;
				}
			}

			// overwrite existing asset, if possible
			if (ExistingAsset->GetClass()->IsChildOf(InClass))
			{
				return NewObject<UObject>(InParent, InClass, InName, InFlags, InTemplate);
			}

			// otherwise delete and replace
			if (!ObjectTools::DeleteSingleObject(ExistingAsset))
			{
				UE_LOG(LogThumbnailGeneratorEd, Warning, TEXT("Could not delete existing asset %s"), *ExistingAsset->GetFullName());
				return nullptr;
			}

			// keep InPackage alive through the GC, in case ExistingAsset was the only reason it was around
			const bool bRootedPackage = InParent->IsRooted();

			if (!bRootedPackage)
			{
				InParent->AddToRoot();
			}

			// force GC so we can cleanly create a new asset (and not do an 'in place' replacement)
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

			if (!bRootedPackage)
			{
				InParent->RemoveFromRoot();
			}

			// try to find the existing asset again now that the GC has occurred
			ExistingAsset = StaticFindObject(nullptr, InParent, *InName.ToString());

			// if the object is still around after GC, fail this operation
			if (ExistingAsset)
			{
				return nullptr;
			}

			// create the asset in the package
			return NewObject<UObject>(InParent, InClass, InName, InFlags, InTemplate);
		}
	};

	if (!IsValid(Thumbnail))
	{
		UE_LOG(LogThumbnailGeneratorEd, Error, TEXT("SaveThumbnail - Invalid thumbnail object"));
		return nullptr;
	}

	auto PlatformData = Thumbnail->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0)
	{
		UE_LOG(LogThumbnailGeneratorEd, Error, TEXT("SaveThumbnail - Thumbnail is missing valid platform data (%s)"), *Thumbnail->GetName());
		return nullptr;
	}

	const ETextureSourceFormat TextureFormat = [&]() // Right now, we only support RGBA 8 and 16 bit
	{
		switch (PlatformData->PixelFormat)
		{
		case PF_B8G8R8A8: return TSF_BGRA8;
		case PF_FloatRGBA: return TSF_RGBA16F;
		}
		return TSF_Invalid;
	}();

	if (TextureFormat == TSF_Invalid)
	{
		UE_LOG(LogThumbnailGeneratorEd, Error, TEXT("SaveThumbnail - Unsupported pixel format (%s)"), *Thumbnail->GetName());
		return nullptr;
	}

	const FString Directory = [&]()
	{
		if (OutputDirectory.IsEmpty())
			return FPaths::Combine(UKismetSystemLibrary::GetProjectContentDirectory(), TEXT("ThumbnailGenerator"));

		if (OutputDirectory.StartsWith(TEXT("/Content")))
			return OutputDirectory;

		if (OutputDirectory.StartsWith(TEXT("/Game")))
		{
			FString TmpOutDir = OutputDirectory;
			TmpOutDir.RemoveFromStart(TEXT("/Game"));
			return FPaths::Combine(UKismetSystemLibrary::GetProjectContentDirectory(), TmpOutDir);
		}

		return OutputDirectory;
	}();

	const FString ProjectContentDir = UKismetSystemLibrary::GetProjectContentDirectory();
	if (!FPaths::IsUnderDirectory(Directory, ProjectContentDir))
	{
		UE_LOG(LogThumbnailGeneratorEd, Error, TEXT("SaveThumbnail - Directory %s is not under project content directory"), *Directory);
		return nullptr;
	}

	const FString ThumbnailName = [&]() 
	{
		if (!OutputName.IsEmpty())
			return OutputName;

		FString OutName = Thumbnail->GetName();
		if (OutName.EndsWith(TEXT("_Thumbnail"))) // Is this an auto-generated thumbnail name?
		{
			FString BlueprintClassName;
			OutName.Split("_C_", &BlueprintClassName, nullptr, ESearchCase::CaseSensitive);
			if (!BlueprintClassName.IsEmpty())
			{
				return FString::Printf(TEXT("%s_Thumbnail"), *BlueprintClassName);
			}
		}
		return OutName;
	}();

	const FString PackagePath = [&]() 
	{
		FString RelativePath = Directory;
		FPaths::MakePathRelativeTo(RelativePath, *ProjectContentDir);
		FPaths::CollapseRelativeDirectories(RelativePath);

		return FPaths::Combine(TEXT("/Game"), RelativePath, ThumbnailName);
	}();

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogThumbnailGeneratorEd, Error, TEXT("SaveThumbnail - Failed to create package %s"), *PackagePath);
		return nullptr;
	}

	Package->FullyLoad();

	UTexture2D* NewTexture = Cast<UTexture2D>(Local::CreateOrOverwriteAsset(
		UTexture2D::StaticClass(), 
		Package, 
		*ThumbnailName, 
		RF_Public | RF_Standalone | RF_Transactional, 
		Thumbnail,
		bWithOverwriteUI
	));

	// Copy texture data into new Texture
	if (NewTexture)
	{
		NewTexture->Source.Init2DWithMipChain(PlatformData->SizeX, PlatformData->SizeY, TextureFormat);
		
		uint8* NewTextureData = NewTexture->Source.LockMip(0);
		const auto& ThumbnailBulkData = PlatformData->Mips[0].BulkData;
		const void* ThumbnailData = ThumbnailBulkData.LockReadOnly();
		
		FMemory::Memcpy(NewTextureData, ThumbnailData, ThumbnailBulkData.GetBulkDataSize());
		
		NewTexture->Source.UnlockMip(0);
		ThumbnailBulkData.Unlock();

		NewTexture->PostEditChange();
		NewTexture->UpdateResource();

		NewTexture->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(NewTexture);

		if (bWithOverwriteUI)
		{
			TArray<UObject*> AssetArray = { NewTexture };
			GEditor->SyncBrowserToObjects(AssetArray);
		}
	}

	return NewTexture;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FThumbnailGeneratorEditorModule, ThumbnailGeneratorEditor);
