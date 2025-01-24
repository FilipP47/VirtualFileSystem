#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#define MAX_FILES        64
#define INODE_BLOCK_NUM  16
#define MAX_FILENAME_LENGTH    32

typedef struct {
    char   diskName[32];
    size_t diskSize;
    size_t blockSize;
    int    blocksCount;
    int    inodeAreaSize;
    int    inodeAreaOffset;
    int    bitmapSize;
    int    bitmapOffset;
    int    dataAreaOffset;
} SuperBlock;

typedef struct {
    char fileName[MAX_FILENAME_LENGTH];  
    size_t fileSize;
    bool   isUsed;
    int    blockIndex[INODE_BLOCK_NUM];
    int    blocksAllocated;
} Inode;

static SuperBlock g_superBlock;
static FILE* g_diskFile = NULL;

static bool isBlockUsed(const unsigned char* bitmap, int blockIndex) {
    int byteIndex = blockIndex / 8;
    int bitOffset = blockIndex % 8;
    return (bitmap[byteIndex] & (1 << bitOffset)) != 0;
}


static void setBlockUsed(unsigned char* bitmap, int blockIndex, bool used) {
    int byteIndex = blockIndex / 8;
    int bitOffset = blockIndex % 8;
    if (used) {
        bitmap[byteIndex] |= (1 << bitOffset);
    } else {
        
        bitmap[byteIndex] &= ~(1 << bitOffset);
    }
}



static int findFreeBlocks(unsigned char* bitmap, int blocksCount, int countNeeded, int* foundIndexes) {
    int foundCount = 0;
    int i;
    for (i = 0; i < blocksCount; i++) {
        if (!isBlockUsed(bitmap, i)) {
            foundIndexes[foundCount++] = i;
            if (foundCount == countNeeded) {
                return 0; 
            }
        }
    }
    return -1; 
}


void deleteInode(Inode* inode) {
    inode->isUsed = false;
    inode->fileSize = 0;
    inode->blocksAllocated = 0;
    memset(inode->fileName, 0, MAX_FILENAME_LENGTH); 
    memset(inode->blockIndex, 0, sizeof(inode->blockIndex)); 
}


void writeSuperBlock() {
    fseek(g_diskFile, 0, SEEK_SET);
    fwrite(&g_superBlock, sizeof(SuperBlock), 1, g_diskFile);
    fflush(g_diskFile);
}


void readSuperBlock() {
    fseek(g_diskFile, 0, SEEK_SET);
    fread(&g_superBlock, sizeof(SuperBlock), 1, g_diskFile);
}


void writeInodeArea(Inode* inodes, int count) {
    fseek(g_diskFile, g_superBlock.inodeAreaOffset, SEEK_SET);
    fwrite(inodes, sizeof(Inode), count, g_diskFile);
    fflush(g_diskFile);
}

void writeInodeToDisk(FILE *fp, const Inode *inode, size_t maxFileNameLength) {
    fwrite(inode, sizeof(Inode) - sizeof(char*), 1, fp); 
    fwrite(inode->fileName, sizeof(char), maxFileNameLength, fp); 
}



void readInodeArea(Inode* inodes, int count) {
    fseek(g_diskFile, g_superBlock.inodeAreaOffset, SEEK_SET);
    fread(inodes, sizeof(Inode), count, g_diskFile);
}


void writeBitmap(unsigned char* bitmap, int size) {
    fseek(g_diskFile, g_superBlock.bitmapOffset, SEEK_SET);
    fwrite(bitmap, 1, size, g_diskFile);
    fflush(g_diskFile);
}


void readBitmap(unsigned char* bitmap, int size) {
    fseek(g_diskFile, g_superBlock.bitmapOffset, SEEK_SET);
    fread(bitmap, 1, size, g_diskFile);
}



