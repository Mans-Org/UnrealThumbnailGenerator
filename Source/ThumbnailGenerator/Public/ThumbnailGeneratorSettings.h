// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Camera/CameraTypes.h"
#include "ThumbnailGeneratorSettings.generated.h"

class UUserWidget;
class UTextureCube;
class UThumbnailGeneratorScript;

UENUM(BlueprintType)
enum class EThumbnailSceneSimulationMode : uint8
{
	ENone					UMETA(DisplayName = "Don't simulate"),
	EActor					UMETA(DisplayName = "Actor"),
	EAllComponents			UMETA(DisplayName = "Only Components"),
	ESpecifiedComponents	UMETA(DisplayName = "Only Specified Components"),
};

UENUM(BlueprintType)
enum class EThumbnailAlphaBlendMode : uint8
{
	EReplace	UMETA(DisplayName = "Replace"),
	EAdd		UMETA(DisplayName = "Add"),
	EMultiply	UMETA(DisplayName = "Multiply"),
	ESubtract	UMETA(DisplayName = "Subtract"),
};

UENUM(BlueprintType)
enum class EThumbnailBitDepth : uint8
{
	E8	UMETA(DisplayName = "8-bit"),
	E16	UMETA(DisplayName = "16-bit"),
};

UENUM(BlueprintType)
enum class EThumbnailCameraFitMode : uint8
{
	EFill   UMETA(DisplayName="Fill"),
	EFit    UMETA(DisplayName="Fit"),
	EFitX   UMETA(DisplayName="Fit X"),
	EFitY   UMETA(DisplayName="Fit Y"),
};

