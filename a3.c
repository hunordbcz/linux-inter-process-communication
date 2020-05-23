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
#define RESPONSE_PIPE_KEY "RESP_PIPE_82417"
#define REQUEST_PIPE_KEY "REQ_PIPE_82417"

typedef enum {
    REQ_PING,
    REQ_CREATE_SHM,
    REQ_WRITE_TO_SHM,
    REQ_MAP_FILE,
    REQ_READ_FROM_FILE_OFFSET,
    REQ_READ_FROM_FILE_SECTION,
    REQ_READ_FROM_LOGICAL_SPACE_OFFSET,
    REQ_EXIT,

    REQ_NOT_FOUND
} REQUEST_ENUM;

typedef struct {
    unsigned int offset;
    unsigned int size;
} SECTION, *pSECTION;

typedef struct {
    char *pointer;
    u_long size;
} MAPPED_FILE, *pMAPPED_FILE;

int pipeReadFD = -1, pipeWriteFD = -1;
pMAPPED_FILE pInput = NULL;
pMAPPED_FILE pMappedSHM = NULL;

void makePipeBasedConnection(char *response, char *request);

void writeStringPipe(char *str);

char *readStringPipe();

int processRequest(char *request);

int getType(char *request);

void writeNumberPipe(unsigned int number);

unsigned int readNumberPipe();

int createSharedMemory(unsigned int bytes);

int writeNumberToSHM(unsigned int value, unsigned int offset);

int mapFile(char *path);

char *readFromFileOffset(unsigned int nrBytes, unsigned int offset);

char *readFromSection(unsigned int nrBytes, unsigned int offset, unsigned int sectionNr);

char *readFromLogicalSpaceOffset(unsigned int nrBytes, unsigned int logicalOffset);

SECTION getSection(unsigned int nr);

int writeStringToSHM(char *value, unsigned int offset);

int getFileDescriptor(char *path);

void cleanUp();

int main() {
    makePipeBasedConnection(RESPONSE_PIPE_KEY, REQUEST_PIPE_KEY);
    while (processRequest(readStringPipe()) != REQ_EXIT);
    return 0;
}

int processRequest(char *request) {
    int requestType = getType(request);
    char *result = NULL;
    switch (requestType) {
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
            if (writeNumberToSHM(readNumberPipe(), readNumberPipe()) == EXIT_FAILURE) {
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
            cleanUp();
            free(request);

            perror("Request not found in processRequest()");
            exit(EXIT_FAILURE);
        default:
            cleanUp();
            return REQ_EXIT;
    }

    free(request);
    return requestType;
}

void cleanUp() {
    if (pInput != NULL) {
        munmap(pInput->pointer, pInput->size);
        free(pInput);
    }

    if (pMappedSHM != NULL) {
        munmap(pMappedSHM->pointer, pMappedSHM->size);
        free(pMappedSHM);
    }

    if (pipeReadFD != -1) {
        close(pipeReadFD);
    }

    if (pipeWriteFD != -1) {
        close(pipeWriteFD);
    }
}

char *readFromLogicalSpaceOffset(unsigned int nrBytes, unsigned int logicalOffset) {
    u_int8_t nrSections;
    memcpy(&nrSections, pInput->pointer + 8, sizeof(nrSections));
    unsigned int currentOffset = 0;

    for (int i = 0; i < nrSections; i++) {
        SECTION section = getSection(i);
        if (section.size + currentOffset < logicalOffset) {
            currentOffset += section.size;
            if (currentOffset % LOGICAL_OFFSET_ALIGNMENT != 0) {
                currentOffset += LOGICAL_OFFSET_ALIGNMENT - currentOffset % LOGICAL_OFFSET_ALIGNMENT;
            }
        } else {
            char *response = (char *) malloc(sizeof(char));
            memcpy(response, pInput->pointer + section.offset + (logicalOffset - currentOffset), nrBytes);
            response[nrBytes] = '\0';

            return response;
        }
    }
    return NULL;
}

SECTION getSection(unsigned int nr) {
    SECTION result;
    int sectionOffsetPos = BODY_HEADER_SIZE + nr * SECTION_HEADER_SIZE + 18;
    int sectionSizePos = BODY_HEADER_SIZE + nr * SECTION_HEADER_SIZE + 22;

    memcpy(&result.size, pInput->pointer + sectionSizePos, sizeof(result.size));
    memcpy(&result.offset, pInput->pointer + sectionOffsetPos, sizeof(result.offset));

    return result;
}

char *readFromSection(unsigned int nrBytes, unsigned int offset, unsigned int sectionNr) {
    u_int8_t noOfSections;
    memcpy(&noOfSections, pInput->pointer + 8, sizeof(noOfSections));
    if (noOfSections < sectionNr) {
        return NULL;
    }

    SECTION section = getSection(sectionNr - 1);
    char *response = (char *) malloc(sizeof(char));
    memcpy(response, pInput->pointer + section.offset + offset, nrBytes);
    response[nrBytes] = '\0';

    return response;
}

char *readFromFileOffset(unsigned int nrBytes, unsigned int offset) {
    if (pInput->size < (offset + nrBytes)) {
        return NULL;
    }

    char *response = (char *) malloc(sizeof(char));
    memcpy(response, pInput->pointer + offset, nrBytes);
    response[nrBytes] = '\0';

    return response;
}

int mapFile(char *path) {
    int fileDescriptor;
    if ((fileDescriptor = getFileDescriptor(path)) == -1) {
        return EXIT_FAILURE;
    }

    int size = lseek(fileDescriptor, 0, SEEK_END);

    char *data = NULL;
    data = (char *) mmap(NULL, size, PROT_READ, MAP_SHARED, fileDescriptor, 0);
    if (data == MAP_FAILED) {
        return EXIT_FAILURE;
    }

    if (pInput == NULL) {
        pInput = (pMAPPED_FILE) malloc(sizeof(MAPPED_FILE));
    }
    pInput->size = size;
    pInput->pointer = data;
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

int writeStringToSHM(char *value, unsigned int offset) {
    if (pMappedSHM == NULL) {
        return EXIT_FAILURE;
    }

    int sizeValue = strlen(value);
    if (pMappedSHM->size < offset + sizeValue) {
        return EXIT_FAILURE;
    }

    memcpy(pMappedSHM->pointer + offset, value, sizeValue);
    return EXIT_SUCCESS;
}

int writeNumberToSHM(unsigned int value, unsigned int offset) {
    return writeStringToSHM((char *) &value, offset);
}

int createSharedMemory(unsigned int bytes) {
    shm_unlink(SHM_NAME);
    int shmFD = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0664);
    if (shmFD < 0) {
        return EXIT_FAILURE;
    }
    ftruncate(shmFD, bytes);
    char *data = (char *) mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, shmFD, 0);

    if (pMappedSHM == NULL) {
        pMappedSHM = (pMAPPED_FILE) malloc(sizeof(MAPPED_FILE));
    }
    pMappedSHM->size = bytes;
    pMappedSHM->pointer = data;

    return EXIT_SUCCESS;
}

