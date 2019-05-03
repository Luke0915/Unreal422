// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp dvector

#pragma once

#include <CoreMinimal.h>
#include "VectorTypes.h"
#include "IndexTypes.h"

#include <array>


/*
 * Blocked array with fixed, power-of-two sized blocks.
 * 
 * Iterator functions suitable for use with range-based for are provided
 */
template <class Type>
class FDynamicVector
{
public:
	FDynamicVector()
	{
		CurBlock = 0;
		CurBlockUsed = 0;
		Blocks.Emplace();
	}

	FDynamicVector(const FDynamicVector& Copy)
	{
		*this = Copy;
	}

	FDynamicVector(FDynamicVector&& Moved)
	{
		*this = MoveTemp(Moved);
	}

	~FDynamicVector() {}

	const FDynamicVector& operator=(const FDynamicVector& Copy);
	const FDynamicVector& operator=(FDynamicVector&& Moved);

	inline void Clear();
	inline void Fill(const Type& Value);
	inline void Resize(size_t Count);
	inline void Resize(size_t Count, const Type& InitValue);
	inline void SetNum(size_t Count) { Resize(Count); }

	inline bool IsEmpty() const { return CurBlock == 0 && CurBlockUsed == 0; }
	inline size_t GetLength() const { return CurBlock * BlockSize + CurBlockUsed; }
	inline size_t Num() const { return CurBlock * BlockSize + CurBlockUsed; }
	inline int GetBlockSize() const { return BlockSize; }
	inline size_t GetByteCount() const { return Blocks.Num() * BlockSize * sizeof(Type); }

	inline void Add(const Type& Data);
	inline void Add(const FDynamicVector<Type>& Data);
	inline void PopBack();

	inline void InsertAt(const Type& Data, unsigned int Index);

	inline const Type& Front() const { return Blocks[0][0]; }
	inline const Type& Back() const { return Blocks[CurBlock][CurBlockUsed - 1]; }

	inline Type& operator[](unsigned int Index)
	{
		return Blocks[Index >> nShiftBits][Index & BlockIndexBitmask];
	}
	inline const Type& operator[](unsigned int Index) const
	{
		return Blocks[Index >> nShiftBits][Index & BlockIndexBitmask];
	}

	// apply f() to each member sequentially
	template <typename Func>
	void Apply(const Func& f);


protected:
	// [RMS] BlockSize must be a power-of-two, so we can use bit-shifts in operator[]
	static constexpr int BlockSize = 2048; // (1 << 11)
	static constexpr int nShiftBits = 11;
	static constexpr int BlockIndexBitmask = 2047; // low 11 bits

	unsigned int CurBlock;
	unsigned int CurBlockUsed;

	using BlockType = std::array<Type, BlockSize>;
	TArray<BlockType> Blocks;

	friend class FIterator;


public:
	/*
	 * FIterator class iterates over values of vector
	 */
	class FIterator
	{
	public:
		inline const Type& operator*() const
		{
			return (*DVector)[Idx];
		}
		inline Type& operator*()
		{
			return (*DVector)[Idx];
		}
		inline FIterator& operator++()   // prefix
		{
			Idx++;
			return *this;
		}
		inline FIterator operator++(int) // postfix
		{
			FIterator Copy(*this);
			Idx++;
			return Copy;
		}
		inline bool operator==(const FIterator& Itr2)
		{
			return DVector == Itr2.DVector && Idx == Itr2.Idx;
		}
		inline bool operator!=(const FIterator& Itr2)
		{
			return DVector != Itr2.DVector || Idx != Itr2.Idx;
		}

	protected:
		FDynamicVector<Type>* DVector;
		int Idx;
		inline FIterator(FDynamicVector<Type>* Parent, int ICur)
		{
			DVector = Parent;
			Idx = ICur;
		}
		friend class FDynamicVector<Type>;
	};

	/** @return iterator at beginning of vector */
	FIterator begin()
	{
		return IsEmpty() ? end() : FIterator(this, 0);
	}
	/** @return iterator at end of vector */
	FIterator end()
	{
		return FIterator(this, (int)GetLength());
	}
};






