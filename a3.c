#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <zconf.h>
#include <string.h>

#define SHM_NAME "/bc3pcA7"

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

typedef union {
    u_int32_t value;
    unsigned char byte[4];
} NUMBER;

int fdRead, fdWrite;
char *mappedFile = NULL;
u_long mappedFileSize = -1;

void makePipeBasedConnection(char *response, char *request);

void writeString(char *str);

char *readString();

int processRequest(char *request);

int getType(char *request);

void writeNumber(unsigned int number);

unsigned int readNumber();

int createSharedMemory(unsigned int bytes);

int writeToSharedMemory(unsigned int value, unsigned int offset);

char *mapFile(char *path);

int readFromFileOffset(char *file, unsigned int offset, unsigned int noOfBytes);

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
            if ((mappedFile = mapFile(readString())) == NULL) {
                writeString("ERROR");
            } else {
                writeString("SUCCESS");
            }
            break;
        case REQ_READ_FROM_FILE_OFFSET:
            writeString("READ_FROM_FILE_OFFSET");
            if (readFromFileOffset(mappedFile, readNumber(), readNumber()) == EXIT_FAILURE) {
                writeString("ERROR");
            } else {
                writeString("SUCCESS");
            }
            break;
        case REQ_READ_FROM_FILE_SECTION:
        case REQ_READ_FROM_LOGICAL_SPACE_OFFSET:
        case REQ_NOT_FOUND:
        default:
            return REQ_EXIT;
    }
    return 1;
}

int readFromFileOffset(char *file, unsigned int offset, unsigned int noOfBytes) {
    if (mappedFileSize < (offset + noOfBytes)) {
        return EXIT_FAILURE;
    }
    char response[noOfBytes + 1];
    for (int i = 0; i < noOfBytes; i++) {
        file[i] = file[offset + i];
    }
    response[noOfBytes] = '\0';
    printf("%s\n", response);
    return EXIT_SUCCESS;
}

char *mapFile(char *path) {
    int fd;
    if (access(path, R_OK) == -1) {
        chmod(path, S_IRUSR | S_IRGRP | S_IROTH);
    }
    if ((fd = open(path, O_RDONLY, 0644)) < 0) {
        return NULL;
    }
    struct stat st;
    fstat(fd, &st);
    int size = st.st_size;
    lseek(fd, 0, SEEK_SET);

    char *data = NULL;
    data = (char *) mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == (void *) -1) {
        return NULL;
    }
    mappedFileSize = size;
    return data;

}

int writeToSharedMemory(unsigned int value, unsigned int offset) {
    int shm_id = shm_open(SHM_NAME, O_RDWR, 0644);
    if (shm_id < 0) {
        perror("Cannot open the shared memory at writeToSharedMemory()");
        exit(-1);
    }

    struct stat st;
    fstat(shm_id, &st);
    int size = st.st_size;
    if (size >= offset + sizeof(value)) {
        NUMBER nr;
        nr.value = value;
        lseek(shm_id, offset, SEEK_SET);
        write(shm_id, nr.byte, sizeof(nr.byte));
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

int createSharedMemory(unsigned int bytes) {
    shm_unlink("/bc3pcA7");
    int shm_id = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0664);
    if (shm_id < 0) {
        return EXIT_FAILURE;
    }
    ftruncate(shm_id, bytes);
    return EXIT_SUCCESS;
}

void writeNumber(unsigned int number) {
    NUMBER nr;
    nr.value = number;
    write(fdWrite, nr.byte, sizeof(nr.byte));
}

int getType(char *request) {
    for (int i = 0; i < sizeof(REQUEST_NAME) / sizeof(REQUEST_NAME[0]); i++) {
        if (strcmp(request, REQUEST_NAME[i]) == 0) {
            return i;
        }
    }

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
    NUMBER nr;
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
