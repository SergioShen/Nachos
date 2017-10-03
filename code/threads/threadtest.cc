// threadtest.cc 
//	Simple test case for the threads assignment.
//
//	Create two threads, and have them context switch
//	back and forth between themselves by calling Thread::Yield, 
//	to illustratethe inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"

// testnum is set in main.cc
int testnum = 1;

//----------------------------------------------------------------------
// SimpleThread
// 	Loop 5 times, yielding the CPU to another ready thread 
//	each iteration.
//
//	"which" is simply a number identifying the thread, for debugging
//	purposes.
//----------------------------------------------------------------------

void
SimpleThread(int which)
{
    int num;
    
    for (num = 0; num < 5; num++) {
	printf("*** thread %d looped %d times\n", which, num);
        currentThread->Yield();
    }
}

void
SimpleThreadWithPriority(int prior)
{
    int num;
    
    for (num = 0; num < 3; num++) {
        printf("*** thread looped %d times, priority: %d, %s\n", num, prior, currentThread->getName());
    }
}

void
SimpleThreadTimeSlice(int prior)
{
    int num;
    
    for (num = 0; num < 80; num++) {
        printf("*** thread looped %d times, priority: %d, %s\n", num, prior, currentThread->getName());
        interrupt->SetLevel(IntOff);
        interrupt->SetLevel(IntOn);
    }
}

//----------------------------------------------------------------------
// ThreadTest1
// 	Set up a ping-pong between two threads, by forking a thread 
//	to call SimpleThread, and then calling SimpleThread ourselves.
//----------------------------------------------------------------------

void
ThreadTest1()
{
    DEBUG('t', "Entering ThreadTest1\n");

    Thread *t = new Thread("forked thread");

    t->Fork(SimpleThread, 1);
    SimpleThread(0);
    printThreadStatus();
}

void ThreadTest2() {
    DEBUG('t', "Entering ThreadTest128\n");
    for(int i = 1; i < 128; i++) {
        Thread *t = new Thread("test thread");
        t->Fork(SimpleThread, i);
    }
    SimpleThread(0);
    printThreadStatus();
}

void ThreadTest3() {
    DEBUG('t', "Entering ThreadTest150\n");
    for(int i = 1; i < 150; i++) {
        Thread *t = new Thread("test thread");
        t->Fork(SimpleThread, i);
    }
    SimpleThread(0);
    printThreadStatus();
}

void ThreadTest4() {
    DEBUG('t', "Entering ThreadTestPriority\n");
    int priors[8] = { 4, 2, 9, 12, 0, 15, 7, 13 };
    for(int i = 0; i < 8; i++) {
        Thread *t = new Thread("test thread", priors[i]);
        t->Fork(SimpleThreadWithPriority, priors[i]);
    }
    SimpleThreadWithPriority(8);
}

void ThreadTest5() {
    DEBUG('t', "Entering ThreadTestTimeSlice\n");
    int priors[4] = { 5, 2, 11, 14 };
    char* names[4] = { "forked 1", "forked 2", "forked 3", "forked 4" };
    for(int i = 0; i < 4; i++) {
        Thread *t = new Thread(names[i], priors[i]);
        t->Fork(SimpleThreadTimeSlice, priors[i]);
    }
    SimpleThreadTimeSlice(8);
}


//----------------------------------------------------------------------
// ThreadTest
// 	Invoke a test routine.
//----------------------------------------------------------------------

void
ThreadTest()
{
    switch (testnum) {
    case 1:
	ThreadTest1();
    break;
    case 2:
    ThreadTest2();
    break;
    case 3:
    ThreadTest3();
    break;
    case 4:
    ThreadTest4();
    break;
    case 5:
    ThreadTest5();
    break;
    default:
	printf("No test specified.\n");
	break;
    }
}

