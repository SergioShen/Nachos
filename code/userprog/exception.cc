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
int FindVictimPhysPage() {
    return Random() % NumPhysPages;
}
int getHashCode(int vpn) {
    return ((vpn * vpn) >> 4) % NumPhysPages;
}

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
        DEBUG('v', "Kick virtual page %d out of TLB, index: %d\n", entry->virtualPage, entry - machine->tlb);
        machine->pageTable[entry->virtualPage] = *entry; // Write back to page table
    }

    // Write TLB
    ASSERT(entry != NULL);
    *entry = *pageTableEntry;
    entry->valid = true;
    DEBUG('v', "Write virtual page %d into TLB, index: %d\n", entry->virtualPage, entry - machine->tlb);
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
        DEBUG('v', "Kick virtual page %d out of TLB, index: %d\n", entry->virtualPage, entry - machine->tlb);
#ifdef USE_INVERTED_TABLE
        TranslationEntry *next = machine->invertedPageTable[entry->physicalPage].next;
        machine->invertedPageTable[entry->physicalPage] = *entry;
        machine->invertedPageTable[entry->physicalPage].next = next;
#else
        machine->pageTable[entry->virtualPage] = *entry; // Write back to page table
#endif
    }

    // Write TLB
    ASSERT(entry != NULL);
    *entry = *pageTableEntry;
    entry->valid = true;
    entry->lastUseTime = stats->totalTicks; // Update last use time
    DEBUG('v', "Write virtual page %d into TLB, index: %d\n", entry->virtualPage, entry - machine->tlb);
}

