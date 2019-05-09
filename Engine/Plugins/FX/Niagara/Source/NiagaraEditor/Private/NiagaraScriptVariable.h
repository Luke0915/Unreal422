#include "NiagaraTypes.h"
#include "NiagaraScriptVariable.generated.h"

UCLASS()
class UNiagaraScriptVariable : public UObject
{
public:
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	FNiagaraVariable Variable;

	UPROPERTY(EditAnywhere, Category = Metadata)
	FNiagaraVariableMetaData Metadata;
};