int createVirtualDisk(const char* diskName, size_t diskSize, size_t blockSize) {
    FILE *fp = fopen(diskName, "wb");
    if (!fp) {
        printf("Cannot create virtual disk file!\n");
        return 1;
    }

    SuperBlock sb;
    strcpy(sb.diskName, diskName);
    sb.diskSize         = diskSize;
    sb.blockSize        = blockSize;
    sb.blocksCount      = diskSize / blockSize;
    sb.inodeAreaSize    = MAX_FILES * sizeof(Inode);
    sb.bitmapSize       = (sb.blocksCount + 7) / 8;
    sb.inodeAreaOffset  = sizeof(SuperBlock);
    sb.bitmapOffset     = sb.inodeAreaOffset + sb.inodeAreaSize;
    sb.dataAreaOffset   = sb.bitmapOffset + sb.bitmapSize;

    fwrite(&sb, sizeof(SuperBlock), 1, fp);

    Inode* inodes = (Inode*)calloc(MAX_FILES, sizeof(Inode));
    for (int i = 0; i < MAX_FILES; i++) {
        inodes[i].isUsed = false;
    }

    fseek(fp, sb.inodeAreaOffset, SEEK_SET);
    fwrite(inodes, sizeof(Inode), MAX_FILES, fp);
    free(inodes);

    unsigned char* bitmap = (unsigned char*)calloc(1, sb.bitmapSize);
    memset(bitmap, 0, sb.bitmapSize);

    fseek(fp, sb.bitmapOffset, SEEK_SET);
    fwrite(bitmap, 1, sb.bitmapSize, fp);
    free(bitmap);
    fseek(fp, diskSize - 1, SEEK_SET);
    fputc('\0', fp);
    fclose(fp);

    printf("Virtual disk created: %s (%lu bytes)\n", diskName, (unsigned long)diskSize);
    return 0;
}


void printBitmap(const char* bitmap, int size) {
    int zeroCount = 0;
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < 8; j++) {
            if ((bitmap[i] >> j) & 1) {
                if (zeroCount > 40) {
                    printf("...%d blocks left... ", zeroCount);
                } else {
                    for (int k = 0; k < zeroCount; k++) {
                        printf("0");
                    }
                }
                zeroCount = 0;
                printf("1");
            } else {
                zeroCount++;
            }
        }
    }
    if (zeroCount > 0) {
        if (zeroCount > 40) {
            printf("...%d blocks left... ", zeroCount);
        } else {
            for (int k = 0; k < zeroCount; k++) {
                printf("0");
            }
        }
    }
    printf("\n");
}


int addNewFile(const char* diskName, const char* filename, size_t fileSize) {
    if (strlen(filename) >= MAX_FILENAME_LENGTH) {
        printf("Filename too long\n");
        return -1;
    }

    FILE *fp = fopen(diskName, "rb+");
    if (fp == NULL) {
        perror("Failed to open virtual disk");
        return -1;
    }

    g_diskFile = fp;
    readSuperBlock();
    SuperBlock sb = g_superBlock;

    unsigned char* bitmap = (unsigned char*)calloc(1, sb.bitmapSize);
    if (bitmap == NULL) {
        perror("Failed to allocate memory for bitmap");
        fclose(fp);
        return -1;
    }
    readBitmap(bitmap, sb.bitmapSize);

    Inode inodes[MAX_FILES];
    readInodeArea(inodes, MAX_FILES);

    int inodeIndex = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodes[i].isUsed && strcmp(inodes[i].fileName, filename) == 0) {
            char response;
            printf("File %s already exists. Overwrite? (y/n): ", filename);
            scanf(" %c", &response);
            if (response == 'y' || response == 'Y') {
                inodeIndex = i;
                break;
            } else {
                free(bitmap);
                fclose(fp);
                printf("Operation cancelled\n");
                return 0;
            }
        }
        if (!inodes[i].isUsed && inodeIndex == -1) {
            inodeIndex = i;
        }
    }

    if (inodeIndex == -1) {
        printf("No free inode available\n");
        free(bitmap);
        fclose(fp);
        return -1;
    }

    size_t requiredBlocks = (fileSize + sb.blockSize - 1) / sb.blockSize;
    if (requiredBlocks > INODE_BLOCK_NUM) {
        printf("File size too large, exceeds maximum block limit per inode\n");
        free(bitmap);
        fclose(fp);
        return -1;
    }

    size_t allocatedBlocks = 0;
    for (size_t i = 0; i < sb.blocksCount && allocatedBlocks < requiredBlocks; i++) {
        if (!isBlockUsed(bitmap, i)) {
            setBlockUsed(bitmap, i, true);
            inodes[inodeIndex].blockIndex[allocatedBlocks] = i;
            allocatedBlocks++;
        }
    }

    if (allocatedBlocks < requiredBlocks) {
        printf("Not enough free space available to store the file\n");
        for (size_t i = 0; i < allocatedBlocks; i++) {
            setBlockUsed(bitmap, inodes[inodeIndex].blockIndex[i], false);
        }
        free(bitmap);
        fclose(fp);
        return -1;
    }

    strncpy(inodes[inodeIndex].fileName, filename, MAX_FILENAME_LENGTH);
    inodes[inodeIndex].fileSize = fileSize;
    inodes[inodeIndex].isUsed = true;
    inodes[inodeIndex].blocksAllocated = allocatedBlocks;

    writeInodeArea(inodes, MAX_FILES);
    writeBitmap(bitmap, sb.bitmapSize);

    free(bitmap);
    fclose(fp);

    printf("File %s of size %ld bytes added to virtual disk %s\n", filename, fileSize, diskName);
    return 0;
}




