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
#define LOGICAL_OFFSET_ALIGNMENT 3072

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

int pipeReadFD, pipeWriteFD;
pMAPPED_FILE pMappedFile = NULL;

void makePipeBasedConnection(char *response, char *request);

void writeStringPipe(char *str);

char *readStringPipe();

int processRequest(char *request);

int getType(char *request);

void writeNumberPipe(unsigned int number);

unsigned int readNumberPipe();

int createSharedMemory(unsigned int bytes);

int writeToSharedMemory(unsigned int value, unsigned int offset);

int mapFile(char *path);

unsigned char *readFromFileOffset(unsigned int noOfBytes, unsigned int offset);

unsigned char *readFromSection(unsigned int nrOfBytes, unsigned int offset, unsigned int sectionNr);

unsigned char *readFromLogicalSpaceOffset(unsigned int nrOfBytes, unsigned int logicalOffset);

SECTION getSection(unsigned int nr);

int writeStringToSHM(unsigned char *value, unsigned int offset);

int getSharedMemory(char *key);

int getFileDescriptor(char *path);

int main() {
    makePipeBasedConnection("RESP_PIPE_82417", "REQ_PIPE_82417");
    while (processRequest(readStringPipe()) != REQ_EXIT);
    return 0;
}

int processRequest(char *request) {
    int type = getType(request);
    unsigned char *result = NULL;
    switch (type) {
        case REQ_PING:
            writeStringPipe("PING");
            writeStringPipe("PONG");
            writeNumberPipe(82417);
            break;
        case REQ_CREATE_SHM:
            writeStringPipe("CREATE_SHM");
            if (createSharedMemory(readNumberPipe()) == EXIT_FAILURE) {
                writeStringPipe("ERROR");
            } else {
                writeStringPipe("SUCCESS");
            }
            break;
        case REQ_WRITE_TO_SHM:
            writeStringPipe("WRITE_TO_SHM");
            if (writeToSharedMemory(readNumberPipe(), readNumberPipe()) == EXIT_FAILURE) {
                writeStringPipe("ERROR");
            } else {
                writeStringPipe("SUCCESS");
            }
            break;
        case REQ_MAP_FILE:
            writeStringPipe("MAP_FILE");
            if (mapFile(readStringPipe()) == EXIT_FAILURE) {
                writeStringPipe("ERROR");
            } else {
                writeStringPipe("SUCCESS");
            }
            break;
        case REQ_READ_FROM_FILE_OFFSET:
            writeStringPipe("READ_FROM_FILE_OFFSET");
            result = readFromFileOffset(readNumberPipe(), readNumberPipe());
            if (result == NULL) {
                writeStringPipe("ERROR");
            } else {
                writeStringToSHM(result, 0);
                writeStringPipe("SUCCESS");

                free(result);
            }
            break;
        case REQ_READ_FROM_FILE_SECTION:
            writeStringPipe("READ_FROM_FILE_SECTION");
            result = readFromSection(readNumberPipe(), readNumberPipe(), readNumberPipe());
            if (result == NULL) {
                writeStringPipe("ERROR");
            } else {
                writeStringToSHM(result, 0);
                writeStringPipe("SUCCESS");

                free(result);
            }
            break;
        case REQ_READ_FROM_LOGICAL_SPACE_OFFSET:
            writeStringPipe("READ_FROM_LOGICAL_SPACE_OFFSET");
            result = readFromLogicalSpaceOffset(readNumberPipe(), readNumberPipe());
            if (result == NULL) {
                writeStringPipe("ERROR");
            } else {
                writeStringToSHM(result, 0);
                writeStringPipe("SUCCESS");

                free(result);
            }
            break;
        case REQ_NOT_FOUND:
            if (pMappedFile != NULL) {
                free(pMappedFile);
            }

            perror("Request not found in processRequest()");
            exit(-1);
        default:
            if (pMappedFile != NULL) {
                free(pMappedFile);
            }
            close(pipeReadFD);
            close(pipeWriteFD);
            return REQ_EXIT;
    }
    return 1;
}

unsigned char *readFromLogicalSpaceOffset(unsigned int nrOfBytes, unsigned int logicalOffset) {
    BYTE noOfSections;
    memcpy(noOfSections.byte, pMappedFile->pointer + 8, 1);
    unsigned int currentOffset = 0;

    for (int i = 0; i < noOfSections.value; i++) {
        SECTION section = getSection(i);
        if (section.size + currentOffset < logicalOffset) {
            currentOffset += section.size;
            if (currentOffset % LOGICAL_OFFSET_ALIGNMENT != 0) {
                currentOffset += LOGICAL_OFFSET_ALIGNMENT - currentOffset % LOGICAL_OFFSET_ALIGNMENT;
            }
        } else {
            unsigned char *response = (unsigned char *) malloc(sizeof(char));
            memcpy(response, pMappedFile->pointer + section.offset + (logicalOffset - currentOffset), nrOfBytes);
            response[nrOfBytes] = '\0';

            return response;
        }
    }
    return NULL;
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

unsigned char *readFromSection(unsigned int nrOfBytes, unsigned int offset, unsigned int sectionNr) {
    BYTE noOfSections;
    memcpy(noOfSections.byte, pMappedFile->pointer + 8, 1);
    if (noOfSections.value < sectionNr) {
        return NULL;
    }

    SECTION section = getSection(sectionNr - 1);
    unsigned char *response = (unsigned char *) malloc(sizeof(char));
    memcpy(response, pMappedFile->pointer + section.offset + offset, nrOfBytes);
    response[nrOfBytes] = '\0';

    return response;
}

unsigned char *readFromFileOffset(unsigned int noOfBytes, unsigned int offset) {
    if (pMappedFile->size < (offset + noOfBytes)) {
        return NULL;
    }

    unsigned char *response = (unsigned char *) malloc(sizeof(char));
    memcpy(response, pMappedFile->pointer + offset, noOfBytes);
    response[noOfBytes] = '\0';

    return response;
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

void writeNumberPipe(unsigned int number) {
    UNSIGNED_INT nr;
    nr.value = number;
    write(pipeWriteFD, nr.byte, sizeof(nr.byte));
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

void writeStringPipe(char *str) {
    unsigned int sizeMessage = strlen(str);
    char size[1];
    size[0] = (char) sizeMessage;
    write(pipeWriteFD, size, 1);
    write(pipeWriteFD, str, sizeMessage);
}

char *readStringPipe() {
    char size[2];
    read(pipeReadFD, size, 1);
    unsigned int messageSize = size[0];

    char *buff = (char *) malloc(sizeof(char));
    read(pipeReadFD, buff, messageSize);
    buff[messageSize] = '\0';
    return buff;
}

unsigned int readNumberPipe() {
    UNSIGNED_INT nr;
    read(pipeReadFD, nr.byte, 4);

    return nr.value;
}

void makePipeBasedConnection(char *response, char *request) {
    if (mknod(response, S_IFIFO | 0640, 0) < 0) {
        perror("Error creating FIFO");
        exit(1);
    }
    pipeReadFD = open(request, O_RDONLY);
    pipeWriteFD = open(response, O_WRONLY);

    writeStringPipe("CONNECT");
    printf("SUCCESS\n");
}
