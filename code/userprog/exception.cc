// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "addrspace.h"

//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------

void FIFOReplace(TranslationEntry *pageTableEntry) {
    // Search for an empty block in TLB
    TranslationEntry *entry;
    int i;
    for (entry = NULL, i = 0; i < TLBSize; i++)
        if (!machine->tlb[i].valid) {
            // FOUND an empty block
            entry = &machine->tlb[i];
            break;
        }
    
    // If there is no empty block in TLB
    if(entry == NULL) {
        entry = machine->tlb + machine->nextVictim;
        machine->nextVictim = (machine->nextVictim + 1) % TLBSize;
        DEBUG('a', "Kick virtual page %d out of TLB, index: %d\n", entry->virtualPage, entry - machine->tlb);
        machine->pageTable[entry->virtualPage] = *entry; // Write back to page table
    }

    // Write TLB
    ASSERT(entry != NULL);
    *entry = *pageTableEntry;
    entry->valid = true;
    DEBUG('a', "Write virtual page %d into TLB, index: %d\n", entry->virtualPage, entry - machine->tlb);
}

void LRUReplace(TranslationEntry *pageTableEntry) {
    // Search for an empty block in TLB
    TranslationEntry *entry;
    int i, minTime = 0x7FFFFFFF, minIndex;
    for (entry = NULL, i = 0; i < TLBSize; i++) {
        if (!machine->tlb[i].valid) {
            // FOUND an empty block
            entry = &machine->tlb[i];
            break;
        }
        if(machine->tlb[i].lastUseTime < minTime) {
            minIndex = i;
            minTime = machine->tlb[i].lastUseTime;
        }
    }
    
    // If there is no empty block in TLB
    if(entry == NULL) {
        entry = machine->tlb + minIndex;
        DEBUG('a', "Kick virtual page %d out of TLB, index: %d\n", entry->virtualPage, entry - machine->tlb);
        machine->pageTable[entry->virtualPage] = *entry; // Write back to page table
    }

    // Write TLB
    ASSERT(entry != NULL);
    *entry = *pageTableEntry;
    entry->valid = true;
    entry->lastUseTime = stats->totalTicks; // Update last use time
    DEBUG('a', "Write virtual page %d into TLB, index: %d\n", entry->virtualPage, entry - machine->tlb);
}

void PageTableInvalidHandler(int badVAddr, unsigned int vpn) {
    ASSERT(machine->pageTable[vpn].valid == FALSE);

    // First, we need to find a empty physical page and initialize page table entry
    int ppn = machine->memUseage->Find();

    // Make sure we have enough space to allocate the program
    ASSERT(ppn != -1);

    DEBUG('a', "Allocate Vpage #%d of thread %s at Ppage #%d, time = %d\n", vpn, currentThread->getName(), ppn, stats->totalTicks);
    machine->pageTable[vpn].virtualPage = vpn;	// for now, virtual page # = phys page #
    machine->pageTable[vpn].physicalPage = ppn;
    machine->pageTable[vpn].lastUseTime = 0;
    machine->pageTable[vpn].valid = TRUE;
    machine->pageTable[vpn].use = FALSE;
    machine->pageTable[vpn].dirty = FALSE;
    machine->pageTable[vpn].readOnly = FALSE;  // if the code segment was entirely on 
                    // a separate page, we could set its 
                    // pages to be read-only
    bzero(&(machine->mainMemory[ppn * PageSize]), PageSize);
    
    // If this page is from executable file segment, we need to copy data
    NoffHeader noffH;
    OpenFile *executable = currentThread->space->executable;

    // Read noff header
    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
        SwapHeader(&noffH);
    
    // Copy in the code and data segments into memory
    int begin = vpn * PageSize, end = begin + PageSize;
    if ((noffH.code.size > 0)
        && (end > noffH.code.virtualAddr)
        && (begin < noffH.code.virtualAddr + noffH.code.size)) {
        // If page fault occurs at code segment
        if(begin < noffH.code.virtualAddr)
            begin = noffH.code.virtualAddr;
        if(end > noffH.code.virtualAddr + noffH.code.size) 
            end = noffH.code.virtualAddr + noffH.code.size;
        
        DEBUG('a', "Read code segment, at 0x%x, size %d\n", begin, end - begin);
        begin -= vpn * PageSize;
        end -= vpn * PageSize;
        for(int i = begin; i < end; i++) {
            int virtualAddr = vpn * PageSize + i;
            int inFileOffest = virtualAddr - noffH.code.virtualAddr;
            int physicalAddr = ppn * PageSize + i;
            executable->ReadAt(&(machine->mainMemory[physicalAddr]),
                1, noffH.code.inFileAddr + inFileOffest);
        }
    }
    else if ((noffH.initData.size > 0)
        && (end > noffH.initData.virtualAddr)
        && (begin < noffH.initData.virtualAddr + noffH.initData.size)) {
        // If page fault occurs at initData segment
        int begin = vpn * PageSize, end = begin + PageSize;
        if(begin < noffH.initData.virtualAddr)
            begin = noffH.initData.virtualAddr;
        if(end > noffH.initData.virtualAddr + noffH.initData.size) 
            end = noffH.initData.virtualAddr + noffH.initData.size;
        DEBUG('a', "Read initData segment, at 0x%x, size %d\n", begin, end - begin);

        begin -= vpn * PageSize;
        end -= vpn * PageSize;
        for(int i = begin; i < end; i++) {
            int virtualAddr = vpn * PageSize + i;
            int inFileOffest = virtualAddr - noffH.initData.virtualAddr;
            int physicalAddr = ppn * PageSize + i;
            executable->ReadAt(&(machine->mainMemory[physicalAddr]),
                1, noffH.initData.inFileAddr + inFileOffest);
        }
    }
}

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if ((which == SyscallException) && (type == SC_Halt)) {
	DEBUG('a', "Shutdown, initiated by user program.\n");
   	interrupt->Halt();
    }
    else if(which == PageFaultException) {
#ifdef USE_TLB
        machine->totalMiss++;
        DEBUG('a', "TLB miss, %d in total\n", machine->totalMiss);
        int badVAddr = machine->registers[BadVAddrReg];
        unsigned int vpn = (unsigned) badVAddr / PageSize;
        unsigned int offset = (unsigned) badVAddr % PageSize;

        // Check the page table
        ASSERT(vpn < machine->pageTableSize);

        // Handle REAL page fault
        if(!machine->pageTable[vpn].valid) {
            // We need to read page from executable file
            DEBUG('a', "Page table miss\n");
            PageTableInvalidHandler(badVAddr, vpn);
        }

        TranslationEntry *pageTableEntry = &machine->pageTable[vpn];

        // Handle TLB miss
        // FIFOReplace(pageTableEntry);
        LRUReplace(pageTableEntry);
#else
        ASSERT(false);
#endif
    }
    else if((which == SyscallException) && (type == SC_Exit)) {
        int returnValue = machine->ReadRegister(4);
        DEBUG('a', "Exit called by thread %s with return value %d\n", currentThread->getName(), returnValue);
        currentThread->Finish();
    }
    else {
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }
}
