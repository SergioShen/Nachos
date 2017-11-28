#include "syscall.h"

int main() {
    char a[6], b[6];
    OpenFileId fd1;
    OpenFileId fd2;
    a[0] = 'a';
    a[1] = '.';
    a[2] = 't';
    a[3] = 'x';
    a[4] = 't';
    a[5] = '\0';
    Create(a);
    fd1 = Open(a);
    fd2 = Open(a);
    Write(a, 5, fd1);
    Read(b, 5, fd2);
    Close(fd1);
    Close(fd2);
    Exit('a' - b[0]);
}