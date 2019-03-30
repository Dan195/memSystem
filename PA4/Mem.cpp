//-----------------------------------------------------------------------------
// Copyright Ed Keenan 2019
// Optimized C++
//----------------------------------------------------------------------------- 

#include <malloc.h>
#include <new>

#include "Framework.h"

#include "Mem.h"
#include "Heap.h"
#include "Block.h"

#define STUB_PLEASE_REPLACE(x) (x)

// To help with coalescing... not required
struct SecretPtr
{
	Free *pFree;
};

void Mem::initialize()
{
	Heap *pHeapStart = this->pHeap;

	Free *pB = (Free *)(pHeapStart + 1);
	Free * pD = pB + 1;
	//align malloc here for placement new.
	uint32_t newSize = (uint32_t)pHeapStart->mStats.heapBottomAddr - (uint32_t)pD;

	Free *pFree = placement_new(pB, Free, newSize);
	this->CreateSecretPtr(pFree);
	this->pHeap->pFreeHead = pFree;
	this->pHeap->pNextFit = pFree;



	//update stats
	pHeap->mStats.currFreeMem = newSize;
	pHeap->mStats.currNumFreeBlocks++;


	pHeap->pUsedHead = nullptr;
}

void *Mem::malloc(const uint32_t size)
{
	Free * bestFit = this->pHeap->pNextFit;
	if (!bestFit)
		return 0;
	uint32_t bestFitSize = 0;

	//Get the size of the new malloc location as well as update freePtr to point to it
	bestFitSize = this->GetBestFit(*&bestFit, bestFitSize, size);
	Free * bestFitNext = bestFit != nullptr ? bestFit->pFreeNext : nullptr;
	Free * bestFitPrev = bestFit != nullptr ? bestFit->pFreePrev : nullptr;
	Free * bestFitNextPrevPrev = nullptr;
	Free * bestFitPrevNextNext = nullptr;
	if (bestFitNext && bestFitNext->pFreeNext)
		bestFitNextPrevPrev = bestFitNext->pFreePrev->pFreePrev;
	if (bestFitPrev && bestFitPrev->pFreeNext)
		bestFitPrevNextNext = bestFitPrev->pFreeNext->pFreeNext;

	if (bestFit)
	{
		//if (bestFit->pFreeNext)
		//	bestFit->pFreeNext->pFreePrev = bestFit->pFreePrev;
		//if (bestFit->pFreePrev)
		//	bestFit->pFreePrev->pFreeNext = bestFit->pFreeNext;
		Used * used = placement_new(bestFit, Used, size);
		this->CreateSecretPtr((Free*)used);

		if (bestFitSize > (size))
		{
			this->pHeap->mStats.currFreeMem -= (size + sizeof(Used));
			uint32_t newFreeSize = bestFitSize - size - sizeof(Free);
			Free * freeLocation = (Free*)used + 1 + (size / sizeof(Free));
			freeLocation->mAboveBlockFree = false;
			placement_new(freeLocation, Free, newFreeSize);
			if (this->pHeap->pFreeHead == bestFit)
				this->pHeap->pFreeHead =  freeLocation;
			freeLocation->pFreePrev = bestFitPrev;
			if (bestFitPrev)
				bestFitPrev->pFreeNext = freeLocation;
			freeLocation->pFreeNext = bestFitNext;
			if (bestFitNext)
				bestFitNext->pFreePrev = freeLocation;

			this->pHeap->pNextFit = freeLocation;
			this->CreateSecretPtr(freeLocation);

		}
		else
		{
			if (this->pHeap->pFreeHead == bestFit)
				this->pHeap->pFreeHead = bestFitNext;
			this->pHeap->pNextFit = bestFitNext != nullptr ? bestFitNext : this->pHeap->pFreeHead;
			this->pHeap->mStats.currFreeMem -= size;
			this->pHeap->mStats.currNumFreeBlocks--;
			if (bestFitPrev)
			{
				if (bestFitPrev->pFreeNext)
					bestFitPrev->pFreeNext = bestFitPrevNextNext;
			}

			if (bestFitNext)
				{
					if (bestFitNext->pFreePrev)
					bestFitNext->pFreePrev = bestFitNextPrevPrev;
				}
	

		}

		//insert used header into linked list
		used->pUsedNext = this->pHeap->pUsedHead;
		if (used->pUsedNext)
			used->pUsedNext->pUsedPrev = used;
		this->pHeap->pUsedHead = used;
		used->mBlockSize = size;
		this->pHeap->mStats.currNumUsedBlocks++;
		if(this->pHeap->mStats.currNumUsedBlocks > this->pHeap->mStats.peakNumUsed)
			this->pHeap->mStats.peakNumUsed++;
		this->pHeap->mStats.currUsedMem += size;
		if (this->pHeap->mStats.currUsedMem > this->pHeap->mStats.peakUsedMemory)
			this->pHeap->mStats.peakUsedMemory = this->pHeap->mStats.currUsedMem;


		bestFit->mType = Block::Used;

		Used* returnPointer = used + 1;

		return returnPointer;
	}
	return 0;

}

