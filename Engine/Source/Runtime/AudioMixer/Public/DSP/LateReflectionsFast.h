// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/IntegerDelay.h"
#include "DSP/LongDelayAPF.h"
#include "DSP/BufferOnePoleLPF.h"
#include "DSP/DynamicDelayAPF.h"

namespace Audio
{
	// Structure to hold the various delay line tap outputs produced from a plate.
	struct AUDIOMIXER_API FLateReflectionsPlateOutputs
	{
		// Number of tap locations in plate reverb
		static const int32 NumTaps = 7;

		// Tap sample buffers.
		AlignedFloatBuffer Taps[NumTaps];

		// Output sample buffer.
		AlignedFloatBuffer Output;

		// Resizes and zero all buffers in structure.
		void ResizeAndZero(int32 InNumSamples);
	};

	// Delay line settings for reverb plate delay
	// Tap locations are determined by analyzing these delay values. 
	struct AUDIOMIXER_API FLateReflectionsPlateDelays
	{
		/*
			Setting the delay values affects the tap locations. The audio flowchart below shows the
			relationship between delay values and tap locations. 

			+---------------+
			|    Input      |
			+------+--------+
				   |
			+------v--------+
			| Modulated APF |
			+------+--------+
				   |
			+------+-------+
			|    DelayA    |
			+------+-------+  +---------+
				   |--------->+  Tap 0  |
			+------v-------+  +---------+
			|    DelayB    |
			+------+-------+  +---------+
				   |---------->  Tap 1  |
			+------v-------+  +---------+
			|    DelayC    |
			+------+-------+  +---------+
				   |---------->  Tap 2  |
			+------v-------+  +---------+
			|    DelayD    |
			+------+-------+
				   |
			+------v--------+
			|     LPF       |
			+------+--------+
				   |
			+------v-------+  +---------+  +---------+  +---------+  +--------+
			|     APF      +--> DelayE  +--> |Tap 3  +--> DelayF  +-->  Tap 4 |
			+------+-------+  +---------+  +---------+  +---------+  +--------+
				   |
			+------v-------+
			|    DelayG    |
			+------+-------+  +---------+
				   |---------->  Tap 5  |
			+------v-------+  +---------+
			|    DelayH    |
			+------+-------+  +---------+
				   |--------->+  Tap 6  |
			+------v-------+  +---------+
			|    DelayI    |
			+------+-------+
				   |
			+------v-------+
			|    Output    |
			+--------------+
		 */

		// Base delay samples for modulated delay
		int32 NumSamplesModulatedBase;
		// Max delta samples for modulated delay
		int32 NumSamplesModulatedDelta;
		// Tap 0 exists at the output of DelayA
		int32 NumSamplesDelayA;
		// Tap 1 exists at the output of DelayB
		int32 NumSamplesDelayB;
		// Tap 2 exists at the output of DelayC
		int32 NumSamplesDelayC;
		// Delay line length
		int32 NumSamplesDelayD;
		// All pass filter length
		int32 NumSamplesAPF;
		// Tap 3 exists at the output of DelayE
		int32 NumSamplesDelayE;
		// Tap 4 exists at the output of DelayF
		int32 NumSamplesDelayF;
		// Tap 5 exists at the output of DelayG
		int32 NumSamplesDelayG;
		// Tap 6 exists at the output of DelayH
		int32 NumSamplesDelayH;
		// Output tap exists at the output of DelayI
		int32 NumSamplesDelayI;

		// Default delay settings for left channel.
		static FLateReflectionsPlateDelays DefaultLeftDelays(float InSampleRate);

		// Default delay settings for right channel.
		static FLateReflectionsPlateDelays DefaultRightDelays(float InSampleRate);
	};


