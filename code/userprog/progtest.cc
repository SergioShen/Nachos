// progtest.cc 
//	Test routines for demonstrating that Nachos can load
//	a user program and execute it.  
//
//	Also, routines for testing the Console hardware device.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "synchconsole.h"
#include "addrspace.h"
#include "synch.h"

void RunUserProgram(int arg)
{
    currentThread->space->InitRegisters();
    currentThread->space->RestoreState();
    Machine *p = (Machine *)arg;
    p->Run();
}
//----------------------------------------------------------------------
// StartProcess
// 	Run a user program.  Open the executable, load it into
//	memory, and jump to it.
//----------------------------------------------------------------------

void newThread(char *threadName, char *filename) {
    OpenFile *executable = fileSystem->Open(filename);
    AddrSpace *space;
    Thread *forked = new Thread(threadName);
    space = new AddrSpace(executable);
    forked->space = space;
    forked->Fork(RunUserProgram, (int)machine);
}

void
StartProcess(char *filename)
{
    OpenFile *executable = fileSystem->Open(filename);
    AddrSpace *space;

    if (executable == NULL) {
	printf("Unable to open file %s\n", filename);
	return;
    }
    space = new AddrSpace(executable);    
    currentThread->space = space;

    // newThread("forked1", filename);
    // newThread("forked2", filename);
    // newThread("forked3", filename);

    // delete executable;			// close file

    space->InitRegisters();		// set the initial register values
    space->RestoreState();		// load page table register

    machine->Run();			// jump to the user progam
    ASSERT(FALSE);			// machine->Run never returns;
					// the address space exits
					// by doing the syscall "exit"
}

// Data structures needed for the console test.  Threads making
// I/O requests wait on a Semaphore to delay until the I/O completes.

static SynchConsole *console;
//----------------------------------------------------------------------
// ConsoleTest
// 	Test the console by echoing characters typed at the input onto
//	the output.  Stop when the user types a 'q'.
//----------------------------------------------------------------------

void 
ConsoleTest (char *in, char *out)
{
    char ch;

    console = new SynchConsole(in, out);
    printf("*** Use SynchConsole ***\n");
    for (;;) {
	ch = console->GetChar();
	console->PutChar(ch);	// echo it!
	if (ch == 'q') return;  // if q, quit
    }
}
