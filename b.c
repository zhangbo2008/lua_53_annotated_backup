#include <stdio.h>
#include <stdlib.h>
 
int
main(int argc, char *argv[])
{
    int a = atoi(argv[1]);
 
    if (a == 1)
    {
        goto failed1;
    }
    else if(a == 2)
    {
        goto failed2;
    }
    else
    {
        goto failed3;
    }
 
failed1:
    printf("get failed1\n");
failed2:
    printf("get failed2\n");
failed3:
    printf("get failed3\n");
 
    printf("a + b = 3\n");
    return 0;
}