// Copyright Mans Isaksson. All Rights Reserved.

#include "K2Node_GenerateThumbnail.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_CallArrayFunction.h"
#include "KismetCompiler.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintCompilationManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ThumbnailGenerator.h"
#include "ThumbnailGeneratorCompatibilityLayer.h"

#define LOCTEXT_NAMESPACE "K2Node_GenerateThumbnail"

namespace K2Node_GenerateThumbnail
{
	const TCHAR* ThumbnailSettingsPinName = TEXT("ThumbnailSettings");
	const TCHAR* ThumbnailOutputPinName   = TEXT("Thumbnail");
	const TCHAR* PreCapturePinName        = TEXT("PreCaptureThumbnail");
}

UK2Node_GenerateThumbnail::UK2Node_GenerateThumbnail()
{
	AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
}

void UK2Node_GenerateThumbnail::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	// Add In Actor Class Pin
	UEdGraphPin* ActorClassPin = GetClassPin();
	ActorClassPin->PinFriendlyName = LOCTEXT("ActorClass_Name", "Actor Class");
	SetPinToolTip(*ActorClassPin, LOCTEXT("ActorClassPin_Description", "The actor class of which a thumbnail will be generated."));

	// Add In Thumbnail Settings pin
	UEdGraphPin* ThumbnailSettingsPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FThumbnailSettings::StaticStruct(), K2Node_GenerateThumbnail::ThumbnailSettingsPinName);
	ThumbnailSettingsPin->PinFriendlyName = LOCTEXT("ThumbnailSettingsPin_Name", "Thumbnail Settings");
	SetPinToolTip(*ThumbnailSettingsPin, LOCTEXT("ThumbnailSettingsPin_Description", "This struct can be used to override individual Thumbnail Settings for this capture."));

	// Add Thumbnail Output Pin
	UEdGraphPin* ThumbnailOutputPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, UTexture2D::StaticClass(), K2Node_GenerateThumbnail::ThumbnailOutputPinName);
	ThumbnailOutputPin->PinFriendlyName = LOCTEXT("ThumbnailOutputPin_Name", "Thumbnail");
	SetPinToolTip(*ThumbnailOutputPin, LOCTEXT("ThumbnailOutputPin_Description", "The generated UTexture2D object (null if thumbnail failed to generate)"));

	// Add PreCapture Out Exec Pin
	UEdGraphPin* PreCapturePin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, K2Node_GenerateThumbnail::PreCapturePinName);
	PreCapturePin->PinFriendlyName = LOCTEXT("PreCapturePin_Name", "Pre Capture");
	PreCapturePin->bAdvancedView   = true;
	SetPinToolTip(*PreCapturePin, LOCTEXT("PreCapturePin_Description", "Right before the thumbnail is captured of the actor."));

	// Add PreCaptureActor Output Pin
	UEdGraphPin* PreCaptureActorOutputPin = GetResultPin();
	PreCaptureActorOutputPin->PinFriendlyName = LOCTEXT("PreCaptureActorOutputPin_Name", "Actor");
	PreCaptureActorOutputPin->bAdvancedView   = true;
	SetPinToolTip(*PreCaptureActorOutputPin, LOCTEXT("PreCaptureActorOutputPin_Description", "Reference to the actor for which the thumbnail is being generated"));
	
	// Move the Actor output pin to the end
	const int32 ActorOutputPinIndex = Pins.IndexOfByKey(PreCaptureActorOutputPin);
	Pins.RemoveAt(ActorOutputPinIndex, 1, EAllowShrinking::No);
	Pins.Add(PreCaptureActorOutputPin);
}

FText UK2Node_GenerateThumbnail::GetTooltipText() const
{
	return LOCTEXT("GenerateThumbnail_Tooltip", "Generates a thumbnail for the selected actor class");
}

FText UK2Node_GenerateThumbnail::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		return LOCTEXT("GenerateThumbnail_BaseTitle", "Generate Thumbnail");
	}
	else if (UClass* ClassToSpawn = GetClassToSpawn())
	{
		if (CachedNodeTitle.IsOutOfDate(this))
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("ClassName"), ClassToSpawn->GetDisplayNameText());
			CachedNodeTitle.SetCachedText(FText::Format(LOCTEXT("GenerateThumbnail", "Generate {ClassName} Thumbnail"), Args), this);
		}
		return CachedNodeTitle;
	}
	return LOCTEXT("GenerateThumbnail_Title_NONE", "Generate Thumbnail");
}