int PageTableInvalidHandler(int badVAddr, unsigned int vpn) {
    // First, we need to find a empty physical page and initialize page table entry
    int ppn = machine->memUseage->Find();

    // Make sure we have enough space to allocate the program
#ifdef USE_INVERTED_TABLE
    if(ppn == -1) {
        bool sameThread = FALSE;
        int victim = FindVictimPhysPage();
        ASSERT(victim >= 0 && victim < NumPhysPages);

        DEBUG('v', "Kick physical page #%d out of main memory, thread ID = %d, Vpn = %d\n",
                victim, machine->invertedPageTable[victim].threadID,
                machine->invertedPageTable[victim].virtualPage);
        // Search whether victim is in TLB, if so, set as invalid
        if(machine->invertedPageTable[victim].threadID == currentThread->getThreadID()) {
            for(int i = 0; i < TLBSize; i++) {
                if(machine->tlb[i].valid && machine->tlb[i].physicalPage == victim) {
                    machine->tlb[i].valid = FALSE;
                    break;
                }
            }
        }

        // Check flags, write to swap area
        if(machine->invertedPageTable[victim].dirty) {
            DEBUG('v', "Ppage #%d Vpage #%d of thread %d is dirty, write into swap area\n", victim,
                    machine->invertedPageTable[victim].virtualPage,
                    machine->invertedPageTable[victim].threadID);
            DEBUG('v', "Now %d pages in swap area\n", ++machine->swapAreaSize);
            // Set up a new item in swap area
            SwapAreaEntry *swapEntry = new SwapAreaEntry();
            swapEntry->entry = machine->invertedPageTable[victim];
            for(int i = 0; i < PageSize; i++) {
                swapEntry->content[i] = machine->mainMemory[victim * PageSize + i];
            }

            // Insert into linked table
            swapEntry->next = machine->swapArea;
            machine->swapArea = swapEntry;
        }

        // All things are done, victim is to be kicked out
        ppn = victim;
    }

    // Remove from hash table
    // Whether we find an empty page or not, we need to do this
    // Because we only set inverted page table entry as invalid when we clear
    // a physical page but not delete from hash table. So when we reallocate
    // a page, we need to do this thing
    int hashCode = getHashCode(machine->invertedPageTable[ppn].virtualPage);
    if(machine->hashTable[hashCode] != NULL && machine->hashTable[hashCode]->physicalPage == ppn) {
        // At the front of the list
        machine->hashTable[hashCode] = machine->hashTable[hashCode]->next;
    }
    else {
        TranslationEntry *temp = machine->hashTable[hashCode];
        while(temp != NULL && temp->next != NULL && temp->next->physicalPage != ppn) {
            temp = temp->next;
        }
        if(temp != NULL && temp->next != NULL)
            temp->next = temp->next->next;
    }
#endif
    ASSERT(ppn != -1);

    DEBUG('v', "Allocate Vpage #%d of thread %s at Ppage #%d, time = %d\n", vpn, currentThread->getName(), ppn, stats->totalTicks);
#ifdef USE_INVERTED_TABLE
    hashCode = getHashCode(vpn);
    machine->invertedPageTable[ppn].threadID = currentThread->getThreadID();
    machine->invertedPageTable[ppn].virtualPage = vpn;
    machine->invertedPageTable[ppn].lastUseTime = 0;
    machine->invertedPageTable[ppn].valid = TRUE;
    machine->invertedPageTable[ppn].use = FALSE;
    machine->invertedPageTable[ppn].dirty = FALSE;
    machine->invertedPageTable[ppn].readOnly = FALSE;
    
    // Insert into hash table
    machine->invertedPageTable[ppn].next = machine->hashTable[hashCode];
    machine->hashTable[hashCode] = &machine->invertedPageTable[ppn];
#else
    machine->pageTable[vpn].virtualPage = vpn;	// for now, virtual page # = phys page #
    machine->pageTable[vpn].physicalPage = ppn;
    machine->pageTable[vpn].lastUseTime = 0;
    machine->pageTable[vpn].valid = TRUE;
    machine->pageTable[vpn].use = FALSE;
    machine->pageTable[vpn].dirty = FALSE;
    machine->pageTable[vpn].readOnly = FALSE;  // if the code segment was entirely on 
                    // a separate page, we could set its 
                    // pages to be read-only
#endif
    bzero(&(machine->mainMemory[ppn * PageSize]), PageSize);

#ifdef USE_INVERTED_TABLE
    // If this page is in swap area, just read from swap area
    SwapAreaEntry *temp = machine->swapArea;
    // It is the first block in the list
    if(temp != NULL && temp->entry.threadID == currentThread->getThreadID() && temp->entry.virtualPage == vpn) {
        // Copy content
        machine->invertedPageTable[ppn].use = temp->entry.use;
        machine->invertedPageTable[ppn].dirty = temp->entry.dirty;
        machine->invertedPageTable[ppn].readOnly = temp->entry.readOnly;
        for(int i = 0; i < PageSize; i++)
            machine->mainMemory[ppn * PageSize + i] = temp->content[i];
        DEBUG('v', "Restore Vpage #%d of thread %d from swap area\n", vpn, currentThread->getThreadID());
        DEBUG('v', "Now %d pages in swap area\n", --machine->swapAreaSize);
        // Remove from swap area
        machine->swapArea = temp->next;

        return ppn;
    }
    // It is not the first block
    while(temp != NULL && temp->next != NULL) {
        if(temp->next->entry.threadID == currentThread->getThreadID() && temp->next->entry.virtualPage == vpn) {
            // Copy content
            machine->invertedPageTable[ppn].use = temp->next->entry.use;
            machine->invertedPageTable[ppn].dirty = temp->next->entry.dirty;
            machine->invertedPageTable[ppn].readOnly = temp->next->entry.readOnly;
            for(int i = 0; i < PageSize; i++)
                machine->mainMemory[ppn * PageSize + i] = temp->next->content[i];

            // Remove from swap area
            temp->next = temp->next->next;

            DEBUG('v', "Restore Vpage #%d of thread %d from swap area\n", vpn, currentThread->getThreadID());
            DEBUG('v', "Now %d pages in swap area\n", --machine->swapAreaSize);
            return ppn;
        }
        temp = temp->next;
    }
#endif
    
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
        
        int size = end - begin;
        int physicalBegin = ppn * PageSize + (begin - vpn * PageSize);
        int inFileBegin = noffH.code.inFileAddr + (begin - noffH.code.virtualAddr);
        DEBUG('v', "Read code segment, at 0x%x, size %d\n", begin, size);
        executable->ReadAt(&(machine->mainMemory[physicalBegin]),
                size, inFileBegin);
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

        int size = end - begin;
        int physicalBegin = ppn * PageSize + (begin - vpn * PageSize);
        int inFileBegin = noffH.initData.inFileAddr + (begin - noffH.initData.virtualAddr);
        DEBUG('v', "Read initData segment, at 0x%x, size %d\n", begin, size);
        executable->ReadAt(&(machine->mainMemory[physicalBegin]),
                size, inFileBegin);
    }
    return ppn;
}

void CreateSyscallHandler() {
    currentThread->SaveUserState();
    // First, get the length of filename
    int fileNameBase = machine->ReadRegister(4);
    int value;
    int length = 0;
    do {
        machine->ReadMem(fileNameBase++, 1, &value);
        length++;
    } while(value != '\0');

    // Copy filename
    char *fileName = new char[length];
    fileNameBase -= length; length--;
    for(int i = 0; i < length; i++) {
        machine->ReadMem(fileNameBase++, 1, &value);
        fileName[i] = char(value);
    }
    fileName[length] = '\0';
    DEBUG('a', "File name: %s\n", fileName);

    bool result = fileSystem->Create(fileName, 0);

    if(result)
        DEBUG('a', "Create file %s done\n", fileName);
    else
        DEBUG('a', "Can not create file %s\n", fileName);
    delete fileName;
    currentThread->RestoreUserState();
}

