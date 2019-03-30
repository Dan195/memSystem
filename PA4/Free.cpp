//-----------------------------------------------------------------------------
// Copyright Ed Keenan 2019
// Optimized C++
//----------------------------------------------------------------------------- 

#include "Framework.h"

#include "Used.h"
#include "Free.h"
#include "Block.h"

// Add magic here

// ---  End of File ---------------
Free::Free(uint32_t size)
	: pFreeNext(nullptr),
	pFreePrev(nullptr),
	mBlockSize(size),
	mType(Block::Free),
	mAboveBlockFree(false),
	pad(0)
{

}