USTRUCT(BlueprintType, meta=(HiddenByDefault))
struct THUMBNAILGENERATOR_API FThumbnailSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ThumbnailTextureWidth:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ThumbnailTextureHeight:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ThumbnailBitDepth:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bCaptureAlpha:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_AlphaBlendMode:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ThumbnailUI:1;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ProjectionType:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CameraFOV:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CameraOrbitRotation:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CameraFitMode:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CameraDistanceOffset:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CameraDistanceOverride:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_OrthoWidthOffset:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_OrthoWidthOverride:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CustomActorBounds:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CameraPositionOffset:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CameraRotationOffset:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CustomCameraLocation:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CustomCameraRotation:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CustomOrthoWidth:1;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SimulationMode:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SimulateSceneTime:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SimulateSceneFramerate:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ComponentsToSimulate:1;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_CustomActorTransform:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bSnapToFloor:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ComponentBoundsBlacklist:1;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bIncludeHiddenComponentsInBounds:1;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DirectionalLightRotation:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DirectionalLightIntensity:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_DirectionalLightColor:1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (PinHiddenByDefault, InlineEditConditionToggle))
    uint8 bOverride_DirectionalFillLightRotation:1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (PinHiddenByDefault, InlineEditConditionToggle))
    uint8 bOverride_DirectionalFillLightIntensity:1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Overrides, meta = (PinHiddenByDefault, InlineEditConditionToggle))
    uint8 bOverride_DirectionalFillLightColor:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SkyLightColor:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_SkyLightIntensity:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bShowEnvironment:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bEnvironmentAffectLighting:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_EnvironmentColor:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_EnvironmentCubeMap:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_EnvironmentRotation:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_PostProcessingSettings:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ThumbnailSkySphere:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_ThumbnailGeneratorScripts:1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Overrides, meta=(PinHiddenByDefault, InlineEditConditionToggle))
	uint8 bOverride_bDebugBounds:1;


	// Thumbnail render target width.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(EditCondition = "bOverride_ThumbnailTextureWidth", ClampMin = "0", ClampMax = "65535"))
	int32 ThumbnailTextureWidth;

	// Thumbnail render target height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(EditCondition = "bOverride_ThumbnailTextureHeight", ClampMin = "0", ClampMax = "32767"))
	int32 ThumbnailTextureHeight;

	// The Bith-depth used to generated thumbnails.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(EditCondition = "bOverride_ThumbnailBitDepth"))
	EThumbnailBitDepth ThumbnailBitDepth = EThumbnailBitDepth::E8;

	// Renders the image twice, once capturing only the alpha. The alpha is then blended with the main capture using AlphaBlendMode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(EditCondition = "bOverride_bCaptureAlpha"))
	bool bCaptureAlpha;

	// How the captured alpha will be blended with the main capture result. (Usefull if your post process effects outputs alpha)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(EditCondition = "bOverride_AlphaBlendMode"))
	EThumbnailAlphaBlendMode AlphaBlendMode = EThumbnailAlphaBlendMode::EReplace;

	// A user widget which to render as part of the thumbnail.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(EditCondition = "bOverride_ThumbnailUI"))
	TSubclassOf<UUserWidget> ThumbnailUI;


	// Type of camera projection to use when generating this thumbnail (Perspective/Orthographic)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera", meta=(EditCondition = "bOverride_ProjectionType"))
	TEnumAsByte<ECameraProjectionMode::Type> ProjectionType;

	// Camera field of view (in degrees) of the Thumbnail Capture Component. (Ignored in Orthographic)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera", meta=(EditCondition = "bOverride_CameraFOV", UIMin = "5.0", UIMax = "170", ClampMin = "0.001", ClampMax = "360.0"))
	float CameraFOV;

	// Amount (in degrees) the camera will orbit around the thumbnail actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_CameraOrbitRotation"))
	FRotator CameraOrbitRotation;

	// How the automatic framing will best try and fit the actor in frame
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_CameraFitMode"))
	EThumbnailCameraFitMode CameraFitMode;

	// Distance offset (in cm) from the automatically calculated distance. (Ignored in Orthographic)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_CameraDistanceOffset"))
	float CameraDistanceOffset;

	// Override the automatically calculated distance (in cm). (Ignored in Orthographic)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_CameraDistanceOverride"))
	float CameraDistanceOverride;

	// Offset from the auto-calculated OrthoWidth. (Ingored in Perspective)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_OrthoWidthOffset"))
	float OrthoWidthOffset;

	// Override the auto-calculated OrthoWidth. (Ignored in Perspective)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_OrthoWidthOverride"))
	float OrthoWidthOverride;

	// Custom bounds that can be used instead of pulling the bounds from the Actor. Useful for actors which does not have bounds of their own such as particle effects.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_CustomActorBounds"))
	FBox CustomActorBounds;

	// Location offset in camera space from the automatically calculated position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_CameraPositionOffset"))
	FVector CameraPositionOffset;

	// Rotation offset in camera space from the automatically calculated rotation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Auto Frame", meta=(EditCondition = "bOverride_CameraRotationOffset"))
	FRotator CameraRotationOffset;

	// Directly set the location of the camera. Enabling this will disable auto framing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Custom", meta=(EditCondition = "bOverride_CustomCameraLocation"))
	FVector CustomCameraLocation;

	// Directly set the rotation of the camera. Enabling this will disable auto framing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Custom", meta=(EditCondition = "bOverride_CustomCameraRotation"))
	FRotator CustomCameraRotation;

	// Directly set the OrthoWidth. Enabling this will disable auto framing. (Ingored in Perspective)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Custom", meta=(EditCondition = "bOverride_CustomOrthoWidth"))
	float CustomOrthoWidth;


	// How to simulate the thumbnail actor before capturing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thumbnail Scene", meta=(EditCondition = "bOverride_SimulationMode"))
	EThumbnailSceneSimulationMode SimulationMode;

	// Duration of the simulation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thumbnail Scene", meta=(EditCondition = "bOverride_SimulateSceneTime"))
	float SimulateSceneTime;

	// The framerate of the scene simulation. Warning: A high framerate might cause hitching.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thumbnail Scene", meta=(EditCondition = "bOverride_SimulateSceneFramerate"))
	float SimulateSceneFramerate;

	// If simulation mode is Only Specified Components the component classes in this list will get simulated.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thumbnail Scene", meta=(EditCondition = "bOverride_ComponentsToSimulate"))
	TArray<TSubclassOf<UActorComponent>> ComponentsToSimulate;


	// Custom transform to apply to the thumbnail actor. 
	// Will not affect the framing of the camera, e.g. if the actor is moved/scaled/rotated the camera adjust accordingly to keep the actor in frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thumbnail Actor", meta=(EditCondition = "bOverride_CustomActorTransform"))
	FTransform CustomActorTransform;
	
	// Whether to adjust the Z position of the actor to align the bounds to the ground. Can be useful when using background worlds
	// and you want your actor to be standing on some form of back-drop.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thumbnail Actor", meta=(EditCondition = "bOverride_bSnapToFloor"))
	bool bSnapToFloor;

	// Components of this type will be ignored when calculating the actor bounding box for framing (Ignored when using custom camera transform)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thumbnail Actor", meta=(EditCondition = "bOverride_ComponentBoundsBlacklist"))
	TSet<TObjectPtr<UClass>> ComponentBoundsBlacklist;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Thumbnail Actor", meta=(EditCondition = "bOverride_bIncludeHiddenComponentsInBounds"))
	bool bIncludeHiddenComponentsInBounds;


	// The rotation of the thumbnail scene directional light.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_DirectionalLightRotation"))
	FRotator DirectionalLightRotation;

	// The intensity of the thumbnail scene directional light.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_DirectionalLightIntensity"))
	float DirectionalLightIntensity;

	// The color of the thumbnail scene directional light.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_DirectionalLightColor"))
	FLinearColor DirectionalLightColor;

    // The rotation of the thumbnail scene directional fill light.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (EditCondition = "bOverride_DirectionalFillLightRotation"))
    FRotator DirectionalFillLightRotation;

    // The intensity of the thumbnail scene directional fill light.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (EditCondition = "bOverride_DirectionalFillLightIntensity"))
    float DirectionalFillLightIntensity;

    // The color of the thumbnail scene directional fill light.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment", meta = (EditCondition = "bOverride_DirectionalFillLightColor"))
    FLinearColor DirectionalFillLightColor;

	// The intensity of the thumbnail scene sky light.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_SkyLightIntensity"))
	float SkyLightIntensity;

	// The color of the thumbnail scene directional light.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_SkyLightColor"))
	FLinearColor SkyLightColor;

	// Whether to render the cube map as an environment (usefull to disable when rendering with alpha)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_bShowEnvironment"))
	bool bShowEnvironment;

	// Whether the environment (the background) will affect the color of the object
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_bEnvironmentAffectLighting"))
	bool bEnvironmentAffectLighting;

	// The color of the thumbnail scene environment (sky sphere).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_EnvironmentColor"))
	FLinearColor EnvironmentColor;

	// The cube map to use for the thumbnail scene environment (sky sphere).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_EnvironmentCubeMap"))
	TSoftObjectPtr<UTextureCube> EnvironmentCubeMap;

	// The rotation (Yaw) of the thumbnail scene environment (sky sphere).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(EditCondition = "bOverride_EnvironmentRotation"))
	float EnvironmentRotation;

	// Post processing settings to use for the capture component.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Post Processing", meta=(EditCondition = "bOverride_PostProcessingSettings"))
	FPostProcessSettings PostProcessingSettings;

	// The actor to use as a sky sphere for the thumbnail scene (Default BP_ThumbnailGenerator_SkySphere). Set to None for no sky-sphere
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Advanced", meta=(EditCondition = "bOverride_ThumbnailSkySphere"))
	TSoftClassPtr<AActor> ThumbnailSkySphere;

	// A static script that will run before each thumbnail capture. Is useful for enabling certain global settings before a thumbnail is captured (An example would be enabling custom depth).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Advanced", meta=(EditCondition = "bOverride_ThumbnailGeneratorScripts"))
	TArray<TSubclassOf<UThumbnailGeneratorScript>> ThumbnailGeneratorScripts;

	// Whether to draw the bounds used to auto framing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Advanced", meta=(EditCondition = "bOverride_bDebugBounds"))
	bool bDebugBounds;

	FThumbnailSettings();

	static FThumbnailSettings MergeThumbnailSettings(const FThumbnailSettings& DefaultSettings, const FThumbnailSettings& OverrideSettings);

};

