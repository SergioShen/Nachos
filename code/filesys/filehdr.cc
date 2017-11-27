// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize)
{ 
    numBytes = fileSize;
    numSectors  = divRoundUp(fileSize, SectorSize);
    DEBUG('f', "Allocate NumBytes: %d, NumSectors: %d\n", numBytes, numSectors);
    if (numSectors > 0 && freeMap->NumClear() < numSectors + (numSectors - 1) / NumDirect)
	    return FALSE;		// not enough space

    // First, allocate extra headers recursively
    if(numSectors > NumDirect) {
        nextSectorOfHeader = freeMap->Find();
        DEBUG('f', "Allocate extra header at %d\n", nextSectorOfHeader);
        FileHeader *extraHdr = new FileHeader;
        ASSERT(extraHdr->Allocate(freeMap, fileSize - NumDirect * SectorSize));
        DEBUG('f', "Writing extra header at %d back to disk.\n", nextSectorOfHeader);
	    extraHdr->WriteBack(nextSectorOfHeader);
        delete extraHdr;
    }
    else
        nextSectorOfHeader = -1;

    for (int i = 0; i < min(numSectors, NumDirect); i++)
	    dataSectors[i] = freeMap->Find();
    return TRUE;
}

bool FileHeader::Reallocate(BitMap *freeMap, int newFileSize) {
    int newNumSectors = divRoundUp(newFileSize, SectorSize);
    if(newNumSectors <= numSectors) {
        numBytes = newFileSize;
        return TRUE; // Do not need extend
    }
    
    DEBUG('f', "Reallocate Bytes: %d, Sectors: %d, OldBytes: %d, OldSectors: %d\n", newFileSize, newNumSectors, numBytes, numSectors);
    // Need extend
    int extendSectors = (newNumSectors + (newNumSectors - 1) / NumDirect) - (numSectors + (numSectors - 1) / NumDirect);
    if (freeMap->NumClear() < extendSectors)
	    return FALSE;		// not enough space

    // First, allocate extra headers recursively
    if(numSectors > NumDirect) {
        numBytes = newFileSize;
        numSectors = newNumSectors;
        FileHeader *extraHdr = new FileHeader;
        extraHdr->FetchFrom(nextSectorOfHeader);
        ASSERT(extraHdr->Reallocate(freeMap, newFileSize - NumDirect * SectorSize));
	    extraHdr->WriteBack(nextSectorOfHeader);
        delete extraHdr;
        return TRUE;
    }
    else {
        for (int i = numSectors; i < min(newNumSectors, NumDirect); i++) {
            dataSectors[i] = freeMap->Find();
            DEBUG('f', "Allocate space for file at %d\n", dataSectors[i]);
        }
        numBytes = newFileSize;
        numSectors = newNumSectors;
        if(newNumSectors > NumDirect) {
            // Need to allocate extra header
            nextSectorOfHeader = freeMap->Find();
            DEBUG('f', "Allocate extra header at %d\n", nextSectorOfHeader);
            FileHeader *extraHdr = new FileHeader;
            ASSERT(extraHdr->Allocate(freeMap, newFileSize - NumDirect * SectorSize));
            DEBUG('f', "Writing extra header at %d back to disk.\n", nextSectorOfHeader);
            extraHdr->WriteBack(nextSectorOfHeader);
            delete extraHdr;
        }
        return TRUE;
    }
    return FALSE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(BitMap *freeMap)
{
    // First, deallocate extra headers
    if(nextSectorOfHeader != -1) {
        FileHeader *extraHdr = new FileHeader;
        extraHdr->FetchFrom(nextSectorOfHeader);
        extraHdr->Deallocate(freeMap);
        delete extraHdr;
    }

    for (int i = 0; i < min(numSectors, NumDirect); i++) {
	ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
	freeMap->Clear((int) dataSectors[i]);
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    synchDisk->WriteSector(sector, (char *)this); 
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
    int sectorIndex = offset / SectorSize;
    if(sectorIndex >= NumDirect) {
        FileHeader *extraHdr = new FileHeader;
        extraHdr->FetchFrom(nextSectorOfHeader);
        int sector = extraHdr->ByteToSector(offset - NumDirect * SectorSize);
        delete extraHdr;
        return sector;
    }
    else
        return(dataSectors[offset / SectorSize]);
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------
void FileHeader::PrintBlocks() {
    int i;

    for (i = 0; i < min(numSectors, NumDirect); i++)
	    printf("%d ", dataSectors[i]);
}

void FileHeader::PrintContent() {
    int i, j, k;
    char *data = new char[SectorSize];

    for (i = k = 0; i < min(numSectors, NumDirect); i++) {
	synchDisk->ReadSector(dataSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
	    if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
		printf("%c", data[j]);
            else
		printf("\\%x", (unsigned char)data[j]);
	}
        printf("\n"); 
    }
    delete [] data;
}

void
FileHeader::Print()
{
    printf("FileHeader contents:\n");
    printf("File size: %d, Reference Num: %d\nCreate Time: %sLast Access Time: %sLast Modify Time: %s", numBytes, numRef, ctime(&createTime), ctime(&lastAccessTime), ctime(&lastModifyTime));
    printf("File blocks:\n");
    PrintBlocks();
    if(nextSectorOfHeader != -1) {
        FileHeader *extraHdr = new FileHeader;
        extraHdr->FetchFrom(nextSectorOfHeader);
        extraHdr->PrintBlocks();
        delete extraHdr;
    }

    printf("\nFile contents:\n");
    PrintContent();
    if(nextSectorOfHeader != -1) {
        FileHeader *extraHdr = new FileHeader;
        extraHdr->FetchFrom(nextSectorOfHeader);
        extraHdr->PrintContent();
        delete extraHdr;
    }
}
