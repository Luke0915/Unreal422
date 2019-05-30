// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================================
	HoloLensAtomics.h: HoloLens platform Atomics functions
==============================================================================================*/

#pragma once
#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformAtomics.h"
#include "HoloLensSystemIncludes.h"
#include <intrin.h>


#pragma intrinsic(_InterlockedExchangeAdd8)
#pragma intrinsic(_InterlockedIncrement16)
#pragma intrinsic(_InterlockedIncrement)
#if PLATFORM_64BITS
#pragma intrinsic(_InterlockedIncrement64)
#pragma intrinsic(_InterlockedDecrement64)
#pragma intrinsic(_InterlockedExchangeAdd64)
#pragma intrinsic(_InterlockedExchange64)
#pragma intrinsic(_InterlockedCompareExchange64)
#else
#pragma intrinsic(_InterlockedCompareExchange64)
#endif
#pragma intrinsic(_InterlockedDecrement16)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchangeAdd16)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedExchange8)
#pragma intrinsic(_InterlockedExchange16)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchangePointer)
#pragma intrinsic(_InterlockedCompareExchange8)
#pragma intrinsic(_InterlockedCompareExchange16)
#pragma intrinsic(_InterlockedCompareExchange)

/**
 * HoloLens implementation of the Atomics OS functions
 */
struct CORE_API FHoloLensAtomics : public FGenericPlatformAtomics
{

	static FORCEINLINE int8 InterlockedIncrement(volatile int8* Value)
	{
		return (int8)::_InterlockedExchangeAdd8((volatile char*)Value, 1) + 1;
	}

	static FORCEINLINE int16 InterlockedIncrement(volatile int16* Value)
	{
		return (int16)::_InterlockedIncrement16((volatile short*)Value);
	}

	static FORCEINLINE int32 InterlockedIncrement(volatile int32* Value)
	{
		return (int32)::_InterlockedIncrement((volatile long*)Value);
	}

	static FORCEINLINE int64 InterlockedIncrement (volatile int64* Value)
	{
#if PLATFORM_64BITS
		return (int64)::_InterlockedIncrement64((volatile int64*)Value);
#else
		// No explicit instruction for 64-bit atomic increment on 32-bit processors; has to be implemented in terms of CMPXCHG8B
		for (;;)
		{
			int64 OldValue = *Value;
			if (_InterlockedCompareExchange64(Value, OldValue + 1, OldValue) == OldValue)
			{
				return OldValue + 1;
			}
		}
#endif
	}

	static FORCEINLINE int8 InterlockedDecrement(volatile int8* Value)
	{
		return (int8)::_InterlockedExchangeAdd8((volatile char*)Value, -1) - 1;
	}

	static FORCEINLINE int16 InterlockedDecrement(volatile int16* Value)
	{
		return (int16)::_InterlockedDecrement16((volatile short*)Value);
	}

	static FORCEINLINE int32 InterlockedDecrement(volatile int32* Value)
	{
		return (int32)::_InterlockedDecrement((volatile long*)Value);
	}

	static FORCEINLINE int64 InterlockedDecrement (volatile int64* Value)
	{
#if PLATFORM_64BITS
		return (int64)::_InterlockedDecrement64((volatile int64*)Value);
#else
		// No explicit instruction for 64-bit atomic decrement on 32-bit processors; has to be implemented in terms of CMPXCHG8B
		for (;;)
		{
			int64 OldValue = *Value;
			if (_InterlockedCompareExchange64(Value, OldValue - 1, OldValue) == OldValue)
			{
				return OldValue - 1;
			}
		}
#endif
	}

	static FORCEINLINE int8 InterlockedAdd(volatile int8* Value, int8 Amount)
	{
		return (int8)::_InterlockedExchangeAdd8((volatile char*)Value, (char)Amount);
	}

	static FORCEINLINE int16 InterlockedAdd(volatile int16* Value, int16 Amount)
	{
		return (int16)::_InterlockedExchangeAdd16((volatile short*)Value, (short)Amount);
	}

