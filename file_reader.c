#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define MAXBUFSIZE  1024

typedef struct {
    char name[100];
    char time[100];
} TimeInfo;

int main()
{
    const int maxBuf = 1048576;
    char *query = (char*)malloc(MAXBUFSIZE), *buf = (char*)malloc(maxBuf);
    scanf("%s", query);
    FILE *file = fopen(query, "r");
    int len = fread(buf, 1, maxBuf, file);
    buf[len] = '\0';
    fputs(buf, stdout);
    free(buf);
    
    int fd;
    time_t current_time;
    char c_time_string[100];
    TimeInfo *p_map;
    
    fd = open("log", O_RDWR | O_TRUNC | O_CREAT, 0777); 
    if(fd<0)
    {
        perror("open");
        exit(-1);
    }
    lseek(fd,sizeof(TimeInfo),SEEK_SET);
    write(fd,"",1);

    p_map = (TimeInfo*) mmap(0, sizeof(TimeInfo), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);


    current_time = time(NULL);
    strcpy(c_time_string, ctime(&current_time));

    memcpy(p_map->time, &c_time_string , sizeof(c_time_string));
    memcpy(p_map->name, query, (int)strlen(query) + 1);
    
    munmap(p_map, sizeof(TimeInfo));

    fd = open("log", O_RDWR);
    p_map = (TimeInfo*)mmap(0, sizeof(TimeInfo),  PROT_READ,  MAP_SHARED, fd, 0);

	free(query);
    return 0;
}
