#include <stdio.h>
#include <sys/syscall.h>

int main(void)
{
	long ret = syscall(436);
	printf("System Call returned: %ld\n", ret);

	return 0;
}