int removeFile(const char* diskName, const char* filename) {
    FILE *fp = fopen(diskName, "rb+");
    if (fp == NULL) {
        perror("Failed to open virtual disk");
        return -1;
    }

    g_diskFile = fp;
    readSuperBlock();
    SuperBlock sb = g_superBlock;

    unsigned char* bitmap = (unsigned char*)calloc(1, sb.bitmapSize);
    if (bitmap == NULL) {
        perror("Failed to allocate memory for bitmap");
        fclose(fp);
        return -1;
    }
    readBitmap(bitmap, sb.bitmapSize);

    int fileFound = 0;
    Inode inodes[MAX_FILES];
    readInodeArea(inodes, MAX_FILES);

    int inodeIndex = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodes[i].isUsed && strcmp(inodes[i].fileName, filename) == 0) {
            fileFound = 1;
            inodeIndex = i;
            break;
        }
    }

    if (!fileFound) {
        printf("File %s not found on the virtual disk\n", filename);
        free(bitmap);
        fclose(fp);
        return -1;
    }

    for (int i = 0; i < inodes[inodeIndex].blocksAllocated; i++) {
        setBlockUsed(bitmap, inodes[inodeIndex].blockIndex[i], false);
    }

    deleteInode(&inodes[inodeIndex]);

    writeInodeArea(inodes, MAX_FILES);
    writeBitmap(bitmap, sb.bitmapSize);

    free(bitmap);
    fclose(fp);

    printf("File %s has been deleted successfully\n", filename);
    return 0;
}


