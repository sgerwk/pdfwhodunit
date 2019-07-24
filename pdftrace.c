/*
 * pdftrace.c
 *
 * trace a PDF file content back to its file position
 *
 * LD_PRELOAD=./pdftrace.so DOUBLEBUFFERING=no hovacui file.pdf
 * LD_PRELOAD=./pdftrace.so pdfwhodunit file.pdf
 *
 * if a program calls tracer_register(tracer), as pdfwhodunit does,
 * then pdftrace.so calls tracer() at each file read and:
 * - if it returns 0, continues
 * - if it returns 1, prints the offset and size of the last read
 * - if it returns 2, also stops and waits for a key
 * the program should also calls tracer_final() at the end
 *
 * it the program does not call tracer_register(tracer):
 * - F3: stop and display all file reads
 * - F4: do that only when the object being read is different from the previous
 */

/*
 * how it works
 * ------------
 *
 * poppler does not store the content of the PDF objects; it reads them from
 * the file when it needs them; this allows tracing when the file content is
 * used; pdftrace.so intercepts the calls to open(2) and read(2) between
 * poppler and libc.so
 *
 * when the program opens a file, pdftrace.so checks whether its extension is
 * ".pdf"; if so, it stores the resulting file descriptor; when the program
 * reads from that descriptor, the operation is logged
 *
 * if the program has registered a tracer, pdftrace.so calls it; depending on
 * its return value, the location of the file that is read is printed or not,
 * and a keystroke is waited for or not; both are done if no tracer is
 * registered, unless the last keystroke was F4
 *
 * since poppler stores the xref table, it knows where the objects begin in the
 * file; when it needs an object, it always reads it from the beginning; as a
 * result, if the result of read begins with "n 0 obj", then object n begins
 * from that offset in the file; if a subsequent read is from an offset larger
 * than this, with no other object beginning in between, it is another access
 * to the same object; if no tracer is registered but the last keystroke was
 * F4, then printing and waiting for a keystroke is only done when switching
 * from reading an object to another
 *
 * limitations:
 * - relies on poppler always reading objects from their beginning the first
 *   time; this is the logical way to process a pdf file, but is not guaranteed
 *   in general
 * - if the string "n 0 obj" is within a stream or comment and a read operation
 *   starts from that exact point by chance, it will be mistaken as the start
 *   of a new object
 * - not all "open" and "read" calls are currently implemented (easy to fix)
 */

/*
 * todo
 * ----
 *
 * - allow tracing a single object, or a list or range
 *   only stop and display reads of that object(s)
 *   pass the objects to trace as an environment variable
 * - allow a specific file(s) to be traced
 * - distinguish between xref and trailer: currently all printed as "XREF"
 * - print type of object, if possible: requires parsing the buffer of read,
 *   which may be too short to include the type; not all objects have types
 * - save log of read offsets and object numbers to file
 *   another key (F5) adds a mark to the file, to allow finding the place
 *   where relevant things happended
 * - same key may also save a screenshot, image file name saved to log
 * - upon open, scan the pdf file for the xref and fill the database of object
 *   offsets; this solves the problem of spurious object starts
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <errno.h>
#include <termios.h>

/*
 * stdio, unistd etc, which cannot be included
 */
typedef struct _FILE FILE;
extern int fprintf(FILE *, const char *, ...);
extern int printf(const char *, ...);
extern size_t fwrite(const void *, size_t, size_t, FILE *);
extern int sscanf(const char *, const char *, ...);
extern int getchar(void);
extern int fileno(FILE *);
extern int fflush(FILE *);
extern void perror(const char *);
extern FILE *stdout;
extern off64_t lseek64(int, off64_t, int);
#define SEEK_CUR 1

/*
 * pdftrace probe
 */
void pdftrace() {
}

/*
 * terminal control
 */
int useterminal = 1;
#define HOME  "\033[1;1H"
#define CLEAR "\033[2J"
#define DOWN  "\033[20E"
void usenoterminal() {
	useterminal = 0;
}

/*
 * tracing function
 */
int (*tracer)() = NULL;
void register_tracer(int (*t)()) {
	tracer = t;
}

/*
 * store the descriptor of the first pdf file that is opened
 */
char *tracedext = ".pdf";
int tracedfd = -1;

void generic_open(const char *filename, int fd) {
	char *suffix;
	printf("%s(%s,%d)\r\n", __FUNCTION__, filename, fd);
	if (tracedfd != -1)
		return;
	suffix = strstr(filename, tracedext);
	if (suffix != NULL && suffix[strlen(tracedext)] == '\0')
		tracedfd = fd;
}

/*
 * offset of the objects and the xref table
 */
#define MAXOBJ 4096
int nobj = 0;
off_t objoffset[MAXOBJ];
off_t xrefoffset;

void init_offsets() {
	int i;
	for (i = 0; i < MAXOBJ; i++)
		objoffset[i] = 0;
	xrefoffset = 0;
}

/*
 * determine the object that contains the given offset
 */
int which_obj(off_t offset) {
	int i, obj = -2;
	for (i = 0; i < nobj; i++)
		if (offset > objoffset[i] && obj < i)
			obj = i;
	return obj;
}

/*
 * log a read
 */
void log_read(off_t offset, size_t count,
		int obj, int startobject, int startxref) {
	char *progchar = "|/-\\";
	static int progress = 0;

	if (useterminal)
		printf(HOME);
	printf("read from %-9lld ", (unsigned long long) offset);
	printf("to %-9lld ", (unsigned long long) offset + count);
	if (useterminal)
		printf("%c    ", progchar[progress]);
	printf("\r\n");
	progress = (progress + 1) % 4;
	if (startobject)
		printf("OBJECT %-9d (START)              \r\n", obj);
	else if (startxref)
		printf("XREF (START)                          \r\n");
	else if (xrefoffset > 0 && offset >= xrefoffset)
		printf("XREF                             \r\n");
	else if (obj > 0)
		printf("OBJECT %-9d                      \r\n", obj);
	else
		printf("                                      \r\n");
	printf("                                      \r\n");
}

