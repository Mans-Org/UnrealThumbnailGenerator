// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/GCObject.h"
#include "ThumbnailGeneratorSettings.h"
#include "ThumbnailGenerator.generated.h"

class UTexture2D;
class UStaticMeshComponent;
class UMaterialInstanceConstant;
class USceneCaptureComponent2D;

class UThumbnailGeneratorScript;

// The FThumbnailGenerator can be used to generate thumbnails for your actors.
// This object manages the underlying scene used for thumbnail generation and various render resources required to capture the thumbnail.
class THUMBNAILGENERATOR_API FThumbnailGenerator : public FGCObject
{
private:

	TSharedPtr<class FThumbnailSceneInterface> ThumbnailScene;
	TSharedPtr<struct FRenderTargetCache>      RenderTargetCache;
	TSharedPtr<class FWidgetRenderer>          WidgetRenderer;

	TObjectPtr<class USceneCaptureComponent2D> CaptureComponent = nullptr;
	TArray<TObjectPtr<UThumbnailGeneratorScript>> ThumbnailGeneratorScripts;
	
	TSet<TObjectPtr<AActor>> ThumbnailSceneActors;

	bool bIsCapturingThumbnail = false;

#if WITH_EDITOR
	FDelegateHandle EndPIEDelegateHandle;
#endif

public:

	FThumbnailGenerator() = default;

	FThumbnailGenerator(bool bInvalidateOnPIEEnd);

	virtual ~FThumbnailGenerator();

	/** 
	* Synchronously generates a thumbnail for the supplied Actor Class.
	* 
	* @param ActorClass        The type of actor which will be spawned for thumbnail generation.
	* @param ThumbnailSettings The ThumbnailSettings can be used to override individual Thumbnail Settings for this capture.
	* @param ResourceObject    Optional pointer to a UTexture2D object to use for the generated thumbnail (if nullptr a new UTexture2D will be created)
	* @param Properties        Property values to apply to the actor before thumbnail generation (In format Pair<Name, Value>, where the value is applied using Property->ImportText)
	* @return                  Returns the generated UTexture2D object. Nullptr of a thumbnail could not be generated.
	*/
	UTexture2D* GenerateActorThumbnail(TSubclassOf<AActor> ActorClass, const FThumbnailSettings& ThumbnailSettings, UTexture2D* ResourceObject = nullptr, const TMap<FString, FString>& Properties = TMap<FString, FString>());

	/**
	* Sets up thumbnail generation for the supplied Actor Class. This function can be useful if you wish to execute some custom logic on the Actor before capturing the thumbnail.
	* IMPORTANT: Do not call this again before calling FinishGenerateActorThumbnail.
	* 
	* @param ActorClass           The type of actor which will be spawned for thumbnail generation.
	* @param ThumbnailSettings    The ThumbnailSettings can be used to override individual Thumbnail Settings for this capture.
	* @param Properties           Property values to apply to the actor before thumbnail generation (In format Pair<Name, Value>, where the value is applied using Property->ImportText)
	* @param bFinishSpawningActor Whether to call FinishSpawning on the spawned Actor. If false the actor will be defered spawned without FinishSpawn being called.
	* @return                     Returns a pointer to the spawned actor. Nullptr if actor failed to spawn.
	*/
	AActor* BeginGenerateActorThumbnail(TSubclassOf<AActor> ActorClass, const FThumbnailSettings& ThumbnailSettings, const TMap<FString, FString>& Properties = TMap<FString, FString>(), bool bFinishSpawningActor = true);

	/**
	* Finish the thumbnail generation setup by BeginGenerateActorThumbnail.
	* IMPORTANT: Do not call this before calling BeginGenerateActorThumbnail.
	* 
	* @param Actor                The actor which was spawned by BeginGenerateActorThumbnail.
	* @param ThumbnailSettings    The ThumbnailSettings can be used to override individual Thumbnail Settings for this capture (Should be the same struct that was passed to BeginGenerateActorThumbnail).
	* @param ResourceObject       Optional pointer to a UTexture2D object to use for the generated thumbnail (if nullptr a new UTexture2D will be created)
	* @param bFinishSpawningActor Whether to call FinishSpawning on the spawned Actor. Should be the opposite of whatever was specified in BeginGenerateActorThumbnail.
	* @return                     Returns the generated UTexture2D object. Nullptr of a thumbnail could not be generated.
	*/
	UTexture2D* FinishGenerateActorThumbnail(AActor* Actor, const FThumbnailSettings& ThumbnailSettings, UTexture2D* ResourceObject = nullptr, bool bFinishSpawningActor = false);