	static FORCEINLINE int32 InterlockedAdd(volatile int32* Value, int32 Amount)
	{
		return (int32)::_InterlockedExchangeAdd((volatile long*)Value, (long)Amount);
	}

	static FORCEINLINE int64 InterlockedAdd (volatile int64* Value, int64 Amount)
	{
#if PLATFORM_64BITS
		return (int64)::_InterlockedExchangeAdd64((volatile int64*)Value, (int64)Amount);
#else
		// No explicit instruction for 64-bit atomic add on 32-bit processors; has to be implemented in terms of CMPXCHG8B
		for (;;)
		{
			int64 OldValue = *Value;
			if (_InterlockedCompareExchange64(Value, OldValue + Amount, OldValue) == OldValue)
			{
				return OldValue + Amount;
			}
		}
#endif
	}

	static FORCEINLINE int8 InterlockedExchange(volatile int8* Value, int8 Exchange)
	{
		return (int8)::_InterlockedExchange8((volatile char*)Value, (char)Exchange);
	}

	static FORCEINLINE int16 InterlockedExchange(volatile int16* Value, int16 Exchange)
	{
		return (int16)::_InterlockedExchange16((volatile short*)Value, (short)Exchange);
	}

	static FORCEINLINE int32 InterlockedExchange(volatile int32* Value, int32 Exchange)
	{
		return (int32)::_InterlockedExchange((volatile long*)Value, (long)Exchange);
	}

	static FORCEINLINE int64 InterlockedExchange (volatile int64* Value, int64 Exchange)
	{
#if PLATFORM_64BITS
		return ::_InterlockedExchange64(Value, Exchange);
#else
		// No explicit instruction for 64-bit atomic exchange on 32-bit processors; has to be implemented in terms of CMPXCHG8B
		for (;;)
		{
			int64 OldValue = *Value;
			if (_InterlockedCompareExchange64(Value, Exchange, OldValue) == OldValue)
			{
				return OldValue;
			}
		}
#endif
	}

	static FORCEINLINE void* InterlockedExchangePtr( void** Dest, void* Exchange )
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (IsAligned(Dest) == false)
		{
			HandleAtomicsFailure(TEXT("InterlockedExchangePointer requires Dest pointer to be aligned to %d bytes"), sizeof(void*));
		}
#endif

		return ::_InterlockedExchangePointer(Dest, Exchange);
	}

	static FORCEINLINE int8 InterlockedCompareExchange(volatile int8* Dest, int8 Exchange, int8 Comparand)
	{
		return (int8)::_InterlockedCompareExchange8((volatile char*)Dest, (char)Exchange, (char)Comparand);
	}

	static FORCEINLINE int16 InterlockedCompareExchange(volatile int16* Dest, int16 Exchange, int16 Comparand)
	{
		return (int16)::_InterlockedCompareExchange16((volatile short*)Dest, (short)Exchange, (short)Comparand);
	}

	static FORCEINLINE int32 InterlockedCompareExchange(volatile int32* Dest,int32 Exchange,int32 Comparand)
	{
		return (int32)::_InterlockedCompareExchange((volatile long*)Dest, (long)Exchange, (long)Comparand);
	}

	static FORCEINLINE int64 InterlockedCompareExchange (volatile int64* Dest, int64 Exchange, int64 Comparand)
	{
		#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (IsAligned(Dest, 8) == false)
			{
				HandleAtomicsFailure(TEXT("InterlockedCompareExchange64 requires args be aligned to %d bytes"), sizeof(int64));
			}
		#endif

			return (int64)::_InterlockedCompareExchange64(Dest, Exchange, Comparand);
	}

	static FORCEINLINE int8 AtomicRead(volatile const int8* Src)
	{
		return InterlockedCompareExchange((volatile int8*)Src, 0, 0);
	}

	static FORCEINLINE int16 AtomicRead(volatile const int16* Src)
	{
		return InterlockedCompareExchange((volatile int16*)Src, 0, 0);
	}

	static FORCEINLINE int32 AtomicRead(volatile const int32* Src)
	{
		return InterlockedCompareExchange((volatile int32*)Src, 0, 0);
	}

	static FORCEINLINE int64 AtomicRead(volatile const int64* Src)
	{
		return InterlockedCompareExchange((volatile int64*)Src, 0, 0);
	}

	static FORCEINLINE void AtomicStore(volatile int8* Src, int8 Val)
	{
		InterlockedExchange(Src, Val);
	}

	static FORCEINLINE void AtomicStore(volatile int16* Src, int16 Val)
	{
		InterlockedExchange(Src, Val);
	}

	static FORCEINLINE void AtomicStore(volatile int32* Src, int32 Val)
	{
		InterlockedExchange(Src, Val);
	}

	static FORCEINLINE void AtomicStore(volatile int64* Src, int64 Val)
	{
		InterlockedExchange(Src, Val);
	}

	 /**
	 *	The function compares the Destination value with the Comparand value:
	 *		- If the Destination value is equal to the Comparand value, the Exchange value is stored in the address specified by Destination, 
	 *		- Otherwise, the initial value of the Destination parameter is stored in the address specified specified by Comparand.
	 *	
	 *	@return true if Comparand equals the original value of the Destination parameter, or false if Comparand does not equal the original value of the Destination parameter.
	 *	
	 *	Early AMD64 processors lacked the CMPXCHG16B instruction.
	 *	To determine whether the processor supports this operation, call the IsProcessorFeaturePresent function with PF_COMPARE64_EXCHANGE128.
	 */
