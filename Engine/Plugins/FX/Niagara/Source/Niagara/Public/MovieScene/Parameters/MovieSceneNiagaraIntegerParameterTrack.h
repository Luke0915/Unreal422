// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieScene/Parameters/MovieSceneNiagaraParameterTrack.h"
#include "MovieSceneNiagaraIntegerParameterTrack.generated.h"

/** A track for animating integer niagara parameters. */
UCLASS(MinimalAPI)
class UMovieSceneNiagaraIntegerParameterTrack : public UMovieSceneNiagaraParameterTrack
{
	GENERATED_BODY()

public:
	/** UMovieSceneTrack interface. */
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
};