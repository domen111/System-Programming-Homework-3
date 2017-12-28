#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXBUFSIZE  1024
char *decode_query( char *query, char *get );

int main()
{
    const int maxBuf = 1048576;
    char *query = (char*)malloc(MAXBUFSIZE), *buf = (char*)malloc(maxBuf);
    scanf("%s", query);
	sleep(5);
    char *fileName = decode_query(query, "file_name");
    FILE *file = fopen(fileName, "r");
    int len = fread(buf, 1, maxBuf, file);
    buf[len] = '\0';
    fputs(buf, stdout);
    free(buf);
	free(query);
    return 0;
}

char *decode_query( char *query, char *get ) {
    char *pch = strtok(query, "&");
    while (pch != NULL) {
        int i, ok = 1;
        for (i = 0; pch[i] != '=' && pch[i]; i++) {
            if (get[i] != pch[i]) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            return pch + i + 1; 
        }
        pch = strtok (NULL, "&");
    }
    return NULL;
}
