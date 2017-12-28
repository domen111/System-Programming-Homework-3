#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXBUFSIZE  1024

int main()
{
    const int maxBuf = 1048576;
    char *query = (char*)malloc(MAXBUFSIZE), *buf = (char*)malloc(maxBuf);
    scanf("%s", query);
	sleep(5);
    FILE *file = fopen(query, "r");
    int len = fread(buf, 1, maxBuf, file);
    buf[len] = '\0';
    fputs(buf, stdout);
    free(buf);
	free(query);
    return 0;
}
