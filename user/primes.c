#include "kernel/types.h"
#include "user/user.h"
void prime(int *p) {
	int q;
	if (!read(p[0], &q, 4)) {
		exit(0);
	}
	printf("prime %d\n", q);
	int r[2];
	pipe(r);
	if (fork() == 0) {
		close(p[0]);
		close(r[1]);
		prime(r);
	}
	else {
		close(r[0]);
		int t;
		while (read(p[0], &t, 4)) {
			if (t % q != 0) {
				write(r[1], &t, 4);
			}
		}
		close(p[0]);
		close(r[1]);
		wait(0);
	}
}

int main() {
	int p[2];
	pipe(p);
	for (int i = 2; i <= 35; i++) {
		write(p[1], &i, 4);
	}
	close(p[1]);
	prime(p);
	exit(0);
}