/*
 * wait for input
 */
int stopatread = 1;
void wait_input() {
	char c;
	c = getchar();
	if (c != '\033')
		return;
	c = getchar();
	if (c != '[')
		return;
	c = getchar();
	if (c != '[')
		return;
	c = getchar();		// F3=C, F4=D
		stopatread = c == 'C';
}

/*
 * trace file reads
 */
off_t prevoffset = -1;
size_t prevcount = -1;
int prevobj = -2;
void generic_read(int fd, char *buf, off_t offset, size_t count) {
	char newline;
	int startxref, startobject;
	int obj;
	int t;

	if (count <= 0 || fd != tracedfd)
		return;

	startobject = sscanf(buf, "%d 0 obj%c", &obj, &newline) == 2;
	startxref = xrefoffset > 0 ?
		offset == xrefoffset :
		count >= 4 && ! strncmp(buf, "xref", 4);

	if (! startobject)
		obj = which_obj(offset);
	else if (obj < MAXOBJ) {
		if (objoffset[obj] == 0)
			objoffset[obj] = offset;
		if (nobj < obj + 1)
			nobj = obj + 1;
	}
	if (startxref && xrefoffset == 0)
		xrefoffset = offset;

	if (tracer != NULL) {
		t = tracer();
		if (t > 0)
			log_read(prevoffset, prevcount, prevobj, 0, 0);
		if (t >= 2)
			wait_input();
	}
	else if (prevobj != obj || startxref || stopatread) {
		log_read(offset, count, obj, startobject, startxref);
		wait_input();
	}

	prevoffset = offset;
	prevcount = count;
	prevobj = obj;
}

/*
 * trace after the last read
 */
void traced_final() {
	int t;
	if (tracer == NULL)
		return;
	t = tracer();
	if (t > 0)
		log_read(prevoffset, prevcount, prevobj, 0, 0);
	if (t >= 2)
		wait_input();
}

/*
 * initialization and finalization
 */
size_t granularity = 0;
struct termios tcorig;

static void __attribute__((constructor)) init() {
	char *g;

	setenv("DOUBLEBUFFERING", "no", 0);
	g = getenv("GRANULARITY");
	if (g)
		granularity = atoi(g);

	struct termios raw;
	tcgetattr(0, &tcorig);
	raw = tcorig;
	raw.c_lflag &= ~(ECHO | ICANON);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &raw);

	printf(CLEAR);
	printf(DOWN);
	fflush(stdout);

	init_offsets();
}

static void __attribute__((destructor)) fini() {
	tcsetattr(0, TCSANOW, &tcorig);
}

/*
 * intercept open calls: open, open64, fopen, fopen64
 * tbd: fdopen, freopen and their 64 versions
 */
int (*open_orig)(const char *filename, int flags, mode_t mode) = NULL;
int open(const char *filename, int flags, mode_t mode) {
	int fd;
	if (open_orig == NULL)
		open_orig = dlsym(RTLD_NEXT, "open");
	fd = open_orig(filename, flags, mode);
	generic_open(filename, fd);
	return fd;
}

int (*open64_orig)(const char *filename, int flags, mode_t mode) = NULL;
int open64(const char *filename, int flags, mode_t mode) {
	int fd;
	if (open64_orig == NULL)
		open64_orig = dlsym(RTLD_NEXT, "open64");
	fd = open64_orig(filename, flags, mode);
	generic_open(filename, fd);
	return fd;
}

FILE *(*fopen_orig)(char *filename, char *mode) = NULL;
FILE *fopen(char *filename, char *mode) {
	FILE *f;
	if (fopen_orig == NULL)
		fopen_orig = dlsym(RTLD_NEXT, "fopen");
	f = fopen_orig(filename, mode);
	generic_open(filename, f == NULL ? -1 : fileno(f));
	return f;
}

FILE *(*fopen64_orig)(char *filename, char *mode) = NULL;
FILE *fopen64(char *filename, char *mode) {
	FILE *f;
	if (fopen64_orig == NULL)
		fopen64_orig = dlsym(RTLD_NEXT, "fopen64");
	f = fopen64_orig(filename, mode);
	generic_open(filename, f == NULL ? -1 : fileno(f));
	return f;
}

/*
 * intercept read calls: read, pread64
 * tbd: all others
 */
ssize_t (*read_orig)(int fd, char *buf, size_t count) = NULL;
ssize_t read(int fd, char *buf, size_t count) {
	size_t ret, c;
	off64_t offset;
	if (read_orig == NULL)
		read_orig = dlsym(RTLD_NEXT, "read");
	c = fd == tracedfd && granularity != 0 && granularity < count ?
		granularity : count;
	offset = lseek64(fd, 0, SEEK_CUR);
	ret = read_orig(fd, buf, c);
	generic_read(fd, buf, offset, ret);
	return ret;
}

ssize_t (*pread64_orig)(int fd, void *buf, size_t count, off_t offset) = NULL;
ssize_t pread64(int fd, void *buf, size_t count, off_t offset) {
	size_t ret, c;
	if (pread64_orig == NULL)
		pread64_orig = dlsym(RTLD_NEXT, "pread64");
	c = fd == tracedfd && granularity != 0 && granularity < count ?
		granularity : count;
	ret = pread64_orig(fd, buf, c, offset);
	generic_read(fd, buf, offset, ret);
	return ret;
}

