#include "copyright.h"
#include "synchconsole.h"

static void ConsoleWriteDone(int arg) {
    SynchConsole *console = (SynchConsole *)arg;
    console->WriteDone();
}

static void ConsoleReadAvail(int arg) {
    SynchConsole *console = (SynchConsole *)arg;
    console->ReadAvail();
}

SynchConsole::SynchConsole(char *readFile, char *writeFile) {
    readAvail = new Semaphore("synch console read avail", 0);
    writeDone = new Semaphore("synch console write done", 0);
    lock = new Lock("synch console lock");
    console = new Console(readFile, writeFile, ConsoleReadAvail,
                    ConsoleWriteDone, (int)this);
}

SynchConsole::~SynchConsole() {
    delete console;
    delete readAvail;
    delete writeDone;
    delete lock;
}

void SynchConsole::PutChar(char ch) {
    lock->Acquire();
    console->PutChar(ch);
    writeDone->P();
    lock->Release();
}

char SynchConsole::GetChar() {
    lock->Acquire();
    readAvail->P();
    char ch = console->GetChar();
    lock->Release();
    return ch;
}

void SynchConsole::ReadAvail() {
    readAvail->V();
}

void SynchConsole::WriteDone() {
    writeDone->V();
}