void UK2Node_GenerateThumbnail::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const static FName BeginGenerateThumbnailFuncName       = GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_BeginGenerateThumbnail);
	const static FName FinishGenerateThumbnailFuncName      = GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_FinishGenerateThumbnail);
	const static FName FinishSpawningThumbnailActorFuncName = GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_FinishSpawningThumbnailActor);
	const static FName FinalizeThumbnailSettingsFuncName    = GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_FinalizeThumbnailSettings);
	const static FName ActorClassClassParamName             = TEXT("ActorClass");
	const static FName ActorParamName                       = TEXT("Actor");

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();
	check(Schema);

	bool bIsErrorFree = true;

	UEdGraphPin* const ExecPin                 = GetExecPin();
	UEdGraphPin* const ThenPin                 = GetThenPin();
	UEdGraphPin* const ThumbnailSettingsInPin  = GetThumbnailSettingsPin();
	UEdGraphPin* const ClassInPin              = GetClassPin();
	UEdGraphPin* const ActorOutPin             = GetResultPin();
	UEdGraphPin* const ThumbnailOutPin         = GetThumbnailOutputPin();
	UEdGraphPin* const PreCaptureExecOutPin    = GetPreCaptureExecPin();

	// Validate Class input Pin
	UClass* SpawnClass = (ClassInPin != nullptr) ? Cast<UClass>(ClassInPin->DefaultObject) : nullptr;
	if (!ClassInPin || (ClassInPin->LinkedTo.Num() == 0 && SpawnClass == nullptr))
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("GenerateThumbnail_Error", "Generate Thumbnail node @@ must have a @@ specified.").ToString(), this, ClassInPin);
		BreakAllNodeLinks();
		return;
	}

	// Spawn BeginGenerateThumbnail Node
	UK2Node_CallFunction* BeginGenerateNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	BeginGenerateNode->FunctionReference.SetExternalMember(BeginGenerateThumbnailFuncName, UThumbnailGeneration::StaticClass());
	BeginGenerateNode->AllocateDefaultPins();

	UEdGraphPin* const BeginGenerateExecPin                = BeginGenerateNode->GetExecPin();
	UEdGraphPin* const BeginGenerateThenPin                = BeginGenerateNode->GetThenPin();
	UEdGraphPin* const BeginGenerateClassInPin             = BeginGenerateNode->FindPinChecked(ActorClassClassParamName);
	UEdGraphPin* const BeginGenerateThumbnailSettingsInPin = BeginGenerateNode->FindPinChecked(K2Node_GenerateThumbnail::ThumbnailSettingsPinName);
	UEdGraphPin* const BeginGenerateActorOutPin            = BeginGenerateNode->GetReturnValuePin();

	// Spawn FinishGenerateThumbnail Node
	UK2Node_CallFunction* FinishGenerateNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	FinishGenerateNode->FunctionReference.SetExternalMember(FinishGenerateThumbnailFuncName, UThumbnailGeneration::StaticClass());
	FinishGenerateNode->AllocateDefaultPins();

	UEdGraphPin* const FinishGenerateExecPin                = FinishGenerateNode->GetExecPin();
	UEdGraphPin* const FinishGenerateThenPin                = FinishGenerateNode->GetThenPin();
	UEdGraphPin* const FinishGenerateActorInPin             = FinishGenerateNode->FindPinChecked(ActorParamName);
	UEdGraphPin* const FinishGenerateThumbnailSettingsInPin = FinishGenerateNode->FindPinChecked(K2Node_GenerateThumbnail::ThumbnailSettingsPinName);
	UEdGraphPin* const FinishGenerateThumbnailOutPin        = FinishGenerateNode->GetReturnValuePin();

	// Spawn FinishSpawningThumbnailActor Node
	UK2Node_CallFunction* FinishSpawningThumbnailActorNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	FinishSpawningThumbnailActorNode->FunctionReference.SetExternalMember(FinishSpawningThumbnailActorFuncName, UThumbnailGeneration::StaticClass());
	FinishSpawningThumbnailActorNode->AllocateDefaultPins();

	UEdGraphPin* const FinishSpawningExecPin = FinishSpawningThumbnailActorNode->GetExecPin();
	UEdGraphPin* const FinishSpawningThenPin = FinishSpawningThumbnailActorNode->GetThenPin();
	UEdGraphPin* const FinishSpawningActorInPin = FinishSpawningThumbnailActorNode->FindPinChecked(ActorParamName);

	// Spawn FinalizeSettings Node
	UK2Node_CallFunction* FinalizeSettingsNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	FinalizeSettingsNode->FunctionReference.SetExternalMember(FinalizeThumbnailSettingsFuncName, UThumbnailGeneration::StaticClass());
	FinalizeSettingsNode->AllocateDefaultPins();

	UEdGraphPin* const FinalizeSettingsThumbnailSettingsInPin = FinalizeSettingsNode->FindPinChecked(K2Node_GenerateThumbnail::ThumbnailSettingsPinName);
	UEdGraphPin* const FinalizeSettingsThumbnailSettingsOutPin = FinalizeSettingsNode->GetReturnValuePin();

	UK2Node_ExecutionSequence* SequenceNode = CompilerContext.SpawnIntermediateNode<UK2Node_ExecutionSequence>(this, SourceGraph);
	SequenceNode->AllocateDefaultPins();

	UEdGraphPin* const SequenceExecPin  = SequenceNode->GetExecPin();
	UEdGraphPin* const SequenceThenPin1 = SequenceNode->GetThenPinGivenIndex(0);
	UEdGraphPin* const SequenceThenPin2 = SequenceNode->GetThenPinGivenIndex(1);
	
	// Move Original Exec pin to BeginGenerateThumbnail Exec input
	bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*ExecPin, *BeginGenerateExecPin).CanSafeConnect();

	// Move/Copy Original Class input to function Class input
	if (ClassInPin->HasAnyConnections())
	{
		// Copy the 'blueprint' connection from the spawn node to 'begin spawn'
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*ClassInPin, *BeginGenerateClassInPin).CanSafeConnect();
	}
	else
	{
		// Copy blueprint literal onto begin spawn call
		BeginGenerateClassInPin->DefaultObject = SpawnClass;
	}
	
	if (ThumbnailSettingsInPin->HasAnyConnections())
	{
		// Move Original ThumbnailSettings input to function FinalizeSettings ThumbnailSettings input
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*ThumbnailSettingsInPin, *FinalizeSettingsThumbnailSettingsInPin).CanSafeConnect();
	}
	else
	{
		FinalizeSettingsThumbnailSettingsInPin->DefaultObject = ThumbnailSettingsInPin->DefaultObject;
	}
	
	// Connect FinalizeSettings output to BeginGenerateThumbnail ThumbnailSettings input
	bIsErrorFree &= Schema->TryCreateConnection(FinalizeSettingsThumbnailSettingsOutPin, BeginGenerateThumbnailSettingsInPin);
	
	// Connect FinalizeSettings output to FinishGenerateThumbnail ThumbnailSettings input
	bIsErrorFree &= Schema->TryCreateConnection(FinalizeSettingsThumbnailSettingsOutPin, FinishGenerateThumbnailSettingsInPin);

	// Move Original Then pin to FinishGenerateThumbnail Exec input
	bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*ThenPin, *FinishGenerateThenPin).CanSafeConnect();

	// Move PreCaptureExec pin connections to Sequence Then 1
	if (PreCaptureExecOutPin->HasAnyConnections())
	{
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*PreCaptureExecOutPin, *SequenceThenPin1).CanSafeConnect();
	}

	// Connect Sequence Then 2 to FinishGenerateThumbnail Exec
	bIsErrorFree &= Schema->TryCreateConnection(SequenceThenPin2, FinishGenerateExecPin);

	// Connect BeginGenerateThumbnail ActorOutput to FinishGenerateThumbnail Actor input
	bIsErrorFree &= Schema->TryCreateConnection(BeginGenerateActorOutPin, FinishGenerateActorInPin);

	// Move ActorOutput pin connections to BeginGenerateThumbnail ActorOutput
	if (ActorOutPin->HasAnyConnections())
	{
		BeginGenerateActorOutPin->PinType = ActorOutPin->PinType; // Copy type so it uses the right actor subclass
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*ActorOutPin, *BeginGenerateActorOutPin).CanSafeConnect();
	}

	// Move ThumbnailOutput pin connections to FinishGenerateThumbnail output pin
	if (ThumbnailOutPin->HasAnyConnections())
	{
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*ThumbnailOutPin, *FinishGenerateThumbnailOutPin).CanSafeConnect();
	}

	// Generates assignment nodes for each exposed proerty 
	// IMPORTANT: This has to be run LAST as it will look at all the remaining pins which has any connections.
	UEdGraphPin* LastThen = FKismetCompilerUtilities::GenerateAssignmentNodes(CompilerContext, SourceGraph, BeginGenerateNode, this, BeginGenerateActorOutPin, SpawnClass);

	// Connect last assignment node Then with FinishSpawningThumbnailActor node
	bIsErrorFree &= Schema->TryCreateConnection(LastThen, FinishSpawningExecPin);

	// Connect BeginGenerateThumbnail actor output to FinishSpawningThumbnailActor actor input
	bIsErrorFree &= Schema->TryCreateConnection(BeginGenerateActorOutPin, FinishSpawningActorInPin);

	// Connect FinishSpawningThumbnailActor Then with Sequence Exec
	bIsErrorFree &= Schema->TryCreateConnection(FinishSpawningThenPin, SequenceExecPin);

	if (!bIsErrorFree)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("InternalConnectionError", "UK2Node_GenerateThumbnail: Internal connection error. @@").ToString(), this);
	}

	BreakAllNodeLinks();
}