void Mem::free(void * const data)
{

	//Get free header attached to passed in 'data' by going up the size of the free header
	Free * dataFreePtr = (Free *)data - 1;



	//Remove free node from the used linked list and updated used linked list connection accordingly
	this->RemoveNodeFromUsedList((Used*)dataFreePtr);



	uint32_t size = dataFreePtr->mBlockSize;


	// ### COALESC DOWN

	uint32_t nextPtr = (uint32_t)(dataFreePtr + 1) + size;
	Free* freePointerCheckDown = (Free*)nextPtr;


	if (freePointerCheckDown->mType == Block::Free)
	{
		this->AdjustNextFit(freePointerCheckDown, dataFreePtr);
		//Check to see if the node below was originally the pFreeHead. If so, make dataFreePtr pFreeHead as the two are being joined.
		if (freePointerCheckDown == this->pHeap->pFreeHead)
			this->pHeap->pFreeHead = dataFreePtr;

		//Setup next pointer
		Free * dataFreePtrNext = freePointerCheckDown->pFreeNext;
		if(dataFreePtrNext)
			dataFreePtrNext->pFreePrev = dataFreePtr;
	
		Free * checkDownPrev = freePointerCheckDown->pFreePrev;
		if (checkDownPrev)
			checkDownPrev->pFreeNext = dataFreePtr;
		placement_new(dataFreePtr, Free, size + freePointerCheckDown->mBlockSize + sizeof(Free));
		dataFreePtr->pFreeNext = dataFreePtrNext;
		dataFreePtr->pFreePrev = checkDownPrev;
		this->CreateSecretPtr(dataFreePtr);
		this->AdjustBelowMAboveBlockFree(dataFreePtr);
		this->pHeap->mStats.currNumUsedBlocks--;
		this->pHeap->mStats.currFreeMem += (size + sizeof(Free));


	}
	else
	{
		placement_new(dataFreePtr, Free, size);
		this->CreateSecretPtr(dataFreePtr);
		this->AdjustBelowMAboveBlockFree(dataFreePtr);
		this->pHeap->mStats.currNumUsedBlocks--;
		this->pHeap->mStats.currNumFreeBlocks++;
		this->pHeap->mStats.currFreeMem += size;


		//Setup the next pointer for the newly inserted FreeHdr. Ensures that its next pointer isnt stil 
		Free * findNextPtr = this->pHeap->pFreeHead;
		while (findNextPtr < dataFreePtr && findNextPtr) 
			findNextPtr = findNextPtr->pFreeNext;
		if (findNextPtr)
		{
			dataFreePtr->pFreeNext = findNextPtr;
			dataFreePtr->pFreePrev = findNextPtr->pFreePrev;
			if (dataFreePtr->pFreePrev)
				dataFreePtr->pFreePrev->pFreeNext = dataFreePtr;
			findNextPtr->pFreePrev = dataFreePtr;
		}
		else
		{
			findNextPtr = this->pHeap->pFreeHead;
			while (findNextPtr && findNextPtr->pFreeNext)
				findNextPtr = findNextPtr->pFreeNext;
			if (findNextPtr)
			{
				findNextPtr->pFreeNext = dataFreePtr;
				dataFreePtr->pFreePrev = findNextPtr;
			}
		}
	}

	// ######## COALESC DOWN  ENDS

	// ####### COALESC UP
	SecretPtr * secretAbove = (SecretPtr*)((uint32_t)dataFreePtr - sizeof(SecretPtr));
	if (this->VerifyPointerInHeapRange(secretAbove->pFree))
	{
		//If we should coalesc up
		if (secretAbove->pFree->mType == Block::Free)
		{

			//set next pointer for secretAbove->pFree
			Free * secretAboveNext = dataFreePtr->pFreeNext;
			if(secretAboveNext)
				secretAboveNext->pFreePrev = secretAbove->pFree;
			secretAbove->pFree->mBlockSize += (dataFreePtr->mBlockSize + sizeof(Free));
			secretAbove->pFree->pFreeNext = secretAboveNext;
			this->AdjustNextFit(dataFreePtr, secretAbove->pFree);
			this->CreateSecretPtr(secretAbove->pFree);
			this->pHeap->mStats.currNumFreeBlocks--;
			this->pHeap->mStats.currFreeMem += sizeof(Free);
		}
	}


	//updating the head and next fit
	
	if (!this->pHeap->pFreeHead)
		this->pHeap->pFreeHead = dataFreePtr;
	if (!this->pHeap->pNextFit)
		this->pHeap->pNextFit = dataFreePtr;

	//If dataFreePtr is closer to the top of the heap then current pFreeHead, make dataPointer the new pFreeHead.
	if (dataFreePtr < this->pHeap->pFreeHead)
	{
		//dataFreePtr->pFreeNext = this->pHeap->pFreeHead;
		this->pHeap->pFreeHead = dataFreePtr;
	}


	this->pHeap->mStats.currUsedMem -= size;
}

