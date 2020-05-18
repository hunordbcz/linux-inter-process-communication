#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <zconf.h>
#include <string.h>

#define SHM_NAME "/bc3pcA7"
#define BODY_HEADER_SIZE 9
#define SECTION_HEADER_SIZE 26

typedef enum {
    REQ_EXIT,
    REQ_PING,
    REQ_CREATE_SHM,
    REQ_WRITE_TO_SHM,
    REQ_MAP_FILE,
    REQ_READ_FROM_FILE_OFFSET,
    REQ_READ_FROM_FILE_SECTION,
    REQ_READ_FROM_LOGICAL_SPACE_OFFSET,

    REQ_NOT_FOUND
} REQUEST_ENUM;

typedef union {
    u_int32_t value;
    unsigned char byte[4];
} UNSIGNED_INT;

typedef union {
    u_int8_t value;
    unsigned char byte[1];
} BYTE;

typedef struct {
    unsigned int offset;
    unsigned int size;
} SECTION, *pSECTION;

typedef struct {
    char *pointer;
    u_long size;
} MAPPED_FILE, *pMAPPED_FILE;

int fdRead, fdWrite;
pMAPPED_FILE pMappedFile = NULL;

void makePipeBasedConnection(char *response, char *request);

void writeString(char *str);

char *readString();

int processRequest(char *request);

int getType(char *request);

void writeNumber(unsigned int number);

unsigned int readNumber();

int createSharedMemory(unsigned int bytes);

int writeToSharedMemory(unsigned int value, unsigned int offset);

int mapFile(char *path);

int readFromFileOffset(unsigned int noOfBytes, unsigned int offset);

int readFromSection(unsigned int nrOfBytes, unsigned int offset, unsigned int sectionNr);

int readFromLogicalSpaceOffset(unsigned int nrOfBytes, unsigned int logicalOffset);

SECTION getSection(unsigned int nr);

int writeStringToSHM(unsigned char *value, unsigned int offset);

int getSharedMemory(char *key);

int getFileDescriptor(char *path);

int main() {
    makePipeBasedConnection("RESP_PIPE_82417", "REQ_PIPE_82417");
    while (processRequest(readString()) != REQ_EXIT);
    close(fdRead);
    close(fdWrite);
    return 0;
}

int processRequest(char *request) {
    int type = getType(request);
    switch (type) {
        case REQ_PING:
            writeString("PING");
            writeString("PONG");
            writeNumber(82417);
            break;
        case REQ_CREATE_SHM:
            writeString("CREATE_SHM");
            if (createSharedMemory(readNumber()) == EXIT_FAILURE) {
                writeString("ERROR");
            } else {
                writeString("SUCCESS");
            }
            break;
        case REQ_WRITE_TO_SHM:
            writeString("WRITE_TO_SHM");
            if (writeToSharedMemory(readNumber(), readNumber()) == EXIT_FAILURE) {
                writeString("ERROR");
            } else {
                writeString("SUCCESS");
            }
            break;
        case REQ_MAP_FILE:
            writeString("MAP_FILE");
            if (mapFile(readString()) == EXIT_FAILURE) {
                writeString("ERROR");
            } else {
                writeString("SUCCESS");
            }
            break;
        case REQ_READ_FROM_FILE_OFFSET:
            writeString("READ_FROM_FILE_OFFSET");
            if (readFromFileOffset(readNumber(), readNumber()) == EXIT_FAILURE) {
                writeString("ERROR");
            } else {
                writeString("SUCCESS");
            }
            break;
        case REQ_READ_FROM_FILE_SECTION:
            writeString("READ_FROM_FILE_SECTION");
            if (readFromSection(readNumber(), readNumber(), readNumber()) == EXIT_FAILURE) {
                writeString("ERROR");
            } else {
                writeString("SUCCESS");
            }
            break;
        case REQ_READ_FROM_LOGICAL_SPACE_OFFSET:
            writeString("READ_FROM_LOGICAL_SPACE_OFFSET");
            if (readFromLogicalSpaceOffset(readNumber(), readNumber()) == EXIT_FAILURE) {
                writeString("ERROR");
            } else {
                writeString("SUCCESS");
            }
            break;
        case REQ_NOT_FOUND:
            perror("Request not found in processRequest()");
            exit(-1);
        default:
            if (pMappedFile != NULL) {
                free(pMappedFile);
            }
            return REQ_EXIT;
    }
    return 1;
}

int readFromLogicalSpaceOffset(unsigned int nrOfBytes, unsigned int logicalOffset) {
    BYTE noOfSections;
    memcpy(noOfSections.byte, pMappedFile->pointer + 8, 1);
    unsigned int currentOffset = 0;

    for (int i = 0; i < noOfSections.value; i++) {
        SECTION section = getSection(i);
        if (section.size + currentOffset < logicalOffset) {
            currentOffset += section.size;
            if (currentOffset % 3072 != 0) {
                currentOffset += 3072 - currentOffset % 3072;
            }
        } else {
            unsigned char response[nrOfBytes + 1];
            memcpy(response, pMappedFile->pointer + section.offset + (logicalOffset - currentOffset), nrOfBytes);

            return writeStringToSHM(response, 0);
        }
    }
    return EXIT_FAILURE;
}

SECTION getSection(unsigned int nr) {
    SECTION result;
    UNSIGNED_INT size;
    UNSIGNED_INT offset;
    int sectionOffsetPos = BODY_HEADER_SIZE + nr * SECTION_HEADER_SIZE + 18;
    int sectionSizePos = BODY_HEADER_SIZE + nr * SECTION_HEADER_SIZE + 22;

    memcpy(size.byte, pMappedFile->pointer + sectionSizePos, 4);
    memcpy(offset.byte, pMappedFile->pointer + sectionOffsetPos, 4);

    result.size = size.value;
    result.offset = offset.value;
    return result;
}

