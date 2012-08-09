#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#endif

#define ENDHDR 22
static const long END_MAXLEN = 0xFFFF + ENDHDR;

#define READBLOCKSZ 128

#define CH(b, n) (((unsigned char *)(b))[n])
#define SH(b, n) (CH(b, n) | (CH(b, n+1) << 8))

#define ENDOFF(b) LG(b, 16)         /* central directory offset */
#define ENDCOM(b) SH(b, 20)         /* size of zip file comment */

#define g_return_val_if_fail(expr, val)    do{        \
     if (expr) { } else                        \
       {                            \
        printf (                    \
            "file %s: line %d: assertion `%s' failed\n",    \
            __FILE__,                    \
            __LINE__,                    \
            #expr);                        \
        return (val);                        \
       };                }while (0)

static int
readFully(FILE *zfd, void *buf, long len) {
  char *bp = (char *) buf;

  while (len > 0) {
        long limit = ((((long) 1) << 31) - 1);
        int count = (len < limit) ?
            (int) len :
            (int) limit;
        int n;
        n = fread(bp, sizeof(char), count, zfd);
        if (n > 0) {
            bp += n;
            len -= n;
        } else if (n == -1 && errno == EINTR) {
          /* Retry after EINTR (interrupted by signal).
             We depend on the fact that n == -1. */
            continue;
        } else { /* EOF or IO error */
            return -1;
        }
    }
    return 0;
}

static int
readFullyAt(FILE *zfd, void *buf, long len, long offset)
{
    if (fseek(zfd, offset, SEEK_SET) == -1) {
        return -1; /* lseek failure. */
    }

    return readFully(zfd, buf, len);
}

int setFileLength(FILE* file, unsigned int len)
{
    int fd;
#ifdef _WIN32
    HANDLE hfile;
    fseek(file, len, SEEK_SET);
    fd = _fileno(file);
    hfile = (HANDLE)_get_osfhandle(fd);
    return SetEndOfFile(hfile);
#else
    fd = fileno(file);
    return ftruncate(fd, len) == 0;
#endif
} 

static long
fixEND(FILE *zfd, long len)
{
    char buf[READBLOCKSZ];
    long pos;
    const long minHDR = len - END_MAXLEN > 0 ? len - END_MAXLEN : 0;
    const long minPos = minHDR - (sizeof(buf)-ENDHDR);

    for (pos = len - sizeof(buf); pos >= minPos; pos -= (sizeof(buf)-ENDHDR)) {

        int i;
        long off = 0;
        if (pos < 0) {
            /* Pretend there are some NUL bytes before start of file */
            off = -pos;
            memset(buf, '\0', off);
        }

        if (readFullyAt(zfd, buf + off, sizeof(buf) - off,
                        pos + off) == -1) {
            return -1;  /* System error */
        }

        /* Now scan the block backwards for END header signature */
        for (i = sizeof(buf) - ENDHDR; i >= 0; i--) {
            if (buf[i+0] == 'P'    &&
                buf[i+1] == 'K'    &&
                buf[i+2] == '\005' &&
                buf[i+3] == '\006') {
                    unsigned int virSize = pos + i + ENDHDR + ENDCOM(buf + i);
                    if (virSize != len) {
                        setFileLength(zfd, virSize);
                    }
                    return pos + i;
            }
        }
    }
    return 0; /* END header not found */
}

int main(int argc, char* argv[])
{
    FILE *file;
    long ret;

    g_return_val_if_fail(argc == 2, -1);

    file = fopen(argv[1], "rb+");
    fseek(file, 0L, SEEK_END);
    ret = fixEND(file, ftell(file));
    printf("%ld\n", ret);
    fclose(file);

    return ret;
}