void OpenSyscallHandler() {
    currentThread->SaveUserState();
    // First, get the length of filename
    int fileNameBase = machine->ReadRegister(4);
    int value;
    int length = 0;
    do {
        machine->ReadMem(fileNameBase++, 1, &value);
        length++;
    } while(value != '\0');

    // Copy filename
    char *fileName = new char[length];
    fileNameBase -= length; length--;
    for(int i = 0; i < length; i++) {
        machine->ReadMem(fileNameBase++, 1, &value);
        fileName[i] = char(value);
    }
    fileName[length] = '\0';
    DEBUG('a', "File name: %s\n", fileName);

    OpenFile *openFile = fileSystem->Open(fileName);

    if(openFile != NULL)
        DEBUG('a', "Open file %s done\n", fileName);
    else
        DEBUG('a', "Can not open file %s\n", fileName);

    currentThread->RestoreUserState();
    machine->WriteRegister(2, (int)openFile);
}

void CloseSyscallHandler() {
    currentThread->SaveUserState();
    OpenFile *openFile = (OpenFile *)machine->ReadRegister(4);
    DEBUG('a', "Close File\n");
    delete openFile;
    currentThread->RestoreUserState();
    machine->WriteRegister(2, 0);
}

void WriteSyscallHandler() {
    currentThread->SaveUserState();    
    int buffer = machine->ReadRegister(4);
    int size = machine->ReadRegister(5);
    OpenFile *openFile = (OpenFile *)machine->ReadRegister(6);

    // Copy data from user space into kernel space
    char *kernelBuffer = new char[size + 1];
    int value, i;
    for(i = 0; i < size; i++) {
        bool success = machine->ReadMem(buffer++, 1, &value);
        if(!success) {
            buffer--; i--;
            continue;
        }
        kernelBuffer[i] = char(value);
    }
    kernelBuffer[i] = '\0';

    // Write into file
    int result = openFile->Write(kernelBuffer, size);
    DEBUG('a', "Write %d bytes into file(%d bytes requested)\nContent: %s\n", result, size, kernelBuffer);
    delete kernelBuffer;
    currentThread->RestoreUserState();
    machine->WriteRegister(2, result);
}

void ReadSyscallHandler() {
    currentThread->SaveUserState();
    int buffer = machine->ReadRegister(4);
    int size = machine->ReadRegister(5);
    OpenFile *openFile = (OpenFile *)machine->ReadRegister(6);

    char *kernelBuffer = new char[size];

    // Read from file into kernel space
    int result = openFile->Read(kernelBuffer, size);

    // Write into user space
    for(int i = 0; i < result; i++) {
        bool success = machine->WriteMem(buffer++, 1, (int)kernelBuffer[i]);
        if(!success) {
            buffer--; i--;
        }
    }

    DEBUG('a', "Read %d bytes from file(%d bytes requested)\nContent: %s\n", size, result, kernelBuffer);
    delete kernelBuffer;
    currentThread->RestoreUserState();    
    machine->WriteRegister(2, result);
}

void ExecRoutine(int arg) {
    currentThread->space->InitRegisters();
    currentThread->space->RestoreState();
    Machine *p = (Machine *)arg;
    p->Run();
}

void ExecSyscallHandler() {
    currentThread->SaveUserState();
    // First, get the length of filename
    int fileNameBase = machine->ReadRegister(4);
    int value;
    int length = 0;
    do {
        machine->ReadMem(fileNameBase++, 1, &value);
        length++;
    } while(value != '\0');

    // Copy filename
    char *fileName = new char[length];
    fileNameBase -= length; length--;
    for(int i = 0; i < length; i++) {
        machine->ReadMem(fileNameBase++, 1, &value);
        fileName[i] = char(value);
    }
    fileName[length] = '\0';
    DEBUG('a', "Executable file name: %s\n", fileName);

    OpenFile *executable = fileSystem->Open(fileName);

    if(executable != NULL)
        DEBUG('a', "Open file %s done\n", fileName);
    else {
        DEBUG('a', "Can not open file %s\n", fileName);
        machine->WriteRegister(2, (int)executable);
        return;
    }

    // Create an address space and a new thread
    AddrSpace *addrSpace = new AddrSpace(executable);
    Thread *forked = new Thread(fileName);
    forked->space = addrSpace;

    // Run user program
    forked->Fork(ExecRoutine, (int)machine);
    DEBUG('t', "Exec done\n");
    currentThread->RestoreUserState();
    machine->WriteRegister(2, (int)addrSpace);
}

void ForkRoutine(int arg) {
    currentThread->space->RestoreState();
    
    // Set PC to *arg*
    machine->WriteRegister(PCReg, arg);
    machine->WriteRegister(NextPCReg, arg + 4);
    machine->Run();
}