	// Single plate channel for plate reverberation.
	class AUDIOMIXER_API FLateReflectionsPlate {
	public:
		// InMaxNumInternalBufferSamples sets the mmaximum number of samples in an internal buffer.
		FLateReflectionsPlate(
			float InSampleRate,
			int32 InMaxNumInternalBufferSamples,
			const FLateReflectionsPlateDelays& InDelays);

		~FLateReflectionsPlate();

		// Processing input samples. All input buffers must be of equal length.
		// InSamples contains previously unseen audio samples.
		// InFeedbackSamples contains the delay line output from the other plate.
		// InDelayModulations contains the fractional delay values per a sample to modulate the first all pass filters delay line.
		// OutPlateSamples is filled with audio from the various tap points and delay line output.
		void ProcessAudioFrames(
			const AlignedFloatBuffer& InSamples,
			const AlignedFloatBuffer& InFeedbackSamples,
			const AlignedFloatBuffer& InDelayModulations,
			FLateReflectionsPlateOutputs& OutPlateSamples);

		void SetDensity(float InDensity);
		void SetDampening(float InDampening);
		void SetDecay(float InDecay);

		// The number of samples in an internal buffer. This describes the limit of how many values
		// are accessible via PeekDelayLineOutput(...)
		int32 GetNumInternalBufferSamples() const;

		// Retrieve the internal delay line data. The number of samples retrieved is limited to the
		// number of samples in the internal buffer. That amount can be determined by calling
		// "GetNumInternalBufferSamples()".
		void PeekDelayLine(int32 InNum, AlignedFloatBuffer& OutSamples);

	protected:
		// Current parameter settings of reverb
		float SampleRate;
		int32 NumInternalBufferSamples;
		float Dampening;
		float Decay;
		float Density;

		// Sample delay values
		FLateReflectionsPlateDelays PlateDelays;

		// Sample rate used for hard-coded delay line values
		static const int32 PresetSampleRate = 29761;

		TUniquePtr<FDynamicDelayAPF> ModulatedAPF;
		TUniquePtr<FIntegerDelay> DelayA;
		TUniquePtr<FIntegerDelay> DelayB;
		TUniquePtr<FIntegerDelay> DelayC;
		TUniquePtr<FIntegerDelay> DelayD;
		TUniquePtr<FBufferOnePoleLPF> LPF;
		TUniquePtr<FLongDelayAPF> APF;
		TUniquePtr<FIntegerDelay> DelayE;
		TUniquePtr<FIntegerDelay> DelayF;
		TUniquePtr<FIntegerDelay> DelayG;
		TUniquePtr<FIntegerDelay> DelayH;
		TUniquePtr<FIntegerDelay> DelayI;

		Audio::AlignedFloatBuffer WorkBufferA;
		Audio::AlignedFloatBuffer WorkBufferB;
		Audio::AlignedFloatBuffer WorkBufferC;
	};

	// Settings for controlling the FLateReflections
	struct AUDIOMIXER_API FLateReflectionsFastSettings
	{
		// Milliseconds for the predelay
		float LateDelayMsec;

		// Initial attenuation of audio after it leaves the predelay
		float LateGainDB;

		// Frequency bandwidth of audio going into input diffusers. 0.999 is full bandwidth
		float Bandwidth;

		// Amount of input diffusion (larger value results in more diffusion)
		float Diffusion;

		// The amount of high-frequency dampening in plate feedback paths
		float Dampening;

		// The amount of decay in the feedback path. Lower value is larger reverb time.
		float Decay;

		// The amount of diffusion in decay path. Larger values is a more dense reverb.
		float Density;

		FLateReflectionsFastSettings();

		bool operator==(const FLateReflectionsFastSettings& Other) const;

		bool operator!=(const FLateReflectionsFastSettings& Other) const;
	};


	// FLateReflections generates the long tail reverb of an input audio signal using a relatively
	// fast algorithm using all pass filter delay lines. It supports 1 or 2 channel input and always 
	// produces 2 channel output. 
	class AUDIOMIXER_API FLateReflectionsFast {
	public:

