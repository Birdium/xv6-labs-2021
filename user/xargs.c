#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int
main(int argc, char *argv[])
{
    char *nargv[64];
    char buf[1024];
    char ibuf[64];
    char *bufhptr, *buftptr;
    char **nargvptr;
    for(int i = 1; i < argc; i++) {
        nargv[i-1] = argv[i];
    }
    bufhptr = buftptr = buf;
    nargvptr = nargv + argc - 1;
    int split = 0;
    int insz;
    while((insz = read(0, ibuf, sizeof(ibuf))) > 0) {
        for (int i = 0; i < insz; i++) {
            char ch = ibuf[i];
            switch (ch) {
            case '\n': // ready to send
                *buftptr = '\0';
                *(nargvptr++) = bufhptr;
                *nargvptr = buftptr;
                if (fork() == 0) {
                    exec(argv[1], nargv);
                }
                wait(0);
                split = 0;
                bufhptr = buftptr = buf;
                nargvptr = nargv + argc - 1;
                break;
            case ' ': // split arg
                if (split) {
                    *(buftptr++) = '\0';
                    *(nargvptr++) = bufhptr;
                    bufhptr = buftptr;
                    split = 0;
                }
                break;
            default:
                *(buftptr++) = ch;
                split = 1;
                break;
            }
        }
    }
    exit(0);
}