void writeNumberPipe(unsigned int number) {
    write(pipeWriteFD, &number, sizeof(number));
}

int getType(char *request) {
    const char *REQUEST_NAME[] = {
            [REQ_PING] = "PING",
            [REQ_CREATE_SHM] ="CREATE_SHM",
            [REQ_WRITE_TO_SHM] ="WRITE_TO_SHM",
            [REQ_MAP_FILE] ="MAP_FILE",
            [REQ_READ_FROM_FILE_OFFSET] ="READ_FROM_FILE_OFFSET",
            [REQ_READ_FROM_FILE_SECTION] ="READ_FROM_FILE_SECTION",
            [REQ_READ_FROM_LOGICAL_SPACE_OFFSET] ="READ_FROM_LOGICAL_SPACE_OFFSET",
            [REQ_EXIT] = "EXIT",
    };

    for (int i = 0; i < sizeof(REQUEST_NAME) / sizeof(REQUEST_NAME[0]); i++) {
        if (strcmp(request, REQUEST_NAME[i]) == 0) {
            return i;
        }
    }

    return REQ_NOT_FOUND;
}

void writeStringPipe(char *str) {
    u_int8_t sizeMessage = strlen(str);
    write(pipeWriteFD, &sizeMessage, sizeof(sizeMessage));
    write(pipeWriteFD, str, sizeMessage);
}

char *readStringPipe() {
    u_int8_t messageSize;
    read(pipeReadFD, &messageSize, sizeof(messageSize));

    char *buff = (char *) malloc(sizeof(char));
    read(pipeReadFD, buff, messageSize);
    buff[messageSize] = '\0';
    return buff;
}

unsigned int readNumberPipe() {
    unsigned int value;
    read(pipeReadFD, &value, sizeof(value));

    return value;
}

void makePipeBasedConnection(char *response, char *request) {
    if (mknod(response, S_IFIFO | 0640, 0) < 0) {
        perror("Error creating FIFO");
        exit(EXIT_FAILURE);
    }
    pipeReadFD = open(request, O_RDONLY);
    pipeWriteFD = open(response, O_WRONLY);

    writeStringPipe("CONNECT");
    printf("SUCCESS\n");
}
