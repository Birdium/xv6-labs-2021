#include "kernel/types.h"
#include "user/user.h"
int main() {
	int p_f2s[2], p_s2f[2];

	pipe(p_f2s);
	pipe(p_s2f);

	char buf[1];

	int pid = fork();
	if (pid > 0) { // parent
		write(p_f2s[1], buf, 1);
		close(p_f2s[1]);
		read(p_s2f[0], buf, 1);
		printf("%d: received pong\n", getpid());
		close(p_s2f[0]);
	}
	else { // son
		read(p_f2s[0], buf, 1);
		printf("%d: received ping\n", getpid());
		close(p_f2s[0]);
		write(p_s2f[1], buf, 1);
		close(p_s2f[1]);
	}
	exit(0);

	// int pid = fork();
	// if (pid > 0) { // parent
	// 	close(p_f2s[0]);
	// 	close(p_s2f[1]);
	// 	read()
	// }
	// else { // son

	// }
}