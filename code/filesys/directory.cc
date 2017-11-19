// directory.cc 
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//	Fixing this is one of the parts to the assignment.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "directory.h"

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory(int size)
{
    table = new DirectoryEntry[size];
    tableSize = size;
    for (int i = 0; i < tableSize; i++)
	table[i].inUse = FALSE;
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{ 
    delete [] table;
} 

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void
Directory::FetchFrom(OpenFile *file)
{
    (void) file->ReadAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void
Directory::WriteBack(OpenFile *file)
{
    (void) file->WriteAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::FindIndex(char *name)
{
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse && !strncmp(table[i].name, name, FileNameMaxLen))
	    return i;
    return -1;		// name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't 
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::RecursivelyFind(char *name, char *fullPath)
{
    ASSERT(name[strlen(name) - 1] != '/');

    char *p = strchr(name, '/');
    if(p == NULL) {
        int i = FindIndex(name);
        if (i != -1)
            return table[i].sector;
    }
    else {
        // It contains directory
        char *path = new char[strlen(name) + 1];
        strncpy(path, name, p - name);
        path[p - name] = '\0';
        
        // Find path in directory
        if(FindIndex(path) == -1) {
            // Path does not exist, return FALSE
            char *currentPath = new char[strlen(fullPath) + 1];
            strcpy(currentPath, fullPath);
            currentPath[strlen(fullPath) - strlen(name)] = '\0';
            printf("%s%s: No such file or directory\n", currentPath, path);
            delete currentPath;
            delete path;
            return -1;
        }

        if(!table[FindIndex(path)].isDirectory) {
            // Path is not a directory
            char *currentPath = new char[strlen(fullPath) + 1];
            strcpy(currentPath, fullPath);
            currentPath[strlen(fullPath) - strlen(name)] = '\0';
            printf("%s%s: Not a directory\n", currentPath, path);
            delete currentPath;
            delete path;
            return -1;
        }

        // Write directory file
        Directory *directory = new Directory(NumDirEntries);
        OpenFile *directoryFile = new OpenFile(Find(path));
        directory->FetchFrom(directoryFile);
        int result = directory->RecursivelyFind(p + 1, fullPath);
        delete path;
        delete directory;
        delete directoryFile;
        return result;
    }
    return -1;
}

int
Directory::Find(char *name)
{
    char *temp = new char[strlen(name) + 1];
    strcpy(temp, name);
    if(temp[strlen(temp) - 1] == '/')
        temp[strlen(temp) - 1] = '\0'; // remove suffix '/'
    int result = RecursivelyFind(temp, temp);
    delete temp;
    return result;
}
//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool
Directory::RecursivelyAdd(char *name, char *fullPath, int newSector, bool isDirectory)
{
    if(isDirectory)
        DEBUG('f', "%s is a directory\n", name);
    else
        DEBUG('f', "%s is a normal file\n", name);

    ASSERT(name[strlen(name) - 1] != '/');

    char *p = strchr(name, '/');
    if(p == NULL) {
        // It is a normal file
        if (FindIndex(name) != -1)
            return FALSE;

        for (int i = 0; i < tableSize; i++)
            if (!table[i].inUse) {
                table[i].inUse = TRUE;
                table[i].isDirectory = isDirectory;
                strncpy(table[i].name, name, FileNameMaxLen); 
                table[i].sector = newSector;
                DEBUG('f', "Allocate %s header at sector %d\n", name, newSector);
            return TRUE;
        }
        return FALSE;	// no space.  Fix when we have extensible files.
    }
    else {
        // It contains directory
        char *path = new char[strlen(name) + 1];
        strncpy(path, name, p - name);
        path[p - name] = '\0';

        // Find path in directory
        if(FindIndex(path) == -1) {
            // Path does not exist, return FALSE
            char *currentPath = new char[strlen(fullPath) + 1];
            strcpy(currentPath, fullPath);
            currentPath[strlen(fullPath) - strlen(name)] = '\0';
            printf("%s%s: No such file or directory\n", currentPath, path);
            delete currentPath;
            delete path;
            return FALSE;
        }

        if(!table[FindIndex(path)].isDirectory) {
            // Path is not a directory
            char *currentPath = new char[strlen(fullPath) + 1];
            strcpy(currentPath, fullPath);
            currentPath[strlen(fullPath) - strlen(name)] = '\0';
            printf("%s%s: Not a directory\n", currentPath, path);
            delete currentPath;
            delete path;
            return FALSE;
        }

        // Write directory file
        Directory *directory = new Directory(NumDirEntries);
        OpenFile *directoryFile = new OpenFile(Find(path));
        directory->FetchFrom(directoryFile);
        if(!directory->RecursivelyAdd(p + 1, fullPath, newSector, isDirectory)) {
            // Add file failed
            delete path;
            delete directory;
            delete directoryFile;
            return FALSE;
        }
        directory->WriteBack(directoryFile);

        delete path;
        delete directory;
        delete directoryFile;
        return TRUE;
    }
}

bool
Directory::Add(char *name, int newSector)
{
    char *temp = new char[strlen(name) + 1];
    bool isDirectory = FALSE;
    strcpy(temp, name);
    if(temp[strlen(temp) - 1] == '/') {
        isDirectory = TRUE;
        temp[strlen(temp) - 1] = '\0'; // remove suffix '/'
    }
    bool result = RecursivelyAdd(temp, temp, newSector, isDirectory);
    delete temp;
    return result;
}
//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory. 
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------
bool
Directory::RecursivelyRemove(char *name, char *fullPath)
{
    ASSERT(name[strlen(name) - 1] != '/');

    char *p = strchr(name, '/');
    if(p == NULL) {
        int i = FindIndex(name);
        if (i != -1) {
            table[i].inUse = FALSE;
            return TRUE;
        }
    }
    else {
        // It contains directory
        char *path = new char[strlen(name) + 1];
        strncpy(path, name, p - name);
        path[p - name] = '\0';
        
        // Find path in directory
        if(FindIndex(path) == -1) {
            // Path does not exist, return FALSE
            char *currentPath = new char[strlen(fullPath) + 1];
            strcpy(currentPath, fullPath);
            currentPath[strlen(fullPath) - strlen(name)] = '\0';
            printf("%s%s: No such file or directory\n", currentPath, path);
            delete currentPath;
            delete path;
            return FALSE;
        }

        if(!table[FindIndex(path)].isDirectory) {
            // Path is not a directory
            char *currentPath = new char[strlen(fullPath) + 1];
            strcpy(currentPath, fullPath);
            currentPath[strlen(fullPath) - strlen(name)] = '\0';
            printf("%s%s: Not a directory\n", currentPath, path);
            delete currentPath;
            delete path;
            return FALSE;
        }

        // Write directory file
        Directory *directory = new Directory(NumDirEntries);
        OpenFile *directoryFile = new OpenFile(Find(path));
        directory->FetchFrom(directoryFile);
        if(!directory->RecursivelyRemove(p + 1, fullPath)) {
            delete path;
            delete directory;
            delete directoryFile;
            return FALSE;
        }
        directory->WriteBack(directoryFile);
        delete path;
        delete directory;
        delete directoryFile;
        return TRUE;
    }
    return FALSE;
}

bool
Directory::Remove(char *name)
{ 
    char *temp = new char[strlen(name) + 1];
    strcpy(temp, name);
    if(temp[strlen(temp) - 1] == '/')
        temp[strlen(temp) - 1] = '\0'; // remove suffix '/'
    bool result = RecursivelyRemove(temp, temp);
    delete temp;
    return result;	
}

//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory. 
//----------------------------------------------------------------------
void
Directory::RecursivelyList(int layer)
{
   for (int i = 0; i < tableSize; i++)
	if (table[i].inUse) {
        for(int j = 0; j < layer; j++)
            printf("  ");
        if(table[i].isDirectory) {
	        printf("+%s\n", table[i].name);
            Directory *directory = new Directory(NumDirEntries);
            OpenFile *directoryFile = new OpenFile(table[i].sector);
            directory->FetchFrom(directoryFile);
            directory->RecursivelyList(layer + 1);
            delete directory;
            delete directoryFile;
        }
        else {
            printf(" %s\n", table[i].name);
        }
    }
}

void
Directory::List()
{
    RecursivelyList(0);
}

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void
Directory::RecursivelyPrint(char *currentPath)
{ 
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (int i = 0; i < tableSize; i++)
	if (table[i].inUse) {
        if(table[i].isDirectory) {
            printf("Name: %s/%s/, Sector: %d\n", currentPath, table[i].name, table[i].sector);
            hdr->FetchFrom(table[i].sector);
            hdr->Print();
            Directory *directory = new Directory(NumDirEntries);
            OpenFile *directoryFile = new OpenFile(table[i].sector);
            char *path = new char[strlen(currentPath) + FileNameMaxLen + 2];
            path[0] = '\0';
            strcat(path, currentPath);
            strcat(path, "/");
            strcat(path, table[i].name);
            directory->FetchFrom(directoryFile);
            directory->RecursivelyPrint(path);
            delete path;
            delete directory;
            delete directoryFile;
        }
        else {
            printf("Name: %s/%s, Sector: %d\n", currentPath, table[i].name, table[i].sector);
            hdr->FetchFrom(table[i].sector);
            hdr->Print();
        }
	}
    printf("\n");
    delete hdr;
}

void
Directory::Print()
{ 
    char *currentPath = "";
    RecursivelyPrint(currentPath);
}
