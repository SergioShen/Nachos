// synch.cc 
//	Routines for synchronizing threads.  Three kinds of
//	synchronization routines are defined here: semaphores, locks 
//   	and condition variables (the implementation of the last two
//	are left to the reader).
//
// Any implementation of a synchronization routine needs some
// primitive atomic operation.  We assume Nachos is running on
// a uniprocessor, and thus atomicity can be provided by
// turning off interrupts.  While interrupts are disabled, no
// context switch can occur, and thus the current thread is guaranteed
// to hold the CPU throughout, until interrupts are reenabled.
//
// Because some of these routines might be called with interrupts
// already disabled (Semaphore::V for one), instead of turning
// on interrupts at the end of the atomic operation, we always simply
// re-set the interrupt state back to its original value (whether
// that be disabled or enabled).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "synch.h"
#include "system.h"

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	Initialize a semaphore, so that it can be used for synchronization.
//
//	"debugName" is an arbitrary name, useful for debugging.
//	"initialValue" is the initial value of the semaphore.
//----------------------------------------------------------------------

Semaphore::Semaphore(char* debugName, int initialValue)
{
    name = debugName;
    value = initialValue;
    queue = new List;
}

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	De-allocate semaphore, when no longer needed.  Assume no one
//	is still waiting on the semaphore!
//----------------------------------------------------------------------

Semaphore::~Semaphore()
{
    delete queue;
}

//----------------------------------------------------------------------
// Semaphore::P
// 	Wait until semaphore value > 0, then decrement.  Checking the
//	value and decrementing must be done atomically, so we
//	need to disable interrupts before checking the value.
//
//	Note that Thread::Sleep assumes that interrupts are disabled
//	when it is called.
//----------------------------------------------------------------------

void
Semaphore::P()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);	// disable interrupts
    
    DEBUG('t', "Semaphore %s P begin\n", getName());
    while (value == 0) { 			// semaphore not available
	queue->Append((void *)currentThread);	// so go to sleep
	currentThread->Sleep();
    } 
    value--; 					// semaphore available, 
						// consume its value
    DEBUG('t', "Semaphore %s P end\n", getName());

    (void) interrupt->SetLevel(oldLevel);	// re-enable interrupts
}

//----------------------------------------------------------------------
// Semaphore::V
// 	Increment semaphore value, waking up a waiter if necessary.
//	As with P(), this operation must be atomic, so we need to disable
//	interrupts.  Scheduler::ReadyToRun() assumes that threads
//	are disabled when it is called.
//----------------------------------------------------------------------

void
Semaphore::V()
{
    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    DEBUG('t', "Semaphore %s V begin\n", getName());
    thread = (Thread *)queue->Remove();
    if (thread != NULL)	   // make thread ready, consuming the V immediately
	scheduler->ReadyToRun(thread);
    value++;
    DEBUG('t', "Semaphore %s V end\n", getName());

    (void) interrupt->SetLevel(oldLevel);
}

// Dummy functions -- so we can compile our later assignments 
// Note -- without a correct implementation of Condition::Wait(), 
// the test case in the network assignment won't work!
Lock::Lock(char* debugName) {
    name = debugName;
    sema = new Semaphore("Inside semaphore", 1);
    thread = NULL;
}
Lock::~Lock() {
    delete sema;
}
void Lock::Acquire() {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    DEBUG('t', "Lock %s Acquire begin\n", getName());
    ASSERT(!isHeldByCurrentThread()); // lock can't be acquired twice by the same thread
    sema->P();
    thread = currentThread;
    DEBUG('t', "Lock %s Acquire end\n", getName());

    (void) interrupt->SetLevel(oldLevel);
}
void Lock::Release() {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    DEBUG('t', "Lock %s Release begin\n", getName());

    ASSERT(isHeldByCurrentThread()); // only the thread holding the lock can release it
    sema->V();
    thread = NULL;
    DEBUG('t', "Lock %s Release end\n", getName());

    (void) interrupt->SetLevel(oldLevel);
}
bool Lock::isHeldByCurrentThread() {
    return currentThread == thread;
}

Condition::Condition(char* debugName) {
    name = debugName;
    queue = new List;
}
Condition::~Condition() {
    delete queue;
}
void Condition::Wait(Lock* conditionLock) {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    conditionLock->Release();
    queue->Append((void *)currentThread);
    currentThread->Sleep();
    conditionLock->Acquire();

    (void) interrupt->SetLevel(oldLevel);
}
void Condition::Signal(Lock* conditionLock) {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    
    Thread *thread = (Thread *)queue->Remove();
    if (thread != NULL)
	    scheduler->ReadyToRun(thread);
    
    (void) interrupt->SetLevel(oldLevel);
}
void Condition::Broadcast(Lock* conditionLock) {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    
    Thread *thread = NULL;
    while(!queue->IsEmpty()) {
        thread = (Thread *)queue->Remove();
        if (thread != NULL)
            scheduler->ReadyToRun(thread);
    }
    
    (void) interrupt->SetLevel(oldLevel);
}

Barrier::Barrier(char *debugName, int threadNumber) {
    name = debugName;
    targetNumber = threadNumber;
    currentNumber = 0;
    lock = new Lock("Inside barrier lock");
    condition = new Condition("Inside barrier condition");
}
Barrier::~Barrier() {
    delete lock;
    delete condition;
}
void Barrier::Wait() {
    lock->Acquire();
    currentNumber++;
    while(currentNumber != targetNumber)
        condition->Wait(lock); // wait until all threads reach here
    condition->Broadcast(lock);
    lock->Release();
}

ReadWriteLock::ReadWriteLock(char* debugName) {
    name = debugName;
    write = new Lock("Inside rd lock");
    readerNumber = 0;
}
ReadWriteLock::~ReadWriteLock() {
    delete write;
}
void ReadWriteLock::ReaderAcquire() {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    readerNumber++;
    if(readerNumber == 1)
        write->Acquire();
    
    (void) interrupt->SetLevel(oldLevel);
}
void ReadWriteLock::ReaderRelease() {
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    readerNumber--;
    if(readerNumber == 0)
        write->Release();
    
    (void) interrupt->SetLevel(oldLevel);
}
void ReadWriteLock::WriterAcquire() {
    write->Acquire();
}
void ReadWriteLock::WriterRelease() {
    write->Release();
}