// Updates value of pFree instead of returnng it. Returns the size of the block with the best fit
uint32_t Mem::GetBestFit(Free *& pFree, uint32_t bestFitSize, const uint32_t size)
{
	Free * bestFit = nullptr;
	bool restartedFromTop = false;
	while (pFree && !(restartedFromTop && pFree == this->pHeap->pNextFit))
	{
		if (this->VerifyPointerInHeapRange(pFree)) {
			if (pFree->mBlockSize >= size && pFree->mBlockSize > bestFitSize)
			{
				bestFitSize = pFree->mBlockSize;
				bestFit = pFree;
				return bestFitSize;
			}
			pFree = pFree->pFreeNext != nullptr ? pFree->pFreeNext : this->pHeap->pFreeHead;
			restartedFromTop = true;
		}
		else {
			pFree = bestFit;
			return bestFitSize;
		}
	}
	pFree = bestFit;
	return bestFitSize;
}

void Mem::RemoveNodeFromUsedList(Used * pRemove)
{
	if (pRemove->pUsedPrev)
		pRemove->pUsedPrev->pUsedNext = pRemove->pUsedNext;
	if (pRemove->pUsedNext)
		pRemove->pUsedNext->pUsedPrev = pRemove->pUsedPrev;

	//Update Mem's stored Used Header Pointer if the node removed from the LL was the header
	if (pRemove == this->pHeap->pUsedHead)
	{
		this->pHeap->pUsedHead = this->pHeap->pUsedHead->pUsedNext;
		//Update the node linking back to the head if the used header list had more than one node.
		if (this->pHeap->pUsedHead)
			this->pHeap->pUsedHead->pUsedPrev = nullptr;

	}
	pRemove->pUsedNext = nullptr;
	pRemove->pUsedPrev = nullptr;
}

void Mem::AdjustNextFit(Free * removedFreePtr, Free * joinedFreePtr)
{
	if (removedFreePtr == this->pHeap->pNextFit)
	{
		this->pHeap->pNextFit = joinedFreePtr;
	}
}

void Mem::CreateSecretPtr(Free * pFree)
{
	uint32_t secretPtrSize = sizeof(SecretPtr);
	uint32_t secretPtrLocation = (uint32_t)(pFree+1) + pFree->mBlockSize - secretPtrSize;
	SecretPtr* secret = placement_new((SecretPtr*)secretPtrLocation, SecretPtr);
	secret->pFree = pFree;
}

//Adjusts the below mAboveBlock var for the block below the passed in block, currentBlock.
void Mem::AdjustBelowMAboveBlockFree(Free * currentBlock)
{
//	SecretPtr * abovePtr = (SecretPtr*)((uint32_t)currentBlock - (uint32_t)sizeof(SecretPtr));
//	uint32_t distanceFromCurBlockToTop = (uint32_t)this->pHeap->mStats.heapTopAddr - (uint32_t)abovePtr
	uint32_t belowPtr = (uint32_t)(currentBlock + 1) + currentBlock->mBlockSize;

	Free * belowBlock = (Free *)belowPtr;
	if(this->VerifyPointerInHeapRange(belowBlock))
		belowBlock->mAboveBlockFree = currentBlock->mType == Block::Free ? true : false;

}

bool Mem::VerifyPointerInHeapRange(Free * ptr)
{
	uint32_t distanceFromTop = (uint32_t)ptr - (uint32_t)this->pHeap->mStats.heapTopAddr;
	uint32_t heapSpace = (uint32_t)this->pHeap->mStats.heapBottomAddr - (uint32_t)this->pHeap->mStats.heapTopAddr;

	bool withinRangeFromBottom = distanceFromTop < heapSpace;
	bool withinRangeFromTop = ptr >= this->pHeap->mStats.heapTopAddr;
	if (withinRangeFromBottom && withinRangeFromTop)
		return true;
	return false;
		
}

// ---  End of File ---------------
