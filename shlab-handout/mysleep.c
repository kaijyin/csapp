#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char **argv) 
{
    int i, secs;

    if (argc != 2) {
	fprintf(stderr, "Usage: %s <n>\n", argv[0]);
	exit(0);
    }
    secs = atoi(argv[1]);
	unsigned int res=sleep(secs);
    printf("%u\n",res);
    printf("hello world!\n");
    exit(0);
}