int copyFileToVirtualDisk(const char* diskName, const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Failed to open file");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char newFilename[MAX_FILENAME_LENGTH];
    printf("Enter the name to store the file as: ");
    scanf("%s", newFilename);

    if (strlen(newFilename) >= MAX_FILENAME_LENGTH) {
        printf("New filename is too long\n");
        fclose(file);
        return -1;
    }

    FILE *fp = fopen(diskName, "rb+");
    if (fp == NULL) {
        perror("Failed to open virtual disk");
        fclose(file);
        return -1;
    }

    g_diskFile = fp;
    readSuperBlock();
    SuperBlock sb = g_superBlock;

    Inode inodes[MAX_FILES];
    readInodeArea(inodes, MAX_FILES);

    int inodeIndex = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodes[i].isUsed && strcmp(inodes[i].fileName, newFilename) == 0) {
            char response;
            printf("File %s already exists. Overwrite? (y/n): ", newFilename);
            scanf(" %c", &response);
            if (response == 'y' || response == 'Y') {
                inodeIndex = i;
                break;
            } else {
                fclose(file);
                fclose(fp);
                printf("Operation cancelled\n");
                return 0;
            }
        }
    }

    if (inodeIndex == -1) {
        int result = addNewFile(diskName, newFilename, fileSize);
        if (result != 0) {
            fclose(file);
            fclose(fp);
            return result;
        }
        readInodeArea(inodes, MAX_FILES);
        for (int i = 0; i < MAX_FILES; i++) {
            if (inodes[i].isUsed && strcmp(inodes[i].fileName, newFilename) == 0) {
                inodeIndex = i;
                break;
            }
        }
    }

    for (int i = 0; i < inodes[inodeIndex].blocksAllocated; i++) {
        fseek(fp, sb.dataAreaOffset + inodes[inodeIndex].blockIndex[i] * sb.blockSize, SEEK_SET);
        char* buffer = (char*)malloc(sb.blockSize);
        if (buffer == NULL) {
            perror("Failed to allocate buffer");
            fclose(file);
            fclose(fp);
            return -1;
        }

        size_t bytesRead = fread(buffer, 1, sizeof(buffer), file);
        fwrite(buffer, 1, bytesRead, fp);
    }

    fclose(file);
    fclose(fp);
    return 0;
}

int copyFileFromVirtualDisk(const char* diskName, const char* filename) {
    FILE *fp = fopen(diskName, "rb");
    if (fp == NULL) {
        perror("Failed to open virtual disk");
        return -1;
    }

    g_diskFile = fp;
    readSuperBlock();
    SuperBlock sb = g_superBlock;

    Inode inodes[MAX_FILES];
    readInodeArea(inodes, MAX_FILES);

    int inodeIndex = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodes[i].isUsed && strcmp(inodes[i].fileName, filename) == 0) {
            inodeIndex = i;
            break;
        }
    }

    if (inodeIndex == -1) {
        printf("File %s not found on the virtual disk\n", filename);
        fclose(fp);
        return -1;
    }

    char newFilename[MAX_FILENAME_LENGTH];
    printf("Enter the name to save the file as: ");
    scanf("%s", newFilename);

    if (strlen(newFilename) >= MAX_FILENAME_LENGTH) {
        printf("New filename is too long\n");
        fclose(fp);
        return -1;
    }

    if (access(newFilename, F_OK) == 0) {
        char response;
        printf("File %s already exists. Overwrite? (y/n): ", newFilename);
        scanf(" %c", &response);
        if (response != 'y' && response != 'Y') {
            fclose(fp);
            printf("Operation cancelled\n");
            return 0;
        }
    }

    FILE *file = fopen(newFilename, "wb");
    if (file == NULL) {
        perror("Failed to open file");
        fclose(fp);
        return -1;
    }

    char* buffer = (char*)malloc(sb.blockSize);
    if (buffer == NULL) {
        perror("Failed to allocate buffer");
        fclose(file);
        fclose(fp);
        return -1;
    }

    size_t remainingBytes = inodes[inodeIndex].fileSize;
    for (int i = 0; i < inodes[inodeIndex].blocksAllocated; i++) {
        size_t bytesToRead = (remainingBytes < sb.blockSize) ? remainingBytes : sb.blockSize;
        fseek(fp, sb.dataAreaOffset + inodes[inodeIndex].blockIndex[i] * sb.blockSize, SEEK_SET);
        size_t bytesRead = fread(buffer, 1, bytesToRead, fp);
        if (bytesRead != bytesToRead) {
            printf("Error reading data from virtual disk\n");
            free(buffer);
            fclose(file);
            fclose(fp);
            return -1;
        }
        if (fwrite(buffer, 1, bytesRead, file) != bytesRead) {
            printf("Error writing data to output file\n");
            free(buffer);
            fclose(file);
            fclose(fp);
            return -1;
        }
        remainingBytes -= bytesToRead;
    }

    free(buffer);
    fclose(file);
    fclose(fp);

    printf("File %s copied from virtual disk %s and saved as %s\n", filename, diskName, newFilename);

    return 0;
}