	/** 
	* Creates the underlying world used for thumbnail generation (Gets called automatically on "Generate Thumbnail"). 
	* Might want to call this if the assets required for thumbnail generation causes hitching when loaded for the first time.
	* 
	* Calling this function multiple times will cause the thumbnail scene to be reconstructed. Use with caution.
	* 
	* @param BackgroundSceneSettings The settings used to generate initialize the background world.
	*/
	void InitializeThumbnailWorld(const FThumbnailBackgroundSceneSettings &BackgroundSceneSettings);

	/**
	* Destroys the underlying world used for thumbnail generation.
	*/
	void InvalidateThumbnailWorld();

	/** 
	* Gets the underlying world used for thumbnail generation
	* 
	* @return Pointer to the thumbnail UWorld
	*/
	UWorld* GetThumbnailWorld() const;

	/**
	* @return The capture source used for capturing thumbnails. Will be the best possible capture source available for this platform.
	*/
	ESceneCaptureSource GetCaptureSource() const;

	/** 
	* Gets the underlying Capture Component used for thumbnail generation
	* 
	* @return Pointer to the USceneCaptureComponent2D
	*/
	FORCEINLINE USceneCaptureComponent2D* GetThumbnailCaptureComponent() const { return CaptureComponent;  }

private:

	UTexture2D* CaptureThumbnail(const FThumbnailSettings& ThumbnailSettings, UTextureRenderTarget2D* RenderTarget, AActor* Actor, UTexture2D* ResourceObject);

	void PrepareThumbnailCapture();

	void CleanupThumbnailCapture();

	// ~Begin: FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector);
	virtual FString GetReferencerName() const override;
	// ~End: FGCObject Interface

};

extern THUMBNAILGENERATOR_API FThumbnailGenerator* GThumbnailGenerator;

UCLASS(meta=(ScriptName="ThumbnailGeneration"))
class THUMBNAILGENERATOR_API UThumbnailGeneration : public UObject
{
	GENERATED_BODY()

public:

	/** 
	* Synchronously generates a thumbnail for the supplied Actor Class using the global thumbnail generator.
	* 
	* @param ActorClass        The type of actor which will be spawned for thumbnail generation.
	* @param ThumbnailSettings The ThumbnailSettings can be used to override individual Thumbnail Settings for this capture.
	* @param ResourceObject    Optional pointer to a UTexture2D object to use for the generated thumbnail (if nullptr a new UTexture2D will be created)
	* @param Properties        Property values to apply to the actor before thumbnail generation (In format Pair<Name, Value>, where the value is applied using Property->ImportText)
	* @return                  Returns the generated UTexture2D object. Nullptr of a thumbnail could not be generated.
	*/
	static UTexture2D* GenerateThumbnail(TSubclassOf<AActor> ActorClass, const FThumbnailSettings& ThumbnailSettings = FThumbnailSettings(), 
		UTexture2D* ResourceObject = nullptr, const TMap<FString, FString>& Properties = TMap<FString, FString>());

	DECLARE_DELEGATE_OneParam(FGenerateThumbnailCallbackNative, UTexture2D*)
	DECLARE_DELEGATE_OneParam(FPreCaptureThumbnailNative, AActor*)

	/** 
	* Asynchronousy generates a thumbnail for the supplied Actor Class using the global thumbnail generator.
	* 
	* @param ActorClass          The actor class of which a thumbnail will be generated.
	* @param Callback            Callback for when the thumbnail has finished generating.
	* @param ThumbnailSettings   This struct can be used to override individual Thumbnail Settings for this capture.
	* @param PreCaptureThumbnail This delegate will be executed on the thumbnail actor before the thumbnail is captured
	* @param ResourceObject      Optional pointer to a UTexture2D object to use for the generated thumbnail (if nullptr a new UTexture2D will be created)
	* @param Properties          Property values to apply to the actor before thumbnail generation (In format Pair<Name, Value>, where the value is applied using Property->ImportText)
	* @return                    Pointer to the generated UTexture2D object (null if thumbnail failed to generate)
	*/
	static void GenerateThumbnailAsync(UClass* ActorClass, const FGenerateThumbnailCallbackNative& Callback, const FThumbnailSettings& ThumbnailSettings = FThumbnailSettings(),
		const FPreCaptureThumbnailNative& PreCaptureThumbnail = FPreCaptureThumbnailNative(), UTexture2D* ResourceObject = nullptr, const TMap<FString, FString>& Properties = TMap<FString, FString>());