void ForkSyscallHandler() {
    currentThread->SaveUserState(); // Save Registers
    int funcAddr = machine->ReadRegister(4);

    // Create a new thread in the same addrspace
    Thread *thread = new Thread("forked thread");
    thread->space = currentThread->space;
    thread->space->refNum++; // Increase RefNum
    // thread->RestoreUserState();
    thread->Fork(ForkRoutine, funcAddr);
    currentThread->RestoreUserState(); // Save Registers
}

void YieldSyscallHandler() {
    currentThread->SaveUserState(); // Save Registers
    currentThread->Yield();
    currentThread->RestoreUserState();
}

void JoinSyscallHandler() {
    currentThread->SaveUserState();
    AddrSpace *addrSpace = (AddrSpace *)machine->ReadRegister(4);
    addrSpace->Wait();
    DEBUG('a', "Join finished\n");
    int exitCode = currentThread->joinReturnValue;
    DEBUG('a', "Get join exit code: %d", exitCode);
    currentThread->RestoreUserState();
    machine->WriteRegister(2, exitCode);
}

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if (which == SyscallException) {
        if (type == SC_Halt) {
	        DEBUG('a', "Shutdown, initiated by user program.\n");
           interrupt->Halt();
        }
        else if(type == SC_Exit) {
            DEBUG('a', "Syscall: Exit\n");
            int exitCode = machine->ReadRegister(4);
            printf("\nThread %s finished with exit code %d\n\n", currentThread->getName(), exitCode);
            currentThread->space->refNum--;
            DEBUG('a', "AddrSpace reference num: %d\n", currentThread->space->refNum);
            if(currentThread->space->refNum == 0) {
                currentThread->space->Broadcast(exitCode);
            }        
            currentThread->Finish();
        }
        else if(type == SC_Create) {
            DEBUG('a', "Syscall: Create\n");
            CreateSyscallHandler();
        }
        else if(type == SC_Open) {
            DEBUG('a', "Syscall: Open\n");
            OpenSyscallHandler();
        }
        else if(type == SC_Close) {
            DEBUG('a', "Syscall: Close\n");
            CloseSyscallHandler();
        }
        else if(type == SC_Write) {
            DEBUG('a', "Syscall: Write\n");
            WriteSyscallHandler();
        }
        else if(type == SC_Read) {
            DEBUG('a', "Syscall: Read\n");
            ReadSyscallHandler();
        }
        else if(type == SC_Exec) {
            DEBUG('a', "Syscall: Exec\n");
            ExecSyscallHandler();
        }
        else if(type == SC_Fork) {
            DEBUG('a', "Syscall: Fork\n");
            ForkSyscallHandler();
        }
        else if(type == SC_Yield) {
            DEBUG('a', "Syscall: Yield\n");
            YieldSyscallHandler();
        }
        else if(type == SC_Join) {
            DEBUG('a', "Syscall: Join\n");
            JoinSyscallHandler();
        }

        // Increase PC
        machine->ReturnFromSyscall();        
    }
    else if(which == PageFaultException) {
#ifdef USE_TLB
        machine->totalMiss++;
        DEBUG('v', "TLB miss, %d in total\n", machine->totalMiss);
        int badVAddr = machine->registers[BadVAddrReg];
        unsigned int vpn = (unsigned) badVAddr / PageSize;
        unsigned int offset = (unsigned) badVAddr % PageSize;
        TranslationEntry *pageTableEntry = NULL;

#ifdef USE_INVERTED_TABLE
        int hashCode = getHashCode(vpn);
        pageTableEntry = machine->hashTable[hashCode];
        // Search hash table
        while(pageTableEntry != NULL) {
            if(pageTableEntry->valid
                && currentThread->getThreadID() == pageTableEntry->threadID
                && pageTableEntry->virtualPage == vpn)
                break; // FOUND!
            pageTableEntry = pageTableEntry->next;
        }

        // Handle REAL page fault
        if(pageTableEntry == NULL) {
            // We need to read page from executable file
            DEBUG('v', "Page table miss\n");
            pageTableEntry = &machine->invertedPageTable[PageTableInvalidHandler(badVAddr, vpn)];
        }
#else
        // Check the page table
        ASSERT(vpn < machine->pageTableSize);

        // Handle REAL page fault
        if(!machine->pageTable[vpn].valid) {
            // We need to read page from executable file
            DEBUG('v', "Page table miss\n");
            PageTableInvalidHandler(badVAddr, vpn);
        }

        pageTableEntry = &machine->pageTable[vpn];
#endif // USE_INVERTED_TABLE

        // Handle TLB miss
        // FIFOReplace(pageTableEntry);
        LRUReplace(pageTableEntry);
#else
        ASSERT(false);
#endif
    }
    else {
	printf("Unexpected user mode exception %d %d\n", which, type);
	ASSERT(FALSE);
    }
}
