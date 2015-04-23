#include <stdio.h>
#include <stdlib.h>

#define PageSize	4096
// #define NumPages	512
#define NumPages	150

int sparse[NumPages][PageSize];	/* use only the first element in the row */

int
main()
{

	printf("Entering custom swap test\n");

/* move number in so that sparse[i][0]=i */
	for (int i=0; i<NumPages; i++) {
		for (int j = 0; j < PageSize; ++j)
		{
			sparse[i][j]=i;
		}
	}

	for (int i=NumPages-1; i>=0; i--) {
		int passed = 1;
		for (int j = 0; j < PageSize; ++j)
		{
			if (sparse[i][j] != i)
			{
				printf("Expected %d found %d\n", i,sparse[i][j]);
				passed = 0;
				break;
			}
		}
		const char *status = passed? "passed":"failed";
		printf("Page %d %s\n",i,status);
	}
	return 0;
}
