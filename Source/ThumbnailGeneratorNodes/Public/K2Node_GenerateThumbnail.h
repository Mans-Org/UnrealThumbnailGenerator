// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "K2Node_GenericCreateObject.h"
#include "K2Node_GenerateThumbnail.generated.h"

namespace K2Node_GenerateThumbnail
{
	const extern TCHAR* ThumbnailSettingsPinName;
	const extern TCHAR* ThumbnailOutputPinName;
	const extern TCHAR* PreCapturePinName;
}

UCLASS()
class THUMBNAILGENERATORNODES_API UK2Node_GenerateThumbnail : public UK2Node_ConstructObjectFromClass
{
	GENERATED_BODY()
protected:
	FNodeTextCache CachedNodeTitle;

public:
	UK2Node_GenerateThumbnail();

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const override;
	virtual FText GetMenuCategory() const override;
	//~ End UK2Node Interface

	//~ Begin UK2Node_ConstructObjectFromClass Interface
	virtual UClass* GetClassPinBaseClass() const;
	virtual bool IsSpawnVarPin(UEdGraphPin* Pin) const override;
	//~ End UK2Node_ConstructObjectFromClass Interface

	UEdGraphPin* GetThumbnailSettingsPin() const;
	UEdGraphPin* GetThumbnailOutputPin() const;
	UEdGraphPin* GetPreCaptureExecPin() const;

	/** Returns if the node uses World Object Context input */
	virtual bool UseWorldContext() const;

protected:
	void SetPinToolTip(class UEdGraphPin& MutatablePin, const FText& PinDescription) const;

};