int readFromSection(unsigned int nrOfBytes, unsigned int offset, unsigned int sectionNr) {
    BYTE noOfSections;
    memcpy(noOfSections.byte, pMappedFile->pointer + 8, 1);
    if (noOfSections.value < sectionNr) {
        return EXIT_FAILURE;
    }

    SECTION section = getSection(sectionNr - 1);
    unsigned char response[nrOfBytes + 1];
    memcpy(response, pMappedFile->pointer + section.offset + offset, nrOfBytes);

    return writeStringToSHM(response, 0);
}

int readFromFileOffset(unsigned int noOfBytes, unsigned int offset) {
    printf("SIZE: %lu \t OFFSET: %d\tNoOfBytes: %d\n", pMappedFile->size, noOfBytes, offset);
    if (pMappedFile->size < (offset + noOfBytes)) {
        return EXIT_FAILURE;
    }
    printf("TEST\n\n");
    unsigned char response[noOfBytes + 1];

    memcpy(response, pMappedFile->pointer + offset, noOfBytes);
    response[noOfBytes] = '\0';

    return writeStringToSHM(response, 0);
}

int mapFile(char *path) {
    int fileDescriptor;
    if ((fileDescriptor = getFileDescriptor(path)) == -1) {
        return EXIT_FAILURE;
    }

    int size = lseek(fileDescriptor, 0, SEEK_END);
    lseek(fileDescriptor, 0, SEEK_SET);

    char *data = NULL;
    data = (char *) mmap(NULL, size, PROT_READ, MAP_SHARED, fileDescriptor, 0);
    if (data == MAP_FAILED) {
        return EXIT_FAILURE;
    }

    pMappedFile = (pMAPPED_FILE) malloc(sizeof(MAPPED_FILE));
    pMappedFile->size = size;
    pMappedFile->pointer = data;
    return EXIT_SUCCESS;
}

int getFileDescriptor(char *path) {
    int fileD = -1;

    if (access(path, R_OK) == -1) {
        chmod(path, S_IRUSR | S_IRGRP | S_IROTH);
    }
    if ((fileD = open(path, O_RDONLY, 0644)) < 0) {
        return -1;
    }

    return fileD;
}

int writeStringToSHM(unsigned char *value, unsigned int offset) {
    printf("%s\n", value);
    int shm_id = getSharedMemory(SHM_NAME);

    int sizeSHM = lseek(shm_id, 0, SEEK_END);
    int sizeValue = strlen((char *) value);
    if (sizeSHM >= offset + sizeValue) {

        lseek(shm_id, offset, SEEK_SET);
        write(shm_id, value, sizeValue);
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

int writeToSharedMemory(unsigned int value, unsigned int offset) {
    UNSIGNED_INT nr;
    nr.value = value;
    return writeStringToSHM(nr.byte, offset);
}

int getSharedMemory(char *key) {
    int shm_id = shm_open(key, O_RDWR, 0644);
    if (shm_id < 0) {
        perror("Cannot open the shared memory at writeToSharedMemory()");
        exit(-1);
    }
    return shm_id;
}

int createSharedMemory(unsigned int bytes) {
    shm_unlink(SHM_NAME);
    int shm_id = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0664);
    if (shm_id < 0) {
        return EXIT_FAILURE;
    }
    ftruncate(shm_id, bytes);
    return EXIT_SUCCESS;
}

void writeNumber(unsigned int number) {
    UNSIGNED_INT nr;
    nr.value = number;
    write(fdWrite, nr.byte, sizeof(nr.byte));
}

int getType(char *request) {
    const char *REQUEST_NAME[] = {
            [REQ_EXIT] = "EXIT",
            [REQ_PING] = "PING",
            [REQ_CREATE_SHM] ="CREATE_SHM",
            [REQ_WRITE_TO_SHM] ="WRITE_TO_SHM",
            [REQ_MAP_FILE] ="MAP_FILE",
            [REQ_READ_FROM_FILE_OFFSET] ="READ_FROM_FILE_OFFSET",
            [REQ_READ_FROM_FILE_SECTION] ="READ_FROM_FILE_SECTION",
            [REQ_READ_FROM_LOGICAL_SPACE_OFFSET] ="READ_FROM_LOGICAL_SPACE_OFFSET"
    };

    for (int i = 0; i < sizeof(REQUEST_NAME) / sizeof(REQUEST_NAME[0]); i++) {
        if (strcmp(request, REQUEST_NAME[i]) == 0) {
            free(request);
            return i;
        }
    }

    free(request);
    return REQ_NOT_FOUND;
}

void writeString(char *str) {
    unsigned int sizeMessage = strlen(str);
    char size[1];
    size[0] = (char) sizeMessage;
    write(fdWrite, size, 1);
    write(fdWrite, str, sizeMessage);
}

char *readString() {
    char size[2];
    read(fdRead, size, 1);
    unsigned int messageSize = size[0];

    char *buff = (char *) malloc(sizeof(char));
    read(fdRead, buff, messageSize);
    buff[messageSize] = '\0';
    return buff;
}

unsigned int readNumber() {
    UNSIGNED_INT nr;
    read(fdRead, nr.byte, 4);

    return nr.value;
}

void makePipeBasedConnection(char *response, char *request) {
    if (mknod(response, S_IFIFO | 0640, 0) < 0) {
        perror("Error creating FIFO");
        exit(1);
    }
    fdRead = open(request, O_RDONLY);
    fdWrite = open(response, O_WRONLY);

    writeString("CONNECT");
    printf("SUCCESS\n");
}
