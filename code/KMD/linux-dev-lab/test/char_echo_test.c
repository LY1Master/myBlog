#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define DEVICE_PATH "/dev/char_echo"
#define USER_BUFFER_SZIE 512

int main(){



    char* write_buffer = "write buffer msg\n";
    char read_buffer[USER_BUFFER_SZIE];
    int fd = open(DEVICE_PATH,O_WRONLY);
    if(fd < 0){
        perror("open for write failed.\n");
        exit(EXIT_FAILURE);
    }
    ssize_t writen = write(fd,write_buffer,strlen(write_buffer));
    if(writen<0){
        perror("writen failed.\n");
        close(fd);
        exit(EXIT_FAILURE);
    }

    int fd2 = open(DEVICE_PATH,O_RDONLY);
    if(fd2 < 0){
        perror("open for read failed.\n");
        exit(EXIT_FAILURE);
    }
    ssize_t readen = read(fd2,read_buffer,strlen(write_buffer));
    if(readen<0){
        perror("readen failed.\n");
        close(fd2);
        exit(EXIT_FAILURE);
    }
    printf("read buffer is %s\n",read_buffer);

    close(fd);
    close(fd2);
    return 0;
}