bool UK2Node_GenerateThumbnail::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const
{
	const UBlueprint* SourceBlueprint = GetBlueprint();
	const bool bThumbnailGeneratorClassResult = UThumbnailGeneration::StaticClass()->ClassGeneratedBy != SourceBlueprint;
	if (bThumbnailGeneratorClassResult && OptionalOutput)
	{
		OptionalOutput->AddUnique(UThumbnailGeneration::StaticClass());
	}
	const bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return bSuperResult || bThumbnailGeneratorClassResult;
}

FText UK2Node_GenerateThumbnail::GetMenuCategory() const
{
	return LOCTEXT("GenerateThumbnail_MenuCategory", "Thumbnail Generation");
}

UClass* UK2Node_GenerateThumbnail::GetClassPinBaseClass() const
{
	return AActor::StaticClass();
}

bool UK2Node_GenerateThumbnail::IsSpawnVarPin(UEdGraphPin* Pin) const
{
	return Super::IsSpawnVarPin(Pin) &&
			Pin->PinName != K2Node_GenerateThumbnail::ThumbnailSettingsPinName &&
			Pin->PinName != K2Node_GenerateThumbnail::ThumbnailOutputPinName &&
			Pin->PinName != K2Node_GenerateThumbnail::PreCapturePinName;
}