template <class Type, int N>
class FDynamicVectorN
{
public:
	FDynamicVectorN()
	{
	}
	FDynamicVectorN(const FDynamicVectorN& Copy)
		: Data(Copy.Data)
	{
	}
	FDynamicVectorN(FDynamicVectorN&& Moved)
		: Data(MoveTemp(Moved.Data))
	{
	}
	~FDynamicVectorN()
	{
	}

	const FDynamicVectorN& operator=(const FDynamicVectorN& Copy)
	{
		Data = Copy.Data;
		return *this;
	}
	const FDynamicVectorN& operator=(FDynamicVectorN&& Moved)
	{
		Data = MoveTemp(Moved.Data);
		return *this;
	}

	inline void Clear()
	{
		Data.Clear();
	}
	inline void Fill(const Type& Value)
	{
		Data.Fill(Value);
	}
	inline void Resize(size_t Count)
	{
		Data.Resize(Count * N);
	}
	inline void Resize(size_t Count, const Type& InitValue)
	{
		Data.Resize(Count * N, InitValue);
	}

	inline bool IsEmpty() const
	{
		return Data.IsEmpty();
	}
	inline size_t GetLength() const
	{
		return Data.GetLength() / N;
	}
	inline int GetBlockSize() const
	{
		return Data.GetBlockSize();
	}
	inline size_t GetByteCount() const
	{
		return Data.GetByteCount();
	}

	inline void Add(const std::array<Type, N>& AddData)
	{
		// todo specialize for N=2,3,4
		for (int i = 0; i < N; i++)
		{
			Data.Add(AddData[i]);
		}
	}

	inline void PopBack()
	{
		for (int i = 0; i < N; i++)
		{
			PopBack();
		}
	} // TODO specialize

	inline void InsertAt(const std::array<Type, N>& AddData, unsigned int Index)
	{
		for (int i = 1; i <= N; i++)
		{
			Data.InsertAt(AddData[N - i], N * (Index + 1) - i);
		}
	}

	inline Type& operator()(unsigned int TopIndex, unsigned int SubIndex)
	{
		return Data[TopIndex * N + SubIndex];
	}
	inline const Type& operator()(unsigned int TopIndex, unsigned int SubIndex) const
	{
		return Data[TopIndex * N + SubIndex];
	}
	inline void SetVector2(unsigned int TopIndex, const FVector2<Type>& V)
	{
		check(N >= 2);
		unsigned int i = TopIndex * N;
		Data[i] = V.X;
		Data[i + 1] = V.Y;
	}
	inline void SetVector3(unsigned int TopIndex, const FVector3<Type>& V)
	{
		check(N >= 3);
		unsigned int i = TopIndex * N;
		Data[i] = V.X;
		Data[i + 1] = V.Y;
		Data[i + 2] = V.Z;
	}
	inline FVector2<Type> AsVector2(unsigned int TopIndex) const
	{
		check(N >= 2);
		return FVector2<Type>(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1]);
	}
	inline FVector3<Type> AsVector3(unsigned int TopIndex) const
	{
		check(N >= 3);
		return FVector3<Type>(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1],
			Data[TopIndex * N + 2]);
	}
	inline FIndex2i AsIndex2(unsigned int TopIndex) const
	{
		check(N >= 2);
		return FIndex2i(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1]);
	}
	inline FIndex3i AsIndex3(unsigned int TopIndex) const
	{
		check(N >= 3);
		return FIndex3i(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1],
			Data[TopIndex * N + 2]);
	}
	inline FIndex4i AsIndex4(unsigned int TopIndex) const
	{
		check(N >= 4);
		return FIndex4i(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1],
			Data[TopIndex * N + 2],
			Data[TopIndex * N + 3]);
	}

protected:
	FDynamicVector<Type> Data;

	friend class FIterator;
};

template class FDynamicVectorN<double, 2>;

typedef FDynamicVectorN<float, 3> FDynamicVector3f;
typedef FDynamicVectorN<float, 2> FDynamicVector2f;
typedef FDynamicVectorN<double, 3> FDynamicVector3d;
typedef FDynamicVectorN<double, 2> FDynamicVector2d;
typedef FDynamicVectorN<int, 3> FDynamicVector3i;
typedef FDynamicVectorN<int, 2> FDynamicVector2i;





