// Copyright Mans Isaksson. All Rights Reserved.

#include "K2Node_GenerateThumbnailAsync.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MakeMap.h"
#include "K2Node_DynamicCast.h"
#include "KismetCompiler.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintCompilationManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ThumbnailGenerator.h"

#define LOCTEXT_NAMESPACE "K2Node_GenerateThumbnailAsync"

namespace K2Node_GenerateThumbnail
{
	const TCHAR* CallbackPinName = TEXT("Callback");
}

void UK2Node_GenerateThumbnailAsync::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	// Add Callback Out Exec Pin
	FCreatePinParams PinParams;
	PinParams.Index = Pins.IndexOfByKey(GetThumbnailOutputPin()) - 1;
	UEdGraphPin* CallbackPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, K2Node_GenerateThumbnail::CallbackPinName, PinParams);
	CallbackPin->PinFriendlyName = LOCTEXT("CallbackPin_Name", "Callback");
	SetPinToolTip(*CallbackPin, LOCTEXT("CallbackPin_Description", "Executed once the thumbnail has been generated."));
}

FText UK2Node_GenerateThumbnailAsync::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		return LOCTEXT("AsyncGenerateThumbnail_BaseTitle", "Generate Thumbnail Async");
	}
	else if (UClass* ClassToSpawn = GetClassToSpawn())
	{
		if (CachedNodeTitle.IsOutOfDate(this))
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("ClassName"), ClassToSpawn->GetDisplayNameText());
			CachedNodeTitle.SetCachedText(FText::Format(LOCTEXT("AsyncGenerateThumbnail", "Generate {ClassName} Thumbnail Async"), Args), this);
		}
		return CachedNodeTitle;
	}
	return LOCTEXT("AsyncGenerateThumbnail_Title_NONE", "Generate Thumbnail Async");
}

FText UK2Node_GenerateThumbnailAsync::GetTooltipText() const
{
	return LOCTEXT("AsyncGenerateThumbnail_Tooltip", "Asynchronously generates a thumbnail for the selected actor class");
}

bool UK2Node_GenerateThumbnailAsync::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	// Can only place events in ubergraphs and macros (other code will help prevent macros with latents from ending up in functions)
	const EGraphType GraphType = TargetGraph->GetSchema()->GetGraphType(TargetGraph);
	return (GraphType == EGraphType::GT_Ubergraph || GraphType == EGraphType::GT_Macro) && Super::IsCompatibleWithGraph(TargetGraph);
}