UEdGraphPin* UK2Node_GenerateThumbnail::GetThumbnailSettingsPin() const
{
	UEdGraphPin* Pin = FindPinChecked(K2Node_GenerateThumbnail::ThumbnailSettingsPinName);
	check(Pin->Direction == EGPD_Input);
	return Pin;
}

UEdGraphPin* UK2Node_GenerateThumbnail::GetThumbnailOutputPin() const
{
	UEdGraphPin* Pin = FindPin(K2Node_GenerateThumbnail::ThumbnailOutputPinName);
	check(Pin == nullptr || Pin->Direction == EGPD_Output); // If pin exists, it must be output
	return Pin;
}

UEdGraphPin* UK2Node_GenerateThumbnail::GetPreCaptureExecPin() const
{
	UEdGraphPin* Pin = FindPin(K2Node_GenerateThumbnail::PreCapturePinName);
	check(Pin == nullptr || Pin->Direction == EGPD_Output); // If pin exists, it must be output
	return Pin;
}

bool UK2Node_GenerateThumbnail::UseWorldContext() const
{
	return false;
}

void UK2Node_GenerateThumbnail::SetPinToolTip(UEdGraphPin& MutatablePin, const FText& PinDescription) const
{
	MutatablePin.PinToolTip = UEdGraphSchema_K2::TypeToText(MutatablePin.PinType).ToString();

	UEdGraphSchema_K2 const* const K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
	if (K2Schema != nullptr)
	{
		MutatablePin.PinToolTip += TEXT(" ");
		MutatablePin.PinToolTip += K2Schema->GetPinDisplayName(&MutatablePin).ToString();
	}

	MutatablePin.PinToolTip += FString(TEXT("\n")) + PinDescription.ToString();
}

#undef LOCTEXT_NAMESPACE