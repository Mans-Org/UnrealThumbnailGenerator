// Copyright Mans Isaksson. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "K2Node_GenerateThumbnail.h"
#include "K2Node_GenerateThumbnailAsync.generated.h"

UCLASS()
class THUMBNAILGENERATORNODES_API UK2Node_GenerateThumbnailAsync : public UK2Node_GenerateThumbnail
{
	GENERATED_BODY()

public:

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual FName GetCornerIcon() const override;
	//~ End UK2Node Interface

	//~ Begin UK2Node_ConstructObjectFromClass Interface
	virtual bool IsSpawnVarPin(UEdGraphPin* Pin) const override;
	//~ End UK2Node_ConstructObjectFromClass Interface

};