#if	PLATFORM_HAS_128BIT_ATOMICS
	static FORCEINLINE bool InterlockedCompareExchange128( volatile FInt128* Dest, const FInt128& Exchange, FInt128* Comparand )
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (IsAligned(Dest,16) == false)
		{
			HandleAtomicsFailure(TEXT("InterlockedCompareExchangePointer requires Dest pointer to be aligned to 16 bytes") );
		}
		if (IsAligned(Comparand,16) == false)
		{
			HandleAtomicsFailure(TEXT("InterlockedCompareExchangePointer requires Comparand pointer to be aligned to 16 bytes") );
		}
#endif

		return ::_InterlockedCompareExchange128((int64 volatile *)Dest, Exchange.High, Exchange.Low, (int64*)Comparand) == 1;
	}
	/**
	* Atomic read of 128 bit value with a memory barrier
	*/
	static FORCEINLINE void AtomicRead128(const volatile FInt128* Src, FInt128* OutResult)
	{
		FInt128 Zero;
		Zero.High = 0;
		Zero.Low = 0;
		*OutResult = Zero;
		InterlockedCompareExchange128((FInt128*)Src, Zero, OutResult);
	}

#endif // PLATFORM_HAS_128BIT_ATOMICS

	/**
	 * Atomically compares the pointer to comparand and replaces with the exchange
	 * pointer if they are equal and returns the original value
	 */
	static FORCEINLINE void* InterlockedCompareExchangePointer(void** Dest,void* Exchange,void* Comparand)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		// Dest needs to be aligned otherwise the function will behave unpredictably 
		if (IsAligned( Dest ) == false)
		{
			HandleAtomicsFailure( TEXT( "InterlockedCompareExchangePointer requires Dest pointer to be aligned to %d bytes" ), sizeof(void*) );
		}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#if PLATFORM_64BITS
		return (void*)::_InterlockedCompareExchange64((volatile int64*)Dest, (int64)Exchange, (int64)Comparand);
#else
		return (void*)::_InterlockedCompareExchange((volatile long*)Dest, (long)Exchange, (long)Comparand);
#endif
	}

private:
	/** Handles atomics function failure. Since 'check' has not yet been declared here we need to call external function to use it. */
	static void HandleAtomicsFailure( const TCHAR* InFormat, ... );
};

typedef FHoloLensAtomics FPlatformAtomics;
