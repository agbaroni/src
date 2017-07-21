#if 0
exec cc -Wall -Wextra -pedantic $@ -lutil -o $(dirname $0)/hnel $0
#endif

// Wrapper for preserving HJKL in Tarmak layouts.

#include <err.h>
#include <poll.h>
#include <stdlib.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

#if defined __FreeBSD__
#include <libutil.h>
#elif defined __linux__
#include <pty.h>
#else
#include <util.h>
#endif

static ssize_t writeAll(int fd, const char *buf, size_t len) {
    ssize_t writeLen;
    while (0 < (writeLen = write(fd, buf, len))) {
        buf += writeLen;
        len -= writeLen;
    }
    return writeLen;
}

static struct termios saveTerm;

static void restoreTerm(void) {
    tcsetattr(STDERR_FILENO, TCSADRAIN, &saveTerm);
}

int main(int argc, char *argv[]) {
    if (argc < 2) return EX_USAGE;

    char table[256] = {0};
    table['n'] = 'j'; table['N'] = 'J'; table[CTRL('N')] = CTRL('J');
    table['e'] = 'k'; table['E'] = 'K'; table[CTRL('E')] = CTRL('K');
    table['j'] = 'e'; table['J'] = 'E'; table[CTRL('J')] = CTRL('E');
    table['k'] = 'n'; table['K'] = 'N'; table[CTRL('K')] = CTRL('N');

    int master;
    pid_t pid = forkpty(&master, NULL, NULL, NULL);
    if (pid < 0) err(EX_OSERR, "forkpty");

    if (!pid) {
        execvp(argv[1], argv + 1);
        err(EX_OSERR, "%s", argv[1]);
    }

    int error = tcgetattr(STDERR_FILENO, &saveTerm);
    if (error) err(EX_IOERR, "tcgetattr");
    atexit(restoreTerm);

    struct termios raw;
    cfmakeraw(&raw);
    error = tcsetattr(STDERR_FILENO, TCSADRAIN, &raw);
    if (error) err(EX_IOERR, "tcsetattr");

    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = master, .events = POLLIN },
    };

    char buf[4096];
    ssize_t len;
    while (0 < poll(fds, 2, -1)) {
        if (fds[0].revents) {
            len = read(STDIN_FILENO, buf, sizeof(buf));
            if (len < 0) err(EX_IOERR, "read(%d)", STDIN_FILENO);

            if (len == 1) {
                unsigned char c = buf[0];
                if (table[c]) buf[0] = table[c];
            }

            len = writeAll(master, buf, len);
            if (len < 0) err(EX_IOERR, "write(%d)", master);
        }

        if (fds[1].revents) {
            len = read(master, buf, sizeof(buf));
            if (len < 0) err(EX_IOERR, "read(%d)", master);
            len = writeAll(STDOUT_FILENO, buf, len);
            if (len < 0) err(EX_IOERR, "write(%d)", STDOUT_FILENO);
        }

        int status;
        pid_t dead = waitpid(pid, &status, WNOHANG);
        if (dead < 0) err(EX_OSERR, "waitpid(%d)", pid);
        if (dead) return WIFEXITED(status) ? WEXITSTATUS(status) : EX_SOFTWARE;
    }
    err(EX_IOERR, "poll");
}