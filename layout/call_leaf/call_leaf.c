#include <stdio.h>
#include <inttypes.h>

int add_7(int a, int b, int c, int d,
		  int e, int f, int g)
{
	int x;
	x =  a + b + c + d + e + f + g;
	return x;
}

int main(int argc, char **argv) 
{
	printf("%d\n", add_7(1, 2, 3, 4, 5, 6, 7));
	printf("boom\n");
    return 0;
}