	/** 
	* Gets the underlying world used for thumbnail generation in the global thumbnail generator
	* 
	* @return Pointer to the thumbnail UWorld
	*/
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Thumbnail Generator")
	static UWorld* GetThumbnailWorld();

	/** 
	* Gets the underlying Capture Component used for thumbnail generation in the global thumbnail generator
	* 
	* @return Pointer to the USceneCaptureComponent2D
	*/
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Thumbnail Generator")
	static USceneCaptureComponent2D* GetThumbnailCaptureComponent();

	/** 
	* Creates the underlying world used for thumbnail generation for the global thumbnail generator (Gets called automatically on "Generate Thumbnail"). 
	* Might want to call this if the assets required for thumbnail generation causes hitching when loaded for the first time.
	* 
	* Calling this function multiple times will cause the thumbnail scene to be reconstructed. Use with caution.
	* 
	* @param BackgroundSceneSettings The settings used to generate initialize the background world.
	*/
	UFUNCTION(BlueprintCallable, Category = "Thumbnail Generator")
	static void InitializeThumbnailWorld(FThumbnailBackgroundSceneSettings BackgroundSceneSettings);

	/**
	* Saves the UTexture2D thumbnail object to the specified path as a .uasset
	* @param Thumbnail The thumbnail texture to save
	* @param Directory The path to use when saving the thumbnail (needs to be within project content directory)
	* @param Name      Optional name to use when saving thumbnail (derived from the Texture object if left blank)
	* @return          Pointer to the newly created Texture Asset
	*/
	UFUNCTION(BlueprintCallable, Category = "Thumbnail Generator|Editor Utility", meta=(DevelopmentOnly))
	static UTexture2D* SaveThumbnail(UTexture2D* Thumbnail, const FDirectoryPath &OutputDirectory, FString OutputName = "");


	// Blueprint Internal Functions

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE"))
	static AActor* K2_BeginGenerateThumbnail(UClass* ActorClass, const FThumbnailSettings& ThumbnailSettings);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE"))
	static UTexture2D* K2_FinishGenerateThumbnail(AActor* Actor, const FThumbnailSettings& ThumbnailSettings);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE"))
	static void K2_FinishSpawningThumbnailActor(AActor* Actor);

	DECLARE_DYNAMIC_DELEGATE_OneParam(FGenerateThumbnailCallback, class UTexture2D*, Thumbnail);
	DECLARE_DYNAMIC_DELEGATE_OneParam(FPreCaptureThumbnail, class AActor*, Actor);

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE"))
	static void K2_GenerateThumbnailAsync(UClass* ActorClass, FThumbnailSettings ThumbnailSettings, 
		TMap<FString, FString> Properties, FGenerateThumbnailCallback Callback, FPreCaptureThumbnail PreCaptureThumbnail);

	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = "TRUE"))
	static FThumbnailSettings K2_FinalizeThumbnailSettings(FThumbnailSettings ThumbnailSettings);

	UFUNCTION(BlueprintPure, CustomThunk, meta = (CustomStructureParam = "Property", BlueprintInternalUseOnly = "TRUE"))
	static FString K2_ExportPropertyText(const int32& Property);
	DECLARE_FUNCTION(execK2_ExportPropertyText);

	UFUNCTION(BlueprintPure, CustomThunk, meta = (ArrayParm = "ArrayProperty", BlueprintInternalUseOnly = "TRUE"))
	static FString K2_ExportArrayPropertyText(const TArray<int32>& Property);
	DECLARE_FUNCTION(execK2_ExportArrayPropertyText);

	UFUNCTION(BlueprintPure, CustomThunk, meta = (MapParam = "MapProperty", BlueprintInternalUseOnly = "TRUE"))
	static FString K2_ExportMapPropertyText(const TMap<int32, int32>& Property);
	DECLARE_FUNCTION(execK2_ExportMapPropertyText);

	UFUNCTION(BlueprintPure, CustomThunk, meta = (SetParam = "SetProperty", BlueprintInternalUseOnly = "TRUE"))
	static FString K2_ExportSetPropertyText(const TSet<int32>& Property);
	DECLARE_FUNCTION(execK2_ExportSetPropertyText);

};