void UK2Node_GenerateThumbnailAsync::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();
	check(Schema);
	bool bIsErrorFree = true;

	const static FName ActorClassClassParamName = TEXT("ActorClass");
	const static FName WorldContextParamName	= TEXT("WorldContextObject");
	const static FName ActorParamName           = TEXT("Actor");

	UEdGraphPin* const OriginalClassInPin             = GetClassPin();
	UEdGraphPin* const OriginalThumbnailSettingsInPin = GetThumbnailSettingsPin();
	UEdGraphPin* const OriginalClassPin               = GetClassPin();
	UEdGraphPin* const OriginalActorOutputPin         = GetResultPin();

	UClass* SpawnClass = (OriginalClassPin != nullptr) ? Cast<UClass>(OriginalClassPin->DefaultObject) : nullptr;
	if (!OriginalClassPin || (OriginalClassPin->LinkedTo.Num() == 0 && SpawnClass == nullptr))
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("GenerateThumbnail_Error", "Generate Thumbnail node @@ must have a @@ specified.").ToString(), this, OriginalClassPin);
		// we break exec links so this is the only error we get, don't want the SpawnActor node being considered and giving 'unexpected node' type warnings
		BreakAllNodeLinks();
		return;
	}

	// FUNCTION NODE
	const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_GenerateThumbnailAsync);
	UK2Node_CallFunction* const GenerateThumbnailFunctionNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);

	// Connect user function inputs
	{
		GenerateThumbnailFunctionNode->FunctionReference.SetExternalMember(FunctionName, UThumbnailGeneration::StaticClass());
		GenerateThumbnailFunctionNode->AllocateDefaultPins();

		UEdGraphPin* const FunctionNodeExecPin            = GenerateThumbnailFunctionNode->GetExecPin();
		UEdGraphPin* const FunctionNodeThenPin            = GenerateThumbnailFunctionNode->GetThenPin();
		UEdGraphPin* const FunctionClassInPin             = GenerateThumbnailFunctionNode->FindPinChecked(ActorClassClassParamName);
		UEdGraphPin* const FunctionThumbnailSettingsInPin = GenerateThumbnailFunctionNode->FindPinChecked(K2Node_GenerateThumbnail::ThumbnailSettingsPinName);

		// Connect Original Exec pin to function Exec input
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *FunctionNodeExecPin).CanSafeConnect();

		// Connect Original Then pin to function Then input
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *FunctionNodeThenPin).CanSafeConnect();

		// Connect Original Class input to function Class input
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*OriginalClassInPin, *FunctionClassInPin).CanSafeConnect();

		// Connect Original ThumbnailSettings input to function ThumbnailSettings input
		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*OriginalThumbnailSettingsInPin, *FunctionThumbnailSettingsInPin).CanSafeConnect();
	}

	// Uses K2Node_LoadAsset as reference, look into that function for a more generic approach
	const auto ExpandCallbackEvent = [&](const FName& FunctionDelegateInputPinName, const FName& CallbackEventParamPinName, 
		const FName& NodeOutExecPinName, const FName& NodeOutValuePinName)
	{
		UEdGraphPin* DelegatePropertyPin     = GenerateThumbnailFunctionNode->FindPinChecked(FunctionDelegateInputPinName);
		UK2Node_CustomEvent* CustomEventNode = CompilerContext.SpawnIntermediateNode<UK2Node_CustomEvent>(this, SourceGraph);
		CustomEventNode->CustomFunctionName  = *FString::Printf(TEXT("%s_%s"), *FunctionDelegateInputPinName.ToString(), *CompilerContext.GetGuid(this));
		
		CustomEventNode->AllocateDefaultPins();
		{
			UFunction* InlineSerializeFunction          = GenerateThumbnailFunctionNode->GetTargetFunction();
			FDelegateProperty* FunctionDelegateProperty = InlineSerializeFunction ? FindFProperty<FDelegateProperty>(InlineSerializeFunction, FunctionDelegateInputPinName) : nullptr;
			UFunction* FunctionDelegateSignature        = FunctionDelegateProperty ? FunctionDelegateProperty->SignatureFunction : nullptr;
			
			ensure(FunctionDelegateSignature);
			
			if (FunctionDelegateSignature && CallbackEventParamPinName != NAME_None && NodeOutValuePinName != NAME_None)
			{
				const FProperty* Param = FunctionDelegateSignature->FindPropertyByName(CallbackEventParamPinName);
				
				ensure(Param && !(Param->HasAnyPropertyFlags(CPF_OutParm) && !Param->HasAnyPropertyFlags(CPF_ReferenceParm)) && !Param->HasAnyPropertyFlags(CPF_ReturnParm));
				
				FEdGraphPinType PinType;
				bIsErrorFree &= Schema->ConvertPropertyToPinType(Param, PinType);
				bIsErrorFree &= (nullptr != CustomEventNode->CreateUserDefinedPin(Param->GetFName(), PinType, EGPD_Output));
			}
		}

		// Connect Custom Event Delegate Pin to Function Delegate Input
		{
			UEdGraphPin* CustomEventDelegatePin = CustomEventNode->FindPinChecked(UK2Node_CustomEvent::DelegateOutputName);
			bIsErrorFree &= Schema->TryCreateConnection(DelegatePropertyPin, CustomEventDelegatePin);
		}

		// Connect Custom Event output to main node value output
		if (CallbackEventParamPinName != NAME_None && NodeOutValuePinName != NAME_None)
		{
			UEdGraphPin* CustomEventParamPin = CustomEventNode->FindPinChecked(CallbackEventParamPinName);
			UEdGraphPin* NodeOutputParamPin  = FindPinChecked(NodeOutValuePinName);

			if (NodeOutputParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
				&& NodeOutputParamPin->PinType.PinSubCategoryObject != CustomEventParamPin->PinType.PinSubCategoryObject) // Needs casting
			{
				UK2Node_DynamicCast* CastNode = CompilerContext.SpawnIntermediateNode<UK2Node_DynamicCast>(this, SourceGraph);
				CastNode->SetPurity(true);
				CastNode->TargetType = CastChecked<UClass>(NodeOutputParamPin->PinType.PinSubCategoryObject.Get());
				CastNode->AllocateDefaultPins();

				bIsErrorFree &= Schema->TryCreateConnection(CustomEventParamPin, CastNode->GetCastSourcePin());
				bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*NodeOutputParamPin, *CastNode->GetCastResultPin()).CanSafeConnect();
			}
			else
			{
				bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*NodeOutputParamPin, *CustomEventParamPin).CanSafeConnect();
			}
		}

		// Connect Custom Event exec output main node exec output
		{
			UEdGraphPin* CustomEventExecPin = CustomEventNode->FindPin(UEdGraphSchema_K2::PN_Then);
			UEdGraphPin* NodeOutputExecPin  = FindPinChecked(NodeOutExecPinName);
			bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*NodeOutputExecPin, *CustomEventExecPin).CanSafeConnect();
		}
	};

	// Callback event
	ExpandCallbackEvent(TEXT("Callback"), TEXT("Thumbnail"), K2Node_GenerateThumbnail::CallbackPinName, K2Node_GenerateThumbnail::ThumbnailOutputPinName);

	// PreCapture event
	ExpandCallbackEvent(TEXT("PreCaptureThumbnail"), TEXT("Actor"), K2Node_GenerateThumbnail::PreCapturePinName, *OriginalActorOutputPin->GetName());

	// Hook up properties exposed on spawn
	{
		static const FName FunctionPropertiesInputName(TEXT("Properties"));
		static const FName PropertyExporterPropertyInputName(TEXT("Property"));
		static const FName PropertyExporterReturnValueName(UEdGraphSchema_K2::PN_ReturnValue);

		UClass* ClassToSpawn = GetClassToSpawn();

		const auto CheckIsValidSpawnVarPin = [&](UEdGraphPin* InPin)
		{
			const bool bHasDefaultValue = !InPin->DefaultValue.IsEmpty() || !InPin->DefaultTextValue.IsEmpty() || InPin->DefaultObject;
			if (InPin->LinkedTo.Num() > 0 || bHasDefaultValue) // Only export property value if this pin is linked to something
			{
				if (InPin->LinkedTo.Num() == 0)
				{
					FProperty* Property = FindFProperty<FProperty>(ClassToSpawn, InPin->PinName);
					if (!Property) // NULL property indicates that this pin was part of the original node, not the class we're assigning to
						return false;

					// We don't want to copy the property value unless the default value differs from the value in the CDO
					FString DefaultValueAsString;

					if (FBlueprintCompilationManager::GetDefaultValue(ClassToSpawn, Property, DefaultValueAsString))
					{
						if (Schema->DoesDefaultValueMatch(*InPin, DefaultValueAsString))
							return false;
					}
					else if (ClassToSpawn->GetDefaultObject<UObject>())
					{
						FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<uint8*>(ClassToSpawn->GetDefaultObject<UObject>()), DefaultValueAsString);
						if (DefaultValueAsString == InPin->GetDefaultAsString())
							return false;
					}
				}

				return true;
			}

			return false;
		};

		const auto SelectPropertyExportTextFunction = [](UEdGraphPin* InPin)->FName
		{
			switch (InPin->PinType.ContainerType)
			{
			case EPinContainerType::None:
				return GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_ExportPropertyText);
			case EPinContainerType::Array:
				return GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_ExportArrayPropertyText);
			case EPinContainerType::Set:
				return GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_ExportSetPropertyText);
			case EPinContainerType::Map:
				return GET_FUNCTION_NAME_CHECKED(UThumbnailGeneration, K2_ExportMapPropertyText);
			default:
				checkf(0, TEXT("Pin Type Export Function not defined"));
			}

			return NAME_None;
		};

		const auto GetMakeMapIndexNames = [](int32 PinIndex)->TPair<FName, FName>
		{
			// Look at UK2Node_MakeMap::GetPinName for reference
			const int32 KeyIndex   = PinIndex;
			const int32 ValueIndex = ((PinIndex * 2) + 1) / 2;
			return TPair<FName, FName>(*FString::Printf(TEXT("Key %d"), KeyIndex), *FString::Printf(TEXT("Value %d"), ValueIndex));
		};

		UK2Node_MakeMap* MakeMapNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeMap>(this, SourceGraph);
		MakeMapNode->NumInputs = 0;
		MakeMapNode->AllocateDefaultPins();

		UEdGraphPin* const MapOut = MakeMapNode->GetOutputPin();

		// Connect the output of the "Make Map" pin to the function's "Properties" pin
		MapOut->MakeLinkTo(GenerateThumbnailFunctionNode->FindPinChecked(FunctionPropertiesInputName));
		// This will set the "Make Map" node's type, only works if one pin is connected.
		MakeMapNode->PinConnectionListChanged(MapOut); 

		int32 ArgIdx = 0;
		// Create 'export property text' nodes and hook them up
		for (int32 PinIdx = 0; PinIdx < Pins.Num(); PinIdx++)
		{
			UEdGraphPin* Pin = Pins[PinIdx];
			if (!CheckIsValidSpawnVarPin(Pin))
				continue;

			MakeMapNode->AddInputPin();

			const auto PinNames        = GetMakeMapIndexNames(ArgIdx);
			UEdGraphPin* KeyInputPin   = MakeMapNode->FindPinChecked(PinNames.Key);
			UEdGraphPin* ValueInputPin = MakeMapNode->FindPinChecked(PinNames.Value);

			KeyInputPin->DefaultValue = Pin->PinName.ToString();

			// If the pin is connected, export the value of the connected property using ExportTextFunctionNode
			if (Pin->LinkedTo.Num() > 0)
			{
				const FName ExportTextFunctionName = SelectPropertyExportTextFunction(Pin);
				UK2Node_CallFunction* const ExportTextFunctionNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				ExportTextFunctionNode->FunctionReference.SetExternalMember(ExportTextFunctionName, UThumbnailGeneration::StaticClass());
				ExportTextFunctionNode->AllocateDefaultPins();

				UEdGraphPin* PropertyInputPin       = ExportTextFunctionNode->FindPinChecked(PropertyExporterPropertyInputName);
				UEdGraphPin* PropertyValueOutputPin = ExportTextFunctionNode->FindPinChecked(PropertyExporterReturnValueName);

				// Connect Property input
				bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*Pin, *PropertyInputPin).CanSafeConnect();
				ExportTextFunctionNode->PinConnectionListChanged(PropertyInputPin);
				
				PropertyValueOutputPin->MakeLinkTo(ValueInputPin);
			}
			else
			{
				ValueInputPin->DefaultValue = Pin->GetDefaultAsString();
			}

			ArgIdx++;
		}
	}

	if (!bIsErrorFree)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("InternalConnectionError", "UK2Node_GenerateThumbnailAsync: Internal connection error. @@").ToString(), this);
	}

	BreakAllNodeLinks();
}

FName UK2Node_GenerateThumbnailAsync::GetCornerIcon() const
{
	return TEXT("Graph.Latent.LatentIcon");
}

bool UK2Node_GenerateThumbnailAsync::IsSpawnVarPin(UEdGraphPin* Pin) const
{
	return Super::IsSpawnVarPin(Pin) &&
		Pin->PinName != K2Node_GenerateThumbnail::CallbackPinName;
}

#undef LOCTEXT_NAMESPACE