template <class Type>
const FDynamicVector<Type>& FDynamicVector<Type>::operator=(const FDynamicVector& Copy)
{
	Blocks = Copy.Blocks;
	CurBlock = Copy.CurBlock;
	CurBlockUsed = Copy.CurBlockUsed;
	return *this;
}

template <class Type>
const FDynamicVector<Type>& FDynamicVector<Type>::operator=(FDynamicVector&& Moved)
{
	Blocks = MoveTemp(Moved.Blocks);
	CurBlock = Moved.CurBlock;
	CurBlockUsed = Moved.CurBlockUsed;
	return *this;
}

template <class Type>
void FDynamicVector<Type>::Clear()
{
	Blocks.Empty();
	CurBlock = 0;
	CurBlockUsed = 0;
	Blocks.Add(BlockType());
}

template <class Type>
void FDynamicVector<Type>::Fill(const Type& Value)
{
	size_t Count = Blocks.Num();
	for (unsigned int i = 0; i < Count; ++i)
	{
		Blocks[i].fill(Value);
	}
}

template <class Type>
void FDynamicVector<Type>::Resize(size_t Count)
{
	if (GetLength() == Count)
	{
		return;
	}

	// figure out how many segments we need
	int nNumSegs = 1 + (int)Count / BlockSize;

	// figure out how many are currently allocated...
	size_t nCurCount = Blocks.Num();

	// erase extra segments memory
	// [RMS] not necessary right? std::array will deallocate?
	//for (int i = nNumSegs; i < nCurCount; ++i)
	//	Blocks[i] = null;

	// resize to right number of segments
	if (nNumSegs >= Blocks.Num())
	{
		// allocate new segments
		for (int i = (int)nCurCount; i < nNumSegs; ++i)
		{
			Blocks.Emplace();
		}
	}
	else
	{
		//Blocks.RemoveRange(nNumSegs, Blocks.Count - nNumSegs);
		Blocks.SetNum(nNumSegs);
	}

	// mark last segment
	CurBlockUsed = (unsigned int)(Count - (nNumSegs - 1) * BlockSize);
	CurBlock = nNumSegs - 1;
}

template <class Type>
void FDynamicVector<Type>::Resize(size_t Count, const Type& InitValue)
{
	size_t nCurSize = GetLength();
	Resize(Count);
	for (size_t Index = nCurSize; Index < Count; ++Index)
	{
		this->operator[](Index) = InitValue;
	}
}

template <class Type>
void FDynamicVector<Type>::Add(const Type& Value)
{
	if (CurBlockUsed == BlockSize)
	{
		if (CurBlock == Blocks.Num() - 1)
		{
			Blocks.Emplace();
		}
		CurBlock++;
		CurBlockUsed = 0;
	}
	Blocks[CurBlock][CurBlockUsed] = Value;
	CurBlockUsed++;
}


template <class Type>
void FDynamicVector<Type>::Add(const FDynamicVector<Type>& AddData)
{
	// @todo it could be more efficient to use memcopies here...
	size_t nSize = AddData.Num();
	for (unsigned int k = 0; k < nSize; ++k)
	{
		Add(AddData[k]);
	}
}

template <class Type>
void FDynamicVector<Type>::PopBack()
{
	if (CurBlockUsed > 0)
	{
		CurBlockUsed--;
	}
	if (CurBlockUsed == 0 && CurBlock > 0)
	{
		CurBlock--;
		CurBlockUsed = BlockSize;
		// remove block ??
	}
}

template <class Type>
void FDynamicVector<Type>::InsertAt(const Type& AddData, unsigned int Index)
{
	size_t s = GetLength();
	if (Index == s)
	{
		Add(AddData);
	}
	else if (Index > s)
	{
		Resize(Index);
		Add(AddData);
	}
	else
	{
		(*this)[Index] = AddData;
	}
}

template <typename Type>
template <typename Func>
void FDynamicVector<Type>::Apply(const Func& f)
{
	for (int bi = 0; bi < CurBlock; ++bi)
	{
		auto block = Blocks[bi];
		for (int k = 0; k < BlockSize; ++k)
		{
			applyF(block[k], k);
		}
	}
	auto lastblock = Blocks[CurBlock];
	for (int k = 0; k < CurBlockUsed; ++k)
	{
		applyF(lastblock[k], k);
	}
}