UENUM(BlueprintType)
enum class EBackgroundWorldLightMode : uint8
{
	ESpawnLights              UMETA(DisplayName="Spawn Lights"),                         // Create light sources at scene construction
	ESourceFromWorld          UMETA(DisplayName="Source Lights From World"),             // Look through the world for light sources to apply thumbnail settings onto
	ESourceSkyLight           UMETA(DisplayName="Source Sky Light From World"),          // Look through the world for a sky light but will leave directional lights as is
	ESourceDirectionalLights  UMETA(DisplayName="Source Directional Lights From World"), // Look through the world for directional lights but will leave sky light as is
	ESourceAvailableSpawnRest UMETA(DisplayName="Source Available, Spawn Rest"),         // Look through the world for light sources and will spawn any missing light
	EIgnoreLights             UMETA(DisplayName="Ignore Lights")                         // Leave lighting as is
};

USTRUCT(BlueprintType)
struct THUMBNAILGENERATOR_API FThumbnailBackgroundSceneSettings
{
	GENERATED_BODY()

	// If set, this world will be used as the background world for the thumbnail generator.
	// The background world cannot be changed at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Background World")
	TSoftObjectPtr<UWorld> BackgroundWorld;

	// If using custom background world, how do we wish to source the lights used for the thumbnail settings.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Background World")
	EBackgroundWorldLightMode SpawnLightsMode = EBackgroundWorldLightMode::ESourceAvailableSpawnRest;

	// Whether to spawn the thumbnail sky sphere when generating thumbnails.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Background World")
	bool bSpawnSkySphere = true;
};

UCLASS(config = Engine, DefaultConfig)
class THUMBNAILGENERATOR_API UThumbnailGeneratorSettings : public UObject
{
	GENERATED_BODY()
private:
	UPROPERTY()
	TArray<TObjectPtr<UObject>> AssetRefs;

public:

	UThumbnailGeneratorSettings();

	static UThumbnailGeneratorSettings* Get();

	// Default thumbnail settings to use when capturing thumbnails. (Overrides can be done on a per-setting basis in the Capture Thumbnail node).
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Thumbnail Generator")
	FThumbnailSettings DefaultThumbnailSettings;

	// Set a custom scene to use as background for your thumbnail capture. (Experimental feature)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Thumbnail Generator", meta=(DisplayName="Background Scene Settings (Experimental)"))
	FThumbnailBackgroundSceneSettings BackgroundSceneSettings;

	// The max size in MB that different sized render targets are allowed to occupy.
	// (Useful for controlling the momory usage of the Thumbnail Generator when capturing many thumbnails of differen sizes)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Thumbnail Generator", AdvancedDisplay)
	int32 MaxRenderTargetCacheSize = 50;

public:

	static const TArray<FName> &GetPresetList();

	static void ApplyPreset(FName Preset);
	
	static void ApplyDefaultPreset();
	static void ApplyDefaultNoBackgroundPreset();
	static void ApplyOutlinePreset();
	static void ApplyOutlineNoBackgroundPreset();
	static void ApplySilhuettPreset();
	static void ApplySilhuettNoBackgroundPreset();

private:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent);
#endif

};