int listFiles(const char *diskName) {
    FILE *fp = fopen(diskName, "rb");
    if (!fp) {
        perror("Failed to open virtual disk");
        return -1;
    }

    g_diskFile = fp;
    readSuperBlock();
    SuperBlock sb = g_superBlock;

    Inode inodes[MAX_FILES];
    readInodeArea(inodes, MAX_FILES);

    printf("Files on virtual disk %s:\n", diskName);
    bool filesFound = false;
    for (int i = 0; i < MAX_FILES; i++) {
        if (inodes[i].isUsed) {
            printf("File: %s, Size: %lu bytes\n", inodes[i].fileName, inodes[i].fileSize);
            filesFound = true;
        }
    }
    if (!filesFound) {
        printf("No files on disk.\n");
    }
    fclose(fp);
    return 0;
}

int showDiskUsage(const char* diskName) {
    FILE *fp = fopen(diskName, "rb");
    if (fp == NULL) {
        perror("Failed to open virtual disk");
        return -1;
    }

    g_diskFile = fp;
    readSuperBlock();
    SuperBlock sb = g_superBlock;

    printf("Disk Memory Usage:\n");

    unsigned char* bitmap = (unsigned char*)calloc(1, sb.bitmapSize);
    if (bitmap == NULL) {
        perror("Failed to allocate memory for bitmap");
        fclose(fp);
        return -1;
    }
    readBitmap(bitmap, sb.bitmapSize);
    printBitmap((const char*)bitmap, sb.bitmapSize);

    free(bitmap);
    fclose(fp);

    return 0;
}


int removeVirtualDisk(const char* diskName) {
    if (remove(diskName) == 0) {
        printf("Virtual disk %s deleted successfully\n", diskName);
        return 0;
    } else {
        perror("Failed to delete virtual disk");
        return -1;
    }
}

int defragmentDisk(const char* diskName) {
    g_diskFile = fopen(diskName, "rb+");
    if (!g_diskFile) {
        printf("Failed to open virtual disk: %s\n", diskName);
        return 1;
    }

    readSuperBlock();

    Inode inodes[MAX_FILES];
    readInodeArea(inodes, MAX_FILES);

    unsigned char* bitmap = (unsigned char*)calloc(1, g_superBlock.bitmapSize);
    readBitmap(bitmap, g_superBlock.bitmapSize);

    int nextFreeBlock = 0; 
    size_t block_size = g_superBlock.blockSize;
    unsigned char* tempBlock = (unsigned char*)malloc(block_size);
    int i;
    int currentBlockIndex;
    long oldBlockOffset;
    long newBlockOffset;
    int b;
    unsigned char* swapBlock;
    for (i = 0; i < MAX_FILES; i++) {
        if (!inodes[i].isUsed) continue;
        for (b = 0; b < inodes[i].blocksAllocated; b++) {
            currentBlockIndex = inodes[i].blockIndex[b];
            if (currentBlockIndex != nextFreeBlock) {
                if (isBlockUsed(bitmap, nextFreeBlock)) {
                    for (int k = 0; k < MAX_FILES; k++) {
                        if (!inodes[k].isUsed) continue;
                        for (int kk = 0; kk < inodes[k].blocksAllocated; kk++) {
                            if (inodes[k].blockIndex[kk] == nextFreeBlock) {
                                oldBlockOffset = g_superBlock.dataAreaOffset + 
                                                      (long)currentBlockIndex * block_size;
                                fseek(g_diskFile, oldBlockOffset, SEEK_SET);
                                fread(tempBlock, 1, block_size, g_diskFile);
                                swapBlock = (unsigned char*)malloc(block_size);
                                newBlockOffset = g_superBlock.dataAreaOffset + 
                                                      (long)nextFreeBlock * block_size;
                                fseek(g_diskFile, newBlockOffset, SEEK_SET);
                                fread(swapBlock, 1, block_size, g_diskFile);

                                fseek(g_diskFile, oldBlockOffset, SEEK_SET);
                                fwrite(swapBlock, 1, block_size, g_diskFile);

                                fseek(g_diskFile, newBlockOffset, SEEK_SET);
                                fwrite(tempBlock, 1, block_size, g_diskFile);
                                fflush(g_diskFile);

                                free(swapBlock);

                                inodes[k].blockIndex[kk] = currentBlockIndex;
                                inodes[i].blockIndex[b] = nextFreeBlock;

                                writeInodeArea(inodes, MAX_FILES);

                                break;
                            }
                        }
                    }
                } else {
                    oldBlockOffset = g_superBlock.dataAreaOffset + 
                                        (long)currentBlockIndex * block_size;
                    fseek(g_diskFile, oldBlockOffset, SEEK_SET);
                    fread(tempBlock, 1, block_size, g_diskFile);

                    newBlockOffset = g_superBlock.dataAreaOffset + 
                                          (long)nextFreeBlock * block_size;
                    fseek(g_diskFile, newBlockOffset, SEEK_SET);
                    fwrite(tempBlock, 1, block_size, g_diskFile);
                    fflush(g_diskFile);

                    inodes[i].blockIndex[b] = nextFreeBlock;
                    
                    setBlockUsed(bitmap, currentBlockIndex, false);
                    setBlockUsed(bitmap, nextFreeBlock, true);

                    writeBitmap(bitmap, g_superBlock.bitmapSize);
                    writeInodeArea(inodes, MAX_FILES);
                }
            }
            nextFreeBlock++;
        }
    }

    free(tempBlock);
    free(bitmap);

    printf("Defragmentation completed.\n");
    return 0;
}



