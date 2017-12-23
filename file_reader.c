#include <stdio.h>
#include <stdlib.h>
int main(int argc, char const *argv[])
{
    const int maxBuf = 1048576;
    char fileName[100], *buf = malloc(maxBuf);
    scanf("%s", fileName);
    FILE *file = fopen(fileName, "r");
    fread(buf, 1, maxBuf, file);
    fputs(buf, stdout);
    free(buf);
    return 0;
}