		// Limits on settings.
		static const float MaxLateDelayMsec;
		static const float MinLateDelayMsec;
		static const float MaxLateGainDB;
		static const float MaxBandwidth;
		static const float MinBandwidth;
		static const float MaxDampening;
		static const float MinDampening;
		static const float MaxDiffusion;
		static const float MinDiffusion;
		static const float MaxDecay;
		static const float MinDecay;
		static const float MaxDensity;
		static const float MinDensity;

		static const FLateReflectionsFastSettings DefaultSettings;

		// InMaxNumInternalBufferSamples sets the maximum possible number of samples in an internal
		// buffer. This only affectsinternal chunking and does not affect the number of samples that
		// can be sent to ProcessAudio(...), 
		FLateReflectionsFast(float InSampleRate, int InMaxNumInternalBufferSamples, const FLateReflectionsFastSettings& InSettings=DefaultSettings);
		
		~FLateReflectionsFast();

		// Copies the reverb settings internally, clamps the internal copy and applies the clamped settings.
		void SetSettings(const FLateReflectionsFastSettings& InSettings);

		// Alters settings to ensure they are within acceptable ranges. 
		static void ClampSettings(FLateReflectionsFastSettings& InOutSettings);

		// Create reverberation in OutSamples based upon InSamples. 
		// OutSamples is a 2 channel audio buffer no matter the value of InNumChannels.
		// OutSamples is mixed with InSamples based upon gain.
		void ProcessAudio(const AlignedFloatBuffer& InSamples, const int32 InNumChannels, AlignedFloatBuffer& OutLeftSamples, AlignedFloatBuffer& OutRightSamples);

	private:
		void ProcessAudioBuffer(const float* InSampleData, const int32 InNumFrames, const int32 InNumChannels, float* OutLeftSampleData, float* OutRightSampleData);

		void ApplySettings();

		void GeneraterPlateModulations(const int32 InNum, AlignedFloatBuffer& OutLeftDelays, AlignedFloatBuffer& OutRightDelays);

		float SampleRate;
		float Gain;
		float ModulationPhase;
		float ModulationQuadPhase;
		float ModulationPhaseIncrement;
		int32 NumInternalBufferSamples;

		// Current parameter settings of reverb
		FLateReflectionsFastSettings Settings;

		// Sample rate used for hard-coded delay line values
		static const int32 PresetSampleRate = 29761;

		// A simple pre-delay line to emulate large delays for late-reflections
		TUniquePtr<FIntegerDelay> PreDelay;

		// Input diffusion
		TUniquePtr<FBufferOnePoleLPF> InputLPF;
		TUniquePtr<FLongDelayAPF> APF1;
		TUniquePtr<FLongDelayAPF> APF2;
		TUniquePtr<FLongDelayAPF> APF3;
		TUniquePtr<FLongDelayAPF> APF4;

		// The plate delay data
		FLateReflectionsPlateDelays LeftPlateDelays;
		FLateReflectionsPlateDelays RightPlateDelays;

		// The plates.
		TUniquePtr<FLateReflectionsPlate> LeftPlate;
		TUniquePtr<FLateReflectionsPlate> RightPlate;

		// The plate outputs
		FLateReflectionsPlateOutputs LeftPlateOutputs;
		FLateReflectionsPlateOutputs RightPlateOutputs;

		// Plate output buffering.
		TUniquePtr<FAlignedBlockBuffer> LeftPlateOutputBuffer;
		TUniquePtr<FAlignedBlockBuffer> RightPlateOutputBuffer;

		// Buffer for delay modulation of left and right plates.
		AlignedFloatBuffer LeftDelayModSamples;
		AlignedFloatBuffer RightDelayModSamples;

		AlignedFloatBuffer WorkBufferA;
		AlignedFloatBuffer WorkBufferB;
		AlignedFloatBuffer WorkBufferC;

	};

}