int main(int argc, char *argv[]) {
    char *func = argv[2];
    unsigned int error = 0;
    char *diskName = argv[1];
    if (strcmp(argv[1], "create") == 0) {
        if (argc != 5) {
            fprintf(stderr, "create <disk name> <disk size> <block size>\n");
            return 1;
        }
        error = createVirtualDisk(argv[2], atoi(argv[3]), atoi(argv[4]));
    } else if (strcmp(func, "cpin") == 0) {
        if (argc != 4) {
            fprintf(stderr, "<disk name> cpin <filename>\n");
            return 1;
        }
        error = copyFileToVirtualDisk(diskName, argv[3]);
    } else if (strcmp(func, "add") == 0) {
        if (argc != 5) {
            fprintf(stderr, "<disk name> add <filename> <file size>\n");
            return 1;
        }
        error = addNewFile(diskName, argv[3], atoi(argv[4]));
    } else if (strcmp(func, "cpout") == 0) {
        if (argc != 4) {
            fprintf(stderr, "<disk name> cpout <filename>\n");
            return 1;
        }
        error = copyFileFromVirtualDisk(diskName, argv[3]);
    } else if (strcmp(func, "rm") == 0) {
        if (argc != 4) {
            fprintf(stderr, "<disk name> rm <filename>\n");
            return 1;
        }
        error = removeFile(diskName, argv[3]);
    } else if (strcmp(func, "die") == 0) {
        if (argc != 3) {
            fprintf(stderr, "<disk name> die\n");
            return 1;
        }
        error = removeVirtualDisk(diskName);
    } else if (strcmp(func, "ls") == 0) {
        if (argc != 3) {
            fprintf(stderr, "<disk name> ls\n");
            return 1;
        }
        error = listFiles(diskName);
    } else if (strcmp(func, "defrag") == 0) {
        if (argc != 3) {
            fprintf(stderr, "<disk name> defrag\n");
            return 1;
        }
        error = defragmentDisk(diskName);
    } else if (strcmp(func, "mem") == 0) {
        if (argc != 3) {
            fprintf(stderr, "<disk name> mem\n");
            return 1;
        }
        error = showDiskUsage(diskName);
    } else {
        fprintf(stderr, "Incorrect arguments format\n");
        return 1;
    }
    if (error) {
        printf("Error: Function did not work as expected\n");
    }
    return 0;
}
