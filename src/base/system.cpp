/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "system.h"
#include "confusables.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/socket.h>

#if defined(WEBSOCKETS)
	#include "engine/shared/websockets.h"
#endif

#if defined(CONF_FAMILY_UNIX)
	#include <sys/time.h>
	#include <unistd.h>

	/* unix net includes */
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include <errno.h>
	#include <netdb.h>
	#include <netinet/in.h>
	#include <fcntl.h>
	#include <pthread.h>
	#include <arpa/inet.h>

	#include <dirent.h>

	#if defined(CONF_PLATFORM_MACOSX)
		// some lock and pthread functions are already defined in headers
		// included from Carbon.h
		// this prevents having duplicate definitions of those
		#define _lock_set_user_
		#define _task_user_

		#include <Carbon/Carbon.h>
	#endif

	#if defined(__ANDROID__)
		#include <android/log.h>
	#endif

#elif defined(CONF_FAMILY_WINDOWS)
	#define WIN32_LEAN_AND_MEAN
	#undef _WIN32_WINNT
	#define _WIN32_WINNT 0x0501 /* required for mingw to get getaddrinfo to work */
	#include <windows.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <fcntl.h>
	#include <direct.h>
	#include <errno.h>
	#include <process.h>
	#include <shellapi.h>
	#include <wincrypt.h>
#else
	#error NOT IMPLEMENTED
#endif

#if defined(CONF_PLATFORM_SOLARIS)
	#include <sys/filio.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef FUZZING
static unsigned char gs_NetData[1024];
static int gs_NetPosition = 0;
static int gs_NetSize = 0;
#endif

IOHANDLE io_stdin() { return (IOHANDLE)stdin; }
IOHANDLE io_stdout() { return (IOHANDLE)stdout; }
IOHANDLE io_stderr() { return (IOHANDLE)stderr; }

static DBG_LOGGER loggers[16];
static int num_loggers = 0;

static NETSTATS network_stats = {0};
static MEMSTATS memory_stats = {0};

#define VLEN 128
#define PACKETSIZE 1400
typedef struct
{
#ifdef CONF_PLATFORM_LINUX
	int pos;
	int size;
	struct mmsghdr msgs[VLEN];
	struct iovec iovecs[VLEN];
	char bufs[VLEN][PACKETSIZE];
	char sockaddrs[VLEN][128];
#else
	char buf[PACKETSIZE];
#endif
} NETSOCKET_BUFFER;

void net_buffer_init(NETSOCKET_BUFFER *buffer);
void net_buffer_reinit(NETSOCKET_BUFFER *buffer);
void net_buffer_simple(NETSOCKET_BUFFER *buffer, char **buf, int *size);


struct NETSOCKET_INTERNAL
{
	int type;
	int ipv4sock;
	int ipv6sock;
	int web_ipv4sock;

	NETSOCKET_BUFFER buffer;
};
static NETSOCKET_INTERNAL invalid_socket = {NETTYPE_INVALID, -1, -1, -1};

#define AF_WEBSOCKET_INET (0xee)

void dbg_logger(DBG_LOGGER logger)
{
	loggers[num_loggers++] = logger;
}

void dbg_assert_imp(const char *filename, int line, int test, const char *msg)
{
	if(!test)
	{
		dbg_msg("assert", "%s(%d): %s", filename, line, msg);
		dbg_break();
	}
}

void dbg_break()
{
	*((volatile unsigned*)0) = 0x0;
}

#if !defined(CONF_PLATFORM_MACOSX)
#define QUEUE_SIZE 16

typedef struct
{
	char q[QUEUE_SIZE][1024*4];
	int begin;
	int end;
	SEMAPHORE mutex;
	SEMAPHORE notempty;
	SEMAPHORE notfull;
} Queue;

static int dbg_msg_threaded = 0;
static Queue log_queue;

int queue_empty(Queue *q)
{
	return q->begin == q->end;
}

int queue_full(Queue *q)
{
	return ((q->end+1) % QUEUE_SIZE) == q->begin;
}

void dbg_msg_thread(void *v)
{
	char str[1024*4];
	int i;
	int f;
	while(1)
	{
		semaphore_wait(&log_queue.notempty);
		semaphore_wait(&log_queue.mutex);
		f = queue_full(&log_queue);

		str_copy(str, log_queue.q[log_queue.begin], sizeof(str));

		log_queue.begin = (log_queue.begin + 1) % QUEUE_SIZE;

		if(f)
			semaphore_signal(&log_queue.notfull);

		if(!queue_empty(&log_queue))
			semaphore_signal(&log_queue.notempty);

		semaphore_signal(&log_queue.mutex);

		for(i = 0; i < num_loggers; i++)
			loggers[i](str);
	}
}

void dbg_enable_threaded()
{
	Queue *q;
	void *Thread;

	q = &log_queue;
	q->begin = 0;
	q->end = 0;
	semaphore_init(&q->mutex);
	semaphore_init(&q->notempty);
	semaphore_init(&q->notfull);
	semaphore_signal(&q->mutex);
	semaphore_signal(&q->notfull);

	dbg_msg_threaded = 1;

	Thread = thread_init(dbg_msg_thread, 0);
	thread_detach(Thread);
}
#endif

void dbg_msg(const char *sys, const char *fmt, ...)
{
	va_list args;
	char str[1024*4];
	char *msg;
	int i, len;

	str_format(str, sizeof(str), "[%08x][%s]: ", (int)time(0), sys);
	len = strlen(str);
	msg = (char *)str + len;

	va_start(args, fmt);
#if defined(CONF_FAMILY_WINDOWS)
	_vsnprintf(msg, sizeof(str)-len, fmt, args);
#else
	vsnprintf(msg, sizeof(str)-len, fmt, args);
#endif
	va_end(args);

	for(i = 0; i < num_loggers; i++)
		loggers[i](str);
}

static void logger_stdout(const char *line)
{
	printf("%s\n", line);
	fflush(stdout);
#if defined(__ANDROID__)
	__android_log_print(ANDROID_LOG_INFO, "DDNet", "%s", line);
#endif
}

static void logger_debugger(const char *line)
{
#if defined(CONF_FAMILY_WINDOWS)
	OutputDebugString(line);
	OutputDebugString("\n");
#endif
}


static IOHANDLE logfile = 0;
static void logger_file(const char *line)
{
	io_write(logfile, line, strlen(line));
	io_write_newline(logfile);
	io_flush(logfile);
}

void dbg_logger_stdout() { dbg_logger(logger_stdout); }

void dbg_logger_debugger() { dbg_logger(logger_debugger); }
void dbg_logger_file(const char *filename)
{
	logfile = io_open(filename, IOFLAG_WRITE);
	if(logfile)
		dbg_logger(logger_file);
	else
		dbg_msg("dbg/logger", "failed to open '%s' for logging", filename);

}
/* */

typedef struct MEMHEADER
{
	const char *filename;
	int line;
	int size;
	struct MEMHEADER *prev;
	struct MEMHEADER *next;
} MEMHEADER;

typedef struct MEMTAIL
{
	int guard;
} MEMTAIL;

static struct MEMHEADER *first = 0;
static const int MEM_GUARD_VAL = 0xbaadc0de;

void *mem_alloc_debug(const char *filename, int line, unsigned size, unsigned alignment)
{
	/* TODO: fix alignment */
	/* TODO: add debugging */
	MEMTAIL *tail;
	MEMHEADER *header = (struct MEMHEADER *)malloc(size+sizeof(MEMHEADER)+sizeof(MEMTAIL));
	dbg_assert(header != 0, "mem_alloc failure");
	if(!header)
		return NULL;
	tail = (struct MEMTAIL *)(((char*)(header+1))+size);
	header->size = size;
	header->filename = filename;
	header->line = line;

	memory_stats.allocated += header->size;
	memory_stats.total_allocations++;
	memory_stats.active_allocations++;

	tail->guard = MEM_GUARD_VAL;

	header->prev = (MEMHEADER *)0;
	header->next = first;
	if(first)
		first->prev = header;
	first = header;

	/*dbg_msg("mem", "++ %p", header+1); */
	return header+1;
}

void mem_free(void *p)
{
	if(p)
	{
		MEMHEADER *header = (MEMHEADER *)p - 1;
		MEMTAIL *tail = (MEMTAIL *)(((char*)(header+1))+header->size);

		if(tail->guard != MEM_GUARD_VAL)
			dbg_msg("mem", "!! %p", p);
		/* dbg_msg("mem", "-- %p", p); */
		memory_stats.allocated -= header->size;
		memory_stats.active_allocations--;

		if(header->prev)
			header->prev->next = header->next;
		else
			first = header->next;
		if(header->next)
			header->next->prev = header->prev;

		free(header);
	}
}

void mem_debug_dump(IOHANDLE file)
{
	char buf[1024];
	MEMHEADER *header = first;
	if(!file)
		file = io_open("memory.txt", IOFLAG_WRITE);

	if(file)
	{
		while(header)
		{
			str_format(buf, sizeof(buf), "%s(%d): %d", header->filename, header->line, header->size);
			io_write(file, buf, strlen(buf));
			io_write_newline(file);
			header = header->next;
		}

		io_close(file);
	}
}


void mem_copy(void *dest, const void *source, unsigned size)
{
	memcpy(dest, source, size);
}

void mem_move(void *dest, const void *source, unsigned size)
{
	memmove(dest, source, size);
}

void mem_zero(void *block,unsigned size)
{
	memset(block, 0, size);
}

int mem_check_imp()
{
	MEMHEADER *header = first;
	while(header)
	{
		MEMTAIL *tail = (MEMTAIL *)(((char*)(header+1))+header->size);
		if(tail->guard != MEM_GUARD_VAL)
		{
			dbg_msg("mem", "Memory check failed at %s(%d): %d", header->filename, header->line, header->size);
			return 0;
		}
		header = header->next;
	}

	return 1;
}

IOHANDLE io_open_impl(const char *filename, int flags)
{
	dbg_assert(flags == (IOFLAG_READ | IOFLAG_SKIP_BOM) || flags == IOFLAG_READ || flags == IOFLAG_WRITE || flags == IOFLAG_APPEND, "flags must be read, read+skipbom, write or append");
#if defined(CONF_FAMILY_WINDOWS)
	if((flags & IOFLAG_READ) != 0)
	{
		// check for filename case sensitive
		WIN32_FIND_DATAW finddata;
		HANDLE handle;
		WCHAR wBuffer[IO_MAX_PATH_LENGTH];
		char buffer[IO_MAX_PATH_LENGTH];

		int length = str_length(filename);
		if(!filename || !length || filename[length - 1] == '\\')
			return 0x0;
		MultiByteToWideChar(CP_UTF8, 0, filename, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
		handle = FindFirstFileW(wBuffer, &finddata);
		if(handle == INVALID_HANDLE_VALUE)
			return 0x0;
		WideCharToMultiByte(CP_UTF8, 0, finddata.cFileName, -1, buffer, sizeof(buffer), NULL, NULL);
		if(str_comp(filename + length - str_length(buffer), buffer) != 0)
		{
			FindClose(handle);
			return 0x0;
		}
		FindClose(handle);
		return (IOHANDLE) _wfsopen(wBuffer, L"rb", _SH_DENYNO);
	}
	if(flags == IOFLAG_WRITE)
	{
		WCHAR wBuffer[IO_MAX_PATH_LENGTH];
		MultiByteToWideChar(CP_UTF8, 0, filename, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
		return (IOHANDLE) _wfsopen(wBuffer, L"wb", _SH_DENYNO);
	}
	if(flags == IOFLAG_APPEND)
	{
		WCHAR wBuffer[IO_MAX_PATH_LENGTH];
		MultiByteToWideChar(CP_UTF8, 0, filename, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
		return (IOHANDLE) _wfsopen(wBuffer, L"ab", _SH_DENYNO);
	}
	return 0x0;
#else
	if((flags & IOFLAG_READ) != 0)
		return (IOHANDLE) fopen(filename, "rb");
	if(flags == IOFLAG_WRITE)
		return (IOHANDLE) fopen(filename, "wb");
	if(flags == IOFLAG_APPEND)
		return (IOHANDLE) fopen(filename, "ab");
	return 0x0;
#endif
}

IOHANDLE io_open(const char *filename, int flags)
{
	IOHANDLE result = io_open_impl(filename, flags);
	unsigned char buf[3];
	if((flags & IOFLAG_SKIP_BOM) == 0 || !result)
	{
		return result;
	}
	if(io_read(result, buf, sizeof(buf)) != 3 || buf[0] != 0xef || buf[1] != 0xbb || buf[2] != 0xbf)
	{
		io_seek(result, 0, IOSEEK_START);
	}
	return result;
}

unsigned io_read(IOHANDLE io, void *buffer, unsigned size)
{
	return fread(buffer, 1, size, (FILE*)io);
}

void io_read_all(IOHANDLE io, void **result, unsigned *result_len)
{
	unsigned len = (unsigned) io_length(io);
	char *buffer = (char *) malloc(len + 1);
	unsigned read = io_read(io, buffer, len + 1); // +1 to check if the file size is larger than expected
	if(read < len)
	{
		buffer = (char *) realloc(buffer, read + 1);
		len = read;
	}
	else if(read > len)
	{
		unsigned cap = 2 * read;
		len = read;
		buffer = (char *) realloc(buffer, cap);
		while((read = io_read(io, buffer + len, cap - len)) != 0)
		{
			len += read;
			if(len == cap)
			{
				cap *= 2;
				buffer = (char *) realloc(buffer, cap);
			}
		}
		buffer = (char *) realloc(buffer, len + 1);
	}
	buffer[len] = 0;
	*result = buffer;
	*result_len = len;
}

static int mem_has_null(const void *block, unsigned size)
{
	const unsigned char *bytes = (const unsigned char *) block;
	unsigned i;
	for(i = 0; i < size; i++)
	{
		if(bytes[i] == 0)
		{
			return 1;
		}
	}
	return 0;
}

char *io_read_all_str(IOHANDLE io)
{
	void *buffer;
	unsigned len;
	io_read_all(io, &buffer, &len);
	if(mem_has_null(buffer, len))
	{
		mem_free(buffer);
		return 0x0;
	}
	return (char *) buffer;
}

unsigned io_skip(IOHANDLE io, int size)
{
	fseek((FILE*)io, size, SEEK_CUR);
	return size;
}

int io_seek(IOHANDLE io, int offset, int origin)
{
	int real_origin;

	switch(origin)
	{
	case IOSEEK_START:
		real_origin = SEEK_SET;
		break;
	case IOSEEK_CUR:
		real_origin = SEEK_CUR;
		break;
	case IOSEEK_END:
		real_origin = SEEK_END;
		break;
	default:
		return -1;
	}

	return fseek((FILE*)io, offset, real_origin);
}

long int io_tell(IOHANDLE io)
{
	return ftell((FILE*)io);
}

long int io_length(IOHANDLE io)
{
	long int length;
	io_seek(io, 0, IOSEEK_END);
	length = io_tell(io);
	io_seek(io, 0, IOSEEK_START);
	return length;
}

unsigned io_write(IOHANDLE io, const void *buffer, unsigned size)
{
	return fwrite(buffer, 1, size, (FILE*)io);
}

unsigned io_write_newline(IOHANDLE io)
{
#if defined(CONF_FAMILY_WINDOWS)
	return fwrite("\r\n", 1, 2, (FILE*)io);
#else
	return fwrite("\n", 1, 1, (FILE*)io);
#endif
}

int io_close(IOHANDLE io)
{
	fclose((FILE*)io);
	return 1;
}

int io_flush(IOHANDLE io)
{
	fflush((FILE*)io);
	return 0;
}

struct THREAD_RUN
{
	void (*threadfunc)(void *);
	void *u;
};

#if defined(CONF_FAMILY_UNIX)
static void *thread_run(void *user)
#elif defined(CONF_FAMILY_WINDOWS)
static unsigned long __stdcall thread_run(void *user)
#else
#error not implemented
#endif
{
	struct THREAD_RUN *data = (struct THREAD_RUN *) user;
	void (*threadfunc)(void *) = data->threadfunc;
	void *u = data->u;
	free(data);
	threadfunc(u);
	return 0;
}

void *thread_init(void (*threadfunc)(void *), void *u)
{
	struct THREAD_RUN *data = (struct THREAD_RUN *) malloc(sizeof(*data));
	data->threadfunc = threadfunc;
	data->u = u;
#if defined(CONF_FAMILY_UNIX)
	{
		pthread_t id;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
#if defined(CONF_PLATFORM_MACOS)
		pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);
#endif
		if(pthread_create(&id, &attr, thread_run, data) != 0)
		{
			return 0;
		}
		return (void *) id;
	}
#elif defined(CONF_FAMILY_WINDOWS)
	return CreateThread(NULL, 0, thread_run, data, 0, NULL);
#else
#error not implemented
#endif
}

void thread_wait(void *thread)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_join((pthread_t)thread, NULL);
#elif defined(CONF_FAMILY_WINDOWS)
	WaitForSingleObject((HANDLE)thread, INFINITE);
#else
	#error not implemented
#endif
}

void thread_destroy(void *thread)
{
#if defined(CONF_FAMILY_UNIX)
	void *r = 0;
	pthread_join((pthread_t)thread, &r);
#else
	/*#error not implemented*/
#endif
}

void thread_yield()
{
#if defined(CONF_FAMILY_UNIX)
	sched_yield();
#elif defined(CONF_FAMILY_WINDOWS)
	Sleep(0);
#else
	#error not implemented
#endif
}

void thread_sleep(int milliseconds)
{
#if defined(CONF_FAMILY_UNIX)
	usleep(milliseconds*1000);
#elif defined(CONF_FAMILY_WINDOWS)
	Sleep(milliseconds);
#else
	#error not implemented
#endif
}

void thread_detach(void *thread)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_detach((pthread_t)(thread));
#elif defined(CONF_FAMILY_WINDOWS)
	CloseHandle(thread);
#else
	#error not implemented
#endif
}




#if defined(CONF_FAMILY_UNIX)
typedef pthread_mutex_t LOCKINTERNAL;
#elif defined(CONF_FAMILY_WINDOWS)
typedef CRITICAL_SECTION LOCKINTERNAL;
#else
	#error not implemented on this platform
#endif

LOCK lock_create()
{
	LOCKINTERNAL *lock = (LOCKINTERNAL*)mem_alloc(sizeof(LOCKINTERNAL), 4);

#if defined(CONF_FAMILY_UNIX)
	pthread_mutex_init(lock, 0x0);
#elif defined(CONF_FAMILY_WINDOWS)
	InitializeCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
	return (LOCK)lock;
}

void lock_destroy(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_mutex_destroy((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	DeleteCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
	mem_free(lock);
}

int lock_trylock(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	return pthread_mutex_trylock((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	return !TryEnterCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
}

void lock_wait(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_mutex_lock((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	EnterCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
}

void lock_unlock(LOCK lock)
{
#if defined(CONF_FAMILY_UNIX)
	pthread_mutex_unlock((LOCKINTERNAL *)lock);
#elif defined(CONF_FAMILY_WINDOWS)
	LeaveCriticalSection((LPCRITICAL_SECTION)lock);
#else
	#error not implemented on this platform
#endif
}

#if !defined(CONF_PLATFORM_MACOSX)
	#if defined(CONF_FAMILY_UNIX)
	void semaphore_init(SEMAPHORE *sem) { sem_init(sem, 0, 0); }
	void semaphore_wait(SEMAPHORE *sem) { sem_wait(sem); }
	void semaphore_signal(SEMAPHORE *sem) { sem_post(sem); }
	void semaphore_destroy(SEMAPHORE *sem) { sem_destroy(sem); }
	#elif defined(CONF_FAMILY_WINDOWS)
	void semaphore_init(SEMAPHORE *sem) { *sem = CreateSemaphore(0, 0, 10000, 0); }
	void semaphore_wait(SEMAPHORE *sem) { WaitForSingleObject((HANDLE)*sem, INFINITE); }
	void semaphore_signal(SEMAPHORE *sem) { ReleaseSemaphore((HANDLE)*sem, 1, NULL); }
	void semaphore_destroy(SEMAPHORE *sem) { CloseHandle((HANDLE)*sem); }
	#else
		#error not implemented on this platform
	#endif
#endif

static int new_tick = -1;

void set_new_tick()
{
	new_tick = 1;
}

/* -----  time ----- */
int64 time_get()
{
	static int64 last = 0;
	if(!new_tick)
		return last;
	if(new_tick != -1)
		new_tick = 0;

#if defined(CONF_FAMILY_UNIX)
	struct timeval val;
	gettimeofday(&val, NULL);
	last = (int64)val.tv_sec*(int64)1000000+(int64)val.tv_usec;
	return last;
#elif defined(CONF_FAMILY_WINDOWS)
	{
		int64 t;
		QueryPerformanceCounter((PLARGE_INTEGER)&t);
		if(t<last) /* for some reason, QPC can return values in the past */
			return last;
		last = t;
		return t;
	}
#else
	#error not implemented
#endif
}

int64 time_freq()
{
#if defined(CONF_FAMILY_UNIX)
	return 1000000;
#elif defined(CONF_FAMILY_WINDOWS)
	int64 t;
	QueryPerformanceFrequency((PLARGE_INTEGER)&t);
	return t;
#else
	#error not implemented
#endif
}

/* -----  network ----- */
static void netaddr_to_sockaddr_in(const NETADDR *src, struct sockaddr_in *dest)
{
	mem_zero(dest, sizeof(struct sockaddr_in));
	if(src->type != NETTYPE_IPV4)
	{
		dbg_msg("system", "couldn't convert NETADDR of type %d to ipv4", src->type);
		return;
	}

	dest->sin_family = AF_INET;
	dest->sin_port = htons(src->port);
	mem_copy(&dest->sin_addr.s_addr, src->ip, 4);
}

static void netaddr_to_sockaddr_in6(const NETADDR *src, struct sockaddr_in6 *dest)
{
	mem_zero(dest, sizeof(struct sockaddr_in6));
	if(src->type != NETTYPE_IPV6)
	{
		dbg_msg("system", "couldn't not convert NETADDR of type %d to ipv6", src->type);
		return;
	}

	dest->sin6_family = AF_INET6;
	dest->sin6_port = htons(src->port);
	mem_copy(&dest->sin6_addr.s6_addr, src->ip, 16);
}

static void sockaddr_to_netaddr(const struct sockaddr *src, NETADDR *dst)
{
	if(src->sa_family == AF_INET)
	{
		mem_zero(dst, sizeof(NETADDR));
		dst->type = NETTYPE_IPV4;
		dst->port = htons(((struct sockaddr_in*)src)->sin_port);
		mem_copy(dst->ip, &((struct sockaddr_in*)src)->sin_addr.s_addr, 4);
	}
	else if(src->sa_family == AF_INET6)
	{
		mem_zero(dst, sizeof(NETADDR));
		dst->type = NETTYPE_IPV6;
		dst->port = htons(((struct sockaddr_in6*)src)->sin6_port);
		mem_copy(dst->ip, &((struct sockaddr_in6*)src)->sin6_addr.s6_addr, 16);
	}
	else
	{
		mem_zero(dst, sizeof(struct sockaddr));
		dbg_msg("system", "couldn't convert sockaddr of family %d", src->sa_family);
	}
}

int net_addr_comp(const NETADDR *a, const NETADDR *b)
{
	return mem_comp(a, b, sizeof(NETADDR));
}

bool NETADDR::operator==(const NETADDR &other) const
{
	return net_addr_comp(this, &other) == 0;
}

int net_addr_comp_noport(const NETADDR *a, const NETADDR *b)
{
	NETADDR ta = *a, tb = *b;
	ta.port = tb.port = 0;

	return net_addr_comp(&ta, &tb);
}

void net_addr_str(const NETADDR *addr, char *string, int max_length, int add_port)
{
	if(addr->type == NETTYPE_IPV4)
	{
		if(add_port != 0)
			str_format(string, max_length, "%d.%d.%d.%d:%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3], addr->port);
		else
			str_format(string, max_length, "%d.%d.%d.%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3]);
	}
	else if(addr->type == NETTYPE_IPV6)
	{
		if(add_port != 0)
			str_format(string, max_length, "[%x:%x:%x:%x:%x:%x:%x:%x]:%d",
				(addr->ip[0]<<8)|addr->ip[1], (addr->ip[2]<<8)|addr->ip[3], (addr->ip[4]<<8)|addr->ip[5], (addr->ip[6]<<8)|addr->ip[7],
				(addr->ip[8]<<8)|addr->ip[9], (addr->ip[10]<<8)|addr->ip[11], (addr->ip[12]<<8)|addr->ip[13], (addr->ip[14]<<8)|addr->ip[15],
				addr->port);
		else
			str_format(string, max_length, "[%x:%x:%x:%x:%x:%x:%x:%x]",
				(addr->ip[0]<<8)|addr->ip[1], (addr->ip[2]<<8)|addr->ip[3], (addr->ip[4]<<8)|addr->ip[5], (addr->ip[6]<<8)|addr->ip[7],
				(addr->ip[8]<<8)|addr->ip[9], (addr->ip[10]<<8)|addr->ip[11], (addr->ip[12]<<8)|addr->ip[13], (addr->ip[14]<<8)|addr->ip[15]);
	}
	else
		str_format(string, max_length, "unknown type %d", addr->type);
}

static int priv_net_extract(const char *hostname, char *host, int max_host, int *port)
{
	int i;

	*port = 0;
	host[0] = 0;

	if(hostname[0] == '[')
	{
		// ipv6 mode
		for(i = 1; i < max_host && hostname[i] && hostname[i] != ']'; i++)
			host[i-1] = hostname[i];
		host[i-1] = 0;
		if(hostname[i] != ']') // malformatted
			return -1;

		i++;
		if(hostname[i] == ':')
			*port = atol(hostname+i+1);
	}
	else
	{
		// generic mode (ipv4, hostname etc)
		for(i = 0; i < max_host-1 && hostname[i] && hostname[i] != ':'; i++)
			host[i] = hostname[i];
		host[i] = 0;

		if(hostname[i] == ':')
			*port = atol(hostname+i+1);
	}

	return 0;
}

int net_host_lookup(const char *hostname, NETADDR *addr, int types)
{
	struct addrinfo hints;
	struct addrinfo *result = NULL;
	int e;
	char host[256];
	int port = 0;

	if(priv_net_extract(hostname, host, sizeof(host), &port))
		return -1;

	dbg_msg("host lookup", "host='%s' port=%d %d", host, port, types);

	mem_zero(&hints, sizeof(hints));

	hints.ai_family = AF_UNSPEC;

	if(types == NETTYPE_IPV4)
		hints.ai_family = AF_INET;
	else if(types == NETTYPE_IPV6)
		hints.ai_family = AF_INET6;

	e = getaddrinfo(host, NULL, &hints, &result);

	if(!result)
		return -1;

	if(e != 0)
	{
		freeaddrinfo(result);
		return -1;
	}

	sockaddr_to_netaddr(result->ai_addr, addr);
	addr->port = port;
	freeaddrinfo(result);
	return 0;
}

static int parse_int(int *out, const char **str)
{
	int i = 0;
	*out = 0;
	if(**str < '0' || **str > '9')
		return -1;

	i = **str - '0';
	(*str)++;

	while(1)
	{
		if(**str < '0' || **str > '9')
		{
			*out = i;
			return 0;
		}

		i = (i*10) + (**str - '0');
		(*str)++;
	}

	return 0;
}

static int parse_char(char c, const char **str)
{
	if(**str != c) return -1;
	(*str)++;
	return 0;
}

static int parse_uint8(unsigned char *out, const char **str)
{
	int i;
	if(parse_int(&i, str) != 0) return -1;
	if(i < 0 || i > 0xff) return -1;
	*out = i;
	return 0;
}

static int parse_uint16(unsigned short *out, const char **str)
{
	int i;
	if(parse_int(&i, str) != 0) return -1;
	if(i < 0 || i > 0xffff) return -1;
	*out = i;
	return 0;
}

int net_addr_from_str(NETADDR *addr, const char *string)
{
	const char *str = string;
	mem_zero(addr, sizeof(NETADDR));

	if(str[0] == '[')
	{
		/* ipv6 */
		struct sockaddr_in6 sa6;
		char buf[128];
		int i;
		str++;
		for(i = 0; i < 127 && str[i] && str[i] != ']'; i++)
			buf[i] = str[i];
		buf[i] = 0;
		str += i;
#if defined(CONF_FAMILY_WINDOWS)
		{
			int size;
			sa6.sin6_family = AF_INET6;
			size = (int)sizeof(sa6);
			if(WSAStringToAddress(buf, AF_INET6, NULL, (struct sockaddr *)&sa6, &size) != 0)
				return -1;
		}
#else
		sa6.sin6_family = AF_INET6;

		if(inet_pton(AF_INET6, buf, &sa6.sin6_addr) != 1)
			return -1;
#endif
		sockaddr_to_netaddr((struct sockaddr *)&sa6, addr);

		if(*str == ']')
		{
			str++;
			if(*str == ':')
			{
				str++;
				if(parse_uint16(&addr->port, &str))
					return -1;
			}
		}
		else
			return -1;

		return 0;
	}
	else
	{
		/* ipv4 */
		if(parse_uint8(&addr->ip[0], &str)) return -1;
		if(parse_char('.', &str)) return -1;
		if(parse_uint8(&addr->ip[1], &str)) return -1;
		if(parse_char('.', &str)) return -1;
		if(parse_uint8(&addr->ip[2], &str)) return -1;
		if(parse_char('.', &str)) return -1;
		if(parse_uint8(&addr->ip[3], &str)) return -1;
		if(*str == ':')
		{
			str++;
			if(parse_uint16(&addr->port, &str)) return -1;
		}

		addr->type = NETTYPE_IPV4;
	}

	return 0;
}

static void priv_net_close_socket(int sock)
{
#if defined(CONF_FAMILY_WINDOWS)
	closesocket(sock);
#else
	close(sock);
#endif
}

static int priv_net_close_all_sockets(NETSOCKET sock)
{
	/* close down ipv4 */
	if(sock->ipv4sock >= 0)
	{
		priv_net_close_socket(sock->ipv4sock);
		sock->ipv4sock = -1;
		sock->type &= ~NETTYPE_IPV4;
	}

#if defined(CONF_WEBSOCKETS)
	/* close down websocket_ipv4 */
	if(sock->web_ipv4sock >= 0)
	{
		websocket_destroy(sock->web_ipv4sock);
		sock->web_ipv4sock = -1;
		sock->type &= ~NETTYPE_WEBSOCKET_IPV4;
	}
#endif

	/* close down ipv6 */
	if(sock->ipv6sock >= 0)
	{
		priv_net_close_socket(sock->ipv6sock);
		sock->ipv6sock = -1;
		sock->type &= ~NETTYPE_IPV6;
	}

	free(sock);
	return 0;
}

static int priv_net_create_socket(int domain, int type, struct sockaddr *addr, int sockaddrlen)
{
	int sock, e;

	/* create socket */
	sock = socket(domain, type, 0);
	if(sock < 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		int error = WSAGetLastError();
		char *message = windows_format_system_message(error);
		dbg_msg("net", "failed to create socket with domain %d and type %d (%d '%s')", domain, type, error, message == nullptr ? "unknown error" : message);
		free(message);
#else
		dbg_msg("net", "failed to create socket with domain %d and type %d (%d '%s')", domain, type, errno, strerror(errno));
#endif
		return -1;
	}

#if defined(CONF_FAMILY_UNIX)
	/* on tcp sockets set SO_REUSEADDR
		to fix port rebind on restart */
	if(domain == AF_INET && type == SOCK_STREAM)
	{
		int option = 1;
		if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) != 0)
			dbg_msg("socket", "Setting SO_REUSEADDR failed: %d", errno);
	}
#endif

	/* set to IPv6 only if that's what we are creating */
#if defined(IPV6_V6ONLY) /* windows sdk 6.1 and higher */
	if(domain == AF_INET6)
	{
		int ipv6only = 1;
		if(setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&ipv6only, sizeof(ipv6only)) != 0)
			dbg_msg("socket", "Setting V6ONLY failed: %d", errno);
	}
#endif

	/* bind the socket */
	e = bind(sock, addr, sockaddrlen);
	if(e != 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		int error = WSAGetLastError();
		char *message = windows_format_system_message(error);
		dbg_msg("net", "failed to bind socket with domain %d and type %d (%d '%s')", domain, type, error, message == nullptr ? "unknown error" : message);
		free(message);
#else
		dbg_msg("net", "failed to bind socket with domain %d and type %d (%d '%s')", domain, type, errno, strerror(errno));
#endif
		priv_net_close_socket(sock);
		return -1;
	}

	/* return the newly created socket */
	return sock;
}

int net_socket_type(NETSOCKET sock)
{
	return sock->type;
}

void net_buffer_init(NETSOCKET_BUFFER *buffer)
{
#if defined(CONF_PLATFORM_LINUX)
	int i;
	buffer->pos = 0;
	buffer->size = 0;
	mem_zero(buffer->msgs, sizeof(buffer->msgs));
	mem_zero(buffer->iovecs, sizeof(buffer->iovecs));
	mem_zero(buffer->sockaddrs, sizeof(buffer->sockaddrs));
	for(i = 0; i < VLEN; ++i)
	{
		buffer->iovecs[i].iov_base = buffer->bufs[i];
		buffer->iovecs[i].iov_len = PACKETSIZE;
		buffer->msgs[i].msg_hdr.msg_iov = &(buffer->iovecs[i]);
		buffer->msgs[i].msg_hdr.msg_iovlen = 1;
		buffer->msgs[i].msg_hdr.msg_name = &(buffer->sockaddrs[i]);
		buffer->msgs[i].msg_hdr.msg_namelen = sizeof(buffer->sockaddrs[i]);
	}
#endif
}

void net_buffer_reinit(NETSOCKET_BUFFER *buffer)
{
#if defined(CONF_PLATFORM_LINUX)
	for(int i = 0; i < VLEN; i++)
	{
		buffer->msgs[i].msg_hdr.msg_namelen = sizeof(buffer->sockaddrs[i]);
	}
#endif
}

void net_buffer_simple(NETSOCKET_BUFFER *buffer, char **buf, int *size)
{
#if defined(CONF_PLATFORM_LINUX)
	*buf = buffer->bufs[0];
	*size = sizeof(buffer->bufs[0]);
#else
	*buf = buffer->buf;
	*size = sizeof(buffer->buf);
#endif
}

NETSOCKET net_udp_create(NETADDR bindaddr)
{
	NETSOCKET sock = (NETSOCKET_INTERNAL *)malloc(sizeof(*sock));
	*sock = invalid_socket;
	NETADDR tmpbindaddr = bindaddr;
	int broadcast = 1;
	int socket = -1;

	if(bindaddr.type & NETTYPE_IPV4)
	{
		struct sockaddr_in addr;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV4;
		netaddr_to_sockaddr_in(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET, SOCK_DGRAM, (struct sockaddr *)&addr, sizeof(addr));
		if(socket >= 0)
		{
			sock->type |= NETTYPE_IPV4;
			sock->ipv4sock = socket;

			/* set broadcast */
			if(setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast)) != 0)
				dbg_msg("socket", "Setting BROADCAST on ipv4 failed: %d", errno);

			{
				/* set DSCP/TOS */
				int iptos = 0x10 /* IPTOS_LOWDELAY */;
				//int iptos = 46; /* High Priority */
				if(setsockopt(socket, IPPROTO_IP, IP_TOS, (char *)&iptos, sizeof(iptos)) != 0)
					dbg_msg("socket", "Setting TOS on ipv4 failed: %d", errno);
			}
		}
	}
#if defined(CONF_WEBSOCKETS)
	if(bindaddr.type & NETTYPE_WEBSOCKET_IPV4)
	{
		char addr_str[NETADDR_MAXSTRSIZE];

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_WEBSOCKET_IPV4;

		net_addr_str(&tmpbindaddr, addr_str, sizeof(addr_str), 0);
		socket = websocket_create(addr_str, tmpbindaddr.port);

		if(socket >= 0)
		{
			sock->type |= NETTYPE_WEBSOCKET_IPV4;
			sock->web_ipv4sock = socket;
		}
	}
#endif

	if(bindaddr.type & NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV6;
		netaddr_to_sockaddr_in6(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET6, SOCK_DGRAM, (struct sockaddr *)&addr, sizeof(addr));
		if(socket >= 0)
		{
			sock->type |= NETTYPE_IPV6;
			sock->ipv6sock = socket;

			/* set broadcast */
			if(setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast)) != 0)
				dbg_msg("socket", "Setting BROADCAST on ipv6 failed: %d", errno);

			{
				/* set DSCP/TOS */
				int iptos = 0x10 /* IPTOS_LOWDELAY */;
				//int iptos = 46; /* High Priority */
				if(setsockopt(socket, IPPROTO_IP, IP_TOS, (char *)&iptos, sizeof(iptos)) != 0)
					dbg_msg("socket", "Setting TOS on ipv6 failed: %d", errno);
			}
		}
	}

	if(socket < 0)
	{
		free(sock);
		sock = nullptr;
	}
	else
	{
		/* set non-blocking */
		net_set_non_blocking(sock);

		net_buffer_init(&sock->buffer);
	}

	/* return */
	return sock;
}

int net_udp_send(NETSOCKET sock, const NETADDR *addr, const void *data, int size)
{
	int d = -1;

	if(addr->type & NETTYPE_IPV4)
	{
		if(sock->ipv4sock >= 0)
		{
			struct sockaddr_in sa;
			if(addr->type & NETTYPE_LINK_BROADCAST)
			{
				mem_zero(&sa, sizeof(sa));
				sa.sin_port = htons(addr->port);
				sa.sin_family = AF_INET;
				sa.sin_addr.s_addr = INADDR_BROADCAST;
			}
			else
				netaddr_to_sockaddr_in(addr, &sa);

			d = sendto((int)sock->ipv4sock, (const char *)data, size, 0, (struct sockaddr *)&sa, sizeof(sa));
		}
		else
			dbg_msg("net", "can't send ipv4 traffic to this socket");
	}

#if defined(CONF_WEBSOCKETS)
	if(addr->type & NETTYPE_WEBSOCKET_IPV4)
	{
		if(sock->web_ipv4sock >= 0)
		{
			char addr_str[NETADDR_MAXSTRSIZE];
			str_format(addr_str, sizeof(addr_str), "%d.%d.%d.%d", addr->ip[0], addr->ip[1], addr->ip[2], addr->ip[3]);
			d = websocket_send(sock->web_ipv4sock, (const unsigned char *)data, size, addr_str, addr->port);
		}

		else
			dbg_msg("net", "can't send websocket_ipv4 traffic to this socket");
	}
#endif

	if(addr->type & NETTYPE_IPV6)
	{
		if(sock->ipv6sock >= 0)
		{
			struct sockaddr_in6 sa;
			if(addr->type & NETTYPE_LINK_BROADCAST)
			{
				mem_zero(&sa, sizeof(sa));
				sa.sin6_port = htons(addr->port);
				sa.sin6_family = AF_INET6;
				sa.sin6_addr.s6_addr[0] = 0xff; /* multicast */
				sa.sin6_addr.s6_addr[1] = 0x02; /* link local scope */
				sa.sin6_addr.s6_addr[15] = 1; /* all nodes */
			}
			else
				netaddr_to_sockaddr_in6(addr, &sa);

			d = sendto((int)sock->ipv6sock, (const char *)data, size, 0, (struct sockaddr *)&sa, sizeof(sa));
		}
		else
			dbg_msg("net", "can't send ipv6 traffic to this socket");
	}
	/*
	else
		dbg_msg("net", "can't send to network of type %d", addr->type);
		*/

	/*if(d < 0)
	{
		char addrstr[256];
		net_addr_str(addr, addrstr, sizeof(addrstr));

		dbg_msg("net", "sendto error (%d '%s')", errno, strerror(errno));
		dbg_msg("net", "\tsock = %d %x", sock, sock);
		dbg_msg("net", "\tsize = %d %x", size, size);
		dbg_msg("net", "\taddr = %s", addrstr);

	}*/
	network_stats.sent_bytes += size;
	network_stats.sent_packets++;
	return d;
}

int net_udp_recv(NETSOCKET sock, NETADDR *addr, unsigned char **data)
{
	char sockaddrbuf[128];
	int bytes = 0;

#if defined(CONF_PLATFORM_LINUX)
	if(sock->ipv4sock >= 0)
	{
		if(sock->buffer.pos >= sock->buffer.size)
		{
			net_buffer_reinit(&sock->buffer);
			sock->buffer.size = recvmmsg(sock->ipv4sock, sock->buffer.msgs, VLEN, 0, NULL);
			sock->buffer.pos = 0;
		}
	}

	if(sock->ipv6sock >= 0)
	{
		if(sock->buffer.pos >= sock->buffer.size)
		{
			net_buffer_reinit(&sock->buffer);
			sock->buffer.size = recvmmsg(sock->ipv6sock, sock->buffer.msgs, VLEN, 0, NULL);
			sock->buffer.pos = 0;
		}
	}

	if(sock->buffer.pos < sock->buffer.size)
	{
		sockaddr_to_netaddr((struct sockaddr *)&(sock->buffer.sockaddrs[sock->buffer.pos]), addr);
		bytes = sock->buffer.msgs[sock->buffer.pos].msg_len;
		*data = (unsigned char *)sock->buffer.bufs[sock->buffer.pos];
		sock->buffer.pos++;
		network_stats.recv_bytes += bytes;
		network_stats.recv_packets++;
		return bytes;
	}
#else
	if(sock->ipv4sock >= 0)
	{
		socklen_t fromlen = sizeof(struct sockaddr_in);
		bytes = recvfrom(sock->ipv4sock, sock->buffer.buf, sizeof(sock->buffer.buf), 0, (struct sockaddr *)&sockaddrbuf, &fromlen);
		*data = (unsigned char *)sock->buffer.buf;
	}

	if(bytes <= 0 && sock->ipv6sock >= 0)
	{
		socklen_t fromlen = sizeof(struct sockaddr_in6);
		bytes = recvfrom(sock->ipv6sock, sock->buffer.buf, sizeof(sock->buffer.buf), 0, (struct sockaddr *)&sockaddrbuf, &fromlen);
		*data = (unsigned char *)sock->buffer.buf;
	}
#endif

#if defined(CONF_WEBSOCKETS)
	if(bytes <= 0 && sock->web_ipv4sock >= 0)
	{
		char *buf;
		int size;
		net_buffer_simple(&sock->buffer, &buf, &size);
		socklen_t fromlen = sizeof(struct sockaddr);
		struct sockaddr_in *sockaddrbuf_in = (struct sockaddr_in *)&sockaddrbuf;
		bytes = websocket_recv(sock->web_ipv4sock, (unsigned char *)buf, size, sockaddrbuf_in, fromlen);
		*data = (unsigned char *)buf;
		sockaddrbuf_in->sin_family = AF_WEBSOCKET_INET;
	}
#endif

	if(bytes > 0)
	{
		sockaddr_to_netaddr((struct sockaddr *)&sockaddrbuf, addr);
		network_stats.recv_bytes += bytes;
		network_stats.recv_packets++;
		return bytes;
	}
	else if(bytes == 0)
		return 0;
	return -1; /* error */
}

int net_udp_close(NETSOCKET sock)
{
	return priv_net_close_all_sockets(sock);
}

NETSOCKET net_tcp_create(NETADDR bindaddr)
{
	NETSOCKET sock = (NETSOCKET_INTERNAL *)malloc(sizeof(*sock));
	*sock = invalid_socket;
	NETADDR tmpbindaddr = bindaddr;
	int socket = -1;

	if(bindaddr.type & NETTYPE_IPV4)
	{
		struct sockaddr_in addr;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV4;
		netaddr_to_sockaddr_in(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET, SOCK_STREAM, (struct sockaddr *)&addr, sizeof(addr));
		if(socket >= 0)
		{
			sock->type |= NETTYPE_IPV4;
			sock->ipv4sock = socket;
		}
	}

	if(bindaddr.type & NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;

		/* bind, we should check for error */
		tmpbindaddr.type = NETTYPE_IPV6;
		netaddr_to_sockaddr_in6(&tmpbindaddr, &addr);
		socket = priv_net_create_socket(AF_INET6, SOCK_STREAM, (struct sockaddr *)&addr, sizeof(addr));
		if(socket >= 0)
		{
			sock->type |= NETTYPE_IPV6;
			sock->ipv6sock = socket;
		}
	}

	if(socket < 0)
	{
		free(sock);
		sock = nullptr;
	}

	/* return */
	return sock;
}

int net_set_non_blocking(NETSOCKET sock)
{
	unsigned long mode = 1;
	if(sock->ipv4sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock->ipv4sock, FIONBIO, (unsigned long *)&mode);
#else
		if(ioctl(sock->ipv4sock, FIONBIO, (unsigned long *)&mode) == -1)
			dbg_msg("socket", "setting ipv4 non-blocking failed: %d", errno);
#endif
	}

	if(sock->ipv6sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock->ipv6sock, FIONBIO, (unsigned long *)&mode);
#else
		if(ioctl(sock->ipv6sock, FIONBIO, (unsigned long *)&mode) == -1)
			dbg_msg("socket", "setting ipv6 non-blocking failed: %d", errno);
#endif
	}

	return 0;
}

int net_set_blocking(NETSOCKET sock)
{
	unsigned long mode = 0;
	if(sock->ipv4sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock->ipv4sock, FIONBIO, (unsigned long *)&mode);
#else
		if(ioctl(sock->ipv4sock, FIONBIO, (unsigned long *)&mode) == -1)
			dbg_msg("socket", "setting ipv4 blocking failed: %d", errno);
#endif
	}

	if(sock->ipv6sock >= 0)
	{
#if defined(CONF_FAMILY_WINDOWS)
		ioctlsocket(sock->ipv6sock, FIONBIO, (unsigned long *)&mode);
#else
		if(ioctl(sock->ipv6sock, FIONBIO, (unsigned long *)&mode) == -1)
			dbg_msg("socket", "setting ipv6 blocking failed: %d", errno);
#endif
	}

	return 0;
}

int net_tcp_listen(NETSOCKET sock, int backlog)
{
	int err = -1;
	if(sock->ipv4sock >= 0)
		err = listen(sock->ipv4sock, backlog);
	if(sock->ipv6sock >= 0)
		err = listen(sock->ipv6sock, backlog);
	return err;
}

int net_tcp_accept(NETSOCKET sock, NETSOCKET *new_sock, NETADDR *a)
{
	int s;
	socklen_t sockaddr_len;

	*new_sock = nullptr;

	if(sock->ipv4sock >= 0)
	{
		struct sockaddr_in addr;
		sockaddr_len = sizeof(addr);

		s = accept(sock->ipv4sock, (struct sockaddr *)&addr, &sockaddr_len);

		if(s != -1)
		{
			sockaddr_to_netaddr((const struct sockaddr *)&addr, a);

			*new_sock = (NETSOCKET_INTERNAL *)malloc(sizeof(**new_sock));
			**new_sock = invalid_socket;
			(*new_sock)->type = NETTYPE_IPV4;
			(*new_sock)->ipv4sock = s;
			return s;
		}
	}

	if(sock->ipv6sock >= 0)
	{
		struct sockaddr_in6 addr;
		sockaddr_len = sizeof(addr);

		s = accept(sock->ipv6sock, (struct sockaddr *)&addr, &sockaddr_len);

		if(s != -1)
		{
			*new_sock = (NETSOCKET_INTERNAL *)malloc(sizeof(**new_sock));
			**new_sock = invalid_socket;
			sockaddr_to_netaddr((const struct sockaddr *)&addr, a);
			(*new_sock)->type = NETTYPE_IPV6;
			(*new_sock)->ipv6sock = s;
			return s;
		}
	}

	return -1;
}

int net_tcp_connect(NETSOCKET sock, const NETADDR *a)
{
	if(a->type & NETTYPE_IPV4)
	{
		struct sockaddr_in addr;
		netaddr_to_sockaddr_in(a, &addr);
		return connect(sock->ipv4sock, (struct sockaddr *)&addr, sizeof(addr));
	}

	if(a->type & NETTYPE_IPV6)
	{
		struct sockaddr_in6 addr;
		netaddr_to_sockaddr_in6(a, &addr);
		return connect(sock->ipv6sock, (struct sockaddr *)&addr, sizeof(addr));
	}

	return -1;
}

int net_tcp_connect_non_blocking(NETSOCKET sock, NETADDR bindaddr)
{
	int res = 0;

	net_set_non_blocking(sock);
	res = net_tcp_connect(sock, &bindaddr);
	net_set_blocking(sock);

	return res;
}

int net_tcp_send(NETSOCKET sock, const void *data, int size)
{
	int bytes = -1;

	if(sock->ipv4sock >= 0)
		bytes = send((int)sock->ipv4sock, (const char *)data, size, 0);
	if(sock->ipv6sock >= 0)
		bytes = send((int)sock->ipv6sock, (const char *)data, size, 0);

	return bytes;
}

int net_tcp_recv(NETSOCKET sock, void *data, int maxsize)
{
	int bytes = -1;

	if(sock->ipv4sock >= 0)
		bytes = recv((int)sock->ipv4sock, (char *)data, maxsize, 0);
	if(sock->ipv6sock >= 0)
		bytes = recv((int)sock->ipv6sock, (char *)data, maxsize, 0);

	return bytes;
}

int net_tcp_close(NETSOCKET sock)
{
	return priv_net_close_all_sockets(sock);
}

int net_errno()
{
#if defined(CONF_FAMILY_WINDOWS)
	return WSAGetLastError();
#else
	return errno;
#endif
}

int net_would_block()
{
#if defined(CONF_FAMILY_WINDOWS)
	return net_errno() == WSAEWOULDBLOCK;
#else
	return net_errno() == EWOULDBLOCK;
#endif
}

int net_init()
{
#if defined(CONF_FAMILY_WINDOWS)
	WSADATA wsaData;
	int err = WSAStartup(MAKEWORD(1, 1), &wsaData);
	dbg_assert(err == 0, "network initialization failed.");
	return err==0?0:1;
#endif

	return 0;
}

int fs_listdir_info(const char *dir, FS_LISTDIR_INFO_CALLBACK cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024*2];
	int length;
	str_format(buffer, sizeof(buffer), "%s/*", dir);

	handle = FindFirstFileA(buffer, &finddata);

	if (handle == INVALID_HANDLE_VALUE)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	/* add all the entries */
	do
	{
		str_copy(buffer+length, finddata.cFileName, (int)sizeof(buffer)-length);
		if(cb(finddata.cFileName, fs_getmtime(buffer), fs_is_dir(buffer), type, user))
			break;
	}
	while (FindNextFileA(handle, &finddata));

	FindClose(handle);
	return 0;
#else
	struct dirent *entry;
	char buffer[1024*2];
	int length;
	DIR *d = opendir(dir);

	if(!d)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	while((entry = readdir(d)) != NULL)
	{
		str_copy(buffer+length, entry->d_name, (int)sizeof(buffer)-length);
		if(cb(entry->d_name, fs_getmtime(buffer), fs_is_dir(buffer), type, user))
			break;
	}

	/* close the directory and return */
	closedir(d);
	return 0;
#endif
}

int fs_listdir(const char *dir, FS_LISTDIR_CALLBACK cb, int type, void *user)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024*2];
	int length;
	str_format(buffer, sizeof(buffer), "%s/*", dir);

	handle = FindFirstFileA(buffer, &finddata);

	if (handle == INVALID_HANDLE_VALUE)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	/* add all the entries */
	do
	{
		str_copy(buffer+length, finddata.cFileName, (int)sizeof(buffer)-length);
		if(cb(finddata.cFileName, fs_is_dir(buffer), type, user))
			break;
	}
	while (FindNextFileA(handle, &finddata));

	FindClose(handle);
	return 0;
#else
	struct dirent *entry;
	char buffer[1024*2];
	int length;
	DIR *d = opendir(dir);

	if(!d)
		return 0;

	str_format(buffer, sizeof(buffer), "%s/", dir);
	length = str_length(buffer);

	while((entry = readdir(d)) != NULL)
	{
		str_copy(buffer+length, entry->d_name, (int)sizeof(buffer)-length);
		if(cb(entry->d_name, fs_is_dir(buffer), type, user))
			break;
	}

	/* close the directory and return */
	closedir(d);
	return 0;
#endif
}

int fs_storage_path(const char *appname, char *path, int max)
{
#if defined(CONF_FAMILY_WINDOWS)
	char *home = getenv("APPDATA");
	if(!home)
		return -1;
	_snprintf(path, max, "%s/%s", home, appname);
	return 0;
#else
	char *home = getenv("HOME");
#if !defined(CONF_PLATFORM_MACOSX)
	int i;
#endif
	if(!home)
		return -1;

#if defined(CONF_PLATFORM_MACOSX)
	snprintf(path, max, "%s/Library/Application Support/%s", home, appname);
#else
	snprintf(path, max, "%s/.%s", home, appname);
	for(i = strlen(home)+2; path[i]; i++)
		path[i] = tolower(path[i]);
#endif

	return 0;
#endif
}

int fs_makedir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(_mkdir(path) == 0)
			return 0;
	if(errno == EEXIST)
		return 0;
	return -1;
#else
	if(mkdir(path, 0755) == 0)
		return 0;
	if(errno == EEXIST)
		return 0;
	return -1;
#endif
}

int fs_is_dir(const char *path)
{
#if defined(CONF_FAMILY_WINDOWS)
	/* TODO: do this smarter */
	WIN32_FIND_DATA finddata;
	HANDLE handle;
	char buffer[1024*2];
	str_format(buffer, sizeof(buffer), "%s/*", path);

	if ((handle = FindFirstFileA(buffer, &finddata)) == INVALID_HANDLE_VALUE)
		return 0;

	FindClose(handle);
	return 1;
#else
	struct stat sb;
	if (stat(path, &sb) == -1)
		return 0;

	if (S_ISDIR(sb.st_mode))
		return 1;
	else
		return 0;
#endif
}

time_t fs_getmtime(const char *path)
{
	struct stat sb;
	if (stat(path, &sb) == -1)
		return 0;

	return sb.st_mtime;
}

int fs_chdir(const char *path)
{
	if(fs_is_dir(path))
	{
		if(chdir(path))
			return 1;
		else
			return 0;
	}
	else
		return 1;
}

char *fs_getcwd(char *buffer, int buffer_size)
{
	if(buffer == 0)
		return 0;
#if defined(CONF_FAMILY_WINDOWS)
	return _getcwd(buffer, buffer_size);
#else
	return getcwd(buffer, buffer_size);
#endif
}

int fs_parent_dir(char *path)
{
	char *parent = 0;
	for(; *path; ++path)
	{
		if(*path == '/' || *path == '\\')
			parent = path;
	}

	if(parent)
	{
		*parent = 0;
		return 0;
	}
	return 1;
}

int fs_remove(const char *filename)
{
	if(remove(filename) != 0)
		return 1;
	return 0;
}

int fs_rename(const char *oldname, const char *newname)
{
#if defined(CONF_FAMILY_WINDOWS)
	if(MoveFileEx(oldname, newname, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0)
		return 1;
#else
	if(rename(oldname, newname) != 0)
		return 1;
#endif
	return 0;
}

int fs_file_time(const char *name, time_t *created, time_t *modified)
{
#if defined(CONF_FAMILY_WINDOWS)
	WIN32_FIND_DATAW finddata;
	HANDLE handle;
	WCHAR wBuffer[IO_MAX_PATH_LENGTH];

	MultiByteToWideChar(CP_UTF8, 0, name, -1, wBuffer, sizeof(wBuffer) / sizeof(WCHAR));
	handle = FindFirstFileW(wBuffer, &finddata);
	if(handle == INVALID_HANDLE_VALUE)
		return 1;

	*created = filetime_to_unixtime(&finddata.ftCreationTime);
	*modified = filetime_to_unixtime(&finddata.ftLastWriteTime);
	FindClose(handle);
#elif defined(CONF_FAMILY_UNIX)
	struct stat sb;
	if(stat(name, &sb))
		return 1;

	*created = sb.st_ctime;
	*modified = sb.st_mtime;
#else
#error not implemented
#endif

	return 0;
}

int fs_makedir_recursive(const char *path)
{
	char buffer[2048];
	int len;
	int i;
	str_copy(buffer, path, sizeof(buffer));
	len = str_length(buffer);
	// ignore a leading slash
	for(i = 1; i < len; i++)
	{
		char b = buffer[i];
		if((buffer[i] == '/' || buffer[i] == '\\') && buffer[i + 1] != '\0' && buffer[i - 1] != ':')
		{
			buffer[i] = '\0';
			if(fs_makedir(buffer) < 0)
			{
				return -1;
			}
			buffer[i] = b;
		}
	}
	return fs_makedir(path);
}

void swap_endian(void *data, unsigned elem_size, unsigned num)
{
	char *src = (char*) data;
	char *dst = src + (elem_size - 1);

	while(num)
	{
		unsigned n = elem_size>>1;
		char tmp;
		while(n)
		{
			tmp = *src;
			*src = *dst;
			*dst = tmp;

			src++;
			dst--;
			n--;
		}

		src = src + (elem_size>>1);
		dst = src + (elem_size - 1);
		num--;
	}
}

int net_socket_read_wait(NETSOCKET sock, int time)
{
	struct timeval tv;
	fd_set readfds;
	int sockid;

	tv.tv_sec = time / 1000000;
	tv.tv_usec = time % 1000000;
	sockid = 0;

	FD_ZERO(&readfds); // NOLINT(clang-analyzer-security.insecureAPI.bzero)
	if(sock->ipv4sock >= 0)
	{
		FD_SET(sock->ipv4sock, &readfds);
		sockid = sock->ipv4sock;
	}
	if(sock->ipv6sock >= 0)
	{
		FD_SET(sock->ipv6sock, &readfds);
		if(sock->ipv6sock > sockid)
			sockid = sock->ipv6sock;
	}

	/* don't care about writefds and exceptfds */
	if(time < 0)
		select(sockid + 1, &readfds, NULL, NULL, NULL);
	else
		select(sockid + 1, &readfds, NULL, NULL, &tv);

	if(sock->ipv4sock >= 0 && FD_ISSET(sock->ipv4sock, &readfds))
		return 1;
	if(sock->ipv6sock >= 0 && FD_ISSET(sock->ipv6sock, &readfds))
		return 1;

	return 0;
}

int time_timestamp()
{
	return time(0);
}

void str_append(char *dst, const char *src, int dst_size)
{
	int s = strlen(dst);
	int i = 0;
	while(s < dst_size)
	{
		dst[s] = src[i];
		if(!src[i]) /* check for null termination */
			break;
		s++;
		i++;
	}

	dst[dst_size-1] = 0; /* assure null termination */
}

void str_copy(char *dst, const char *src, int dst_size)
{
	strncpy(dst, src, dst_size);
	dst[dst_size-1] = 0; /* assure null termination */
}

void str_truncate(char *dst, int dst_size, const char *src, int truncation_len)
{
	int size = dst_size;
	if(truncation_len < size)
	{
		size = truncation_len + 1;
	}
	str_copy(dst, src, size);
}

int str_length(const char *str)
{
	return (int)strlen(str);
}

int str_format(char *buffer, int buffer_size, const char *format, ...)
{
	int ret;
#if defined(CONF_FAMILY_WINDOWS)
	va_list ap;
	va_start(ap, format);
	ret = _vsnprintf(buffer, buffer_size, format, ap);
	va_end(ap);
#else
	va_list ap;
	va_start(ap, format);
	ret = vsnprintf(buffer, buffer_size, format, ap);
	va_end(ap);
#endif

	buffer[buffer_size-1] = 0; /* assure null termination */
	return ret;
}

char *str_trim_words(char *str, int words)
{
	while (words && *str)
	{
		if (isspace(*str) && !isspace(*(str + 1)))
			words--;
		str++;
	}
	return str;
}

/* makes sure that the string only contains the characters between 32 and 127 */
void str_sanitize_strong(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		*str &= 0x7f;
		if(*str < 32)
			*str = 32;
		str++;
	}
}

/* makes sure that the string only contains the characters between 32 and 255 */
void str_sanitize_cc(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		if(*str < 32)
			*str = ' ';
		str++;
	}
}

/* makes sure that the string only contains the characters between 32 and 255 + \r\n\t */
void str_sanitize(char *str_in)
{
	unsigned char *str = (unsigned char *)str_in;
	while(*str)
	{
		if(*str < 32 && !(*str == '\r') && !(*str == '\n') && !(*str == '\t'))
			*str = ' ';
		str++;
	}
}

char *str_skip_to_whitespace(char *str)
{
	while(*str && (*str != ' ' && *str != '\t' && *str != '\n'))
		str++;
	return str;
}

char *str_skip_whitespaces(char *str)
{
	while(*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r'))
		str++;
	return str;
}

/* case */
int str_comp_nocase(const char *a, const char *b)
{
#if defined(CONF_FAMILY_WINDOWS)
	return _stricmp(a,b);
#else
	return strcasecmp(a,b);
#endif
}

int str_comp_nocase_num(const char *a, const char *b, const int num)
{
#if defined(CONF_FAMILY_WINDOWS)
	return _strnicmp(a, b, num);
#else
	return strncasecmp(a, b, num);
#endif
}

int str_comp(const char *a, const char *b)
{
	return strcmp(a, b);
}

int str_comp_num(const char *a, const char *b, const int num)
{
	return strncmp(a, b, num);
}

int str_comp_filenames(const char *a, const char *b)
{
	int result;

	for(; *a && *b; ++a, ++b)
	{
		if(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9')
		{
			result = 0;
			do
			{
				if(!result)
					result = *a - *b;
				++a; ++b;
			}
			while(*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9');

			if(*a >= '0' && *a <= '9')
				return 1;
			else if(*b >= '0' && *b <= '9')
				return -1;
			else if(result)
				return result;
		}

		if(*a != *b)
			break;
	}
	return *a - *b;
}

const char *str_find_nocase(const char *haystack, const char *needle)
{
	while(*haystack) /* native implementation */
	{
		const char *a = haystack;
		const char *b = needle;
		while(*a && *b && tolower(*a) == tolower(*b))
		{
			a++;
			b++;
		}
		if(!(*b))
			return haystack;
		haystack++;
	}

	return 0;
}


const char *str_find(const char *haystack, const char *needle)
{
	while(*haystack) /* native implementation */
	{
		const char *a = haystack;
		const char *b = needle;
		while(*a && *b && *a == *b)
		{
			a++;
			b++;
		}
		if(!(*b))
			return haystack;
		haystack++;
	}

	return 0;
}

const char *str_startswith_nocase(const char *str, const char *prefix)
{
	int prefixl = str_length(prefix);
	if(str_comp_nocase_num(str, prefix, prefixl) == 0)
	{
		return str + prefixl;
	}
	else
	{
		return 0;
	}
}

const char *str_startswith(const char *str, const char *prefix)
{
	int prefixl = str_length(prefix);
	if(str_comp_num(str, prefix, prefixl) == 0)
	{
		return str + prefixl;
	}
	else
	{
		return 0;
	}
}


static int hexval(char x)
{
	switch(x)
	{
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'a':
	case 'A': return 10;
	case 'b':
	case 'B': return 11;
	case 'c':
	case 'C': return 12;
	case 'd':
	case 'D': return 13;
	case 'e':
	case 'E': return 14;
	case 'f':
	case 'F': return 15;
	default: return -1;
	}
}

static int byteval(const char *hex, unsigned char *dst)
{
	int v1 = hexval(hex[0]);
	int v2 = hexval(hex[1]);

	if(v1 < 0 || v2 < 0)
		return 1;

	*dst = v1 * 16 + v2;
	return 0;
}

int str_hex_decode(void *dst, int dst_size, const char *src)
{
	unsigned char *cdst = (unsigned char *)dst;
	int slen = str_length(src);
	int len = slen / 2;
	int i;
	if(slen != dst_size * 2)
		return 2;

	for(i = 0; i < len && dst_size; i++, dst_size--)
	{
		if(byteval(src + i * 2, cdst++))
			return 1;
	}
	return 0;
}

void str_hex(char *dst, int dst_size, const void *data, int data_size)
{
	static const char hex[] = "0123456789ABCDEF";
	int b;

	for(b = 0; b < data_size && b < dst_size/4-4; b++)
	{
		dst[b*3] = hex[((const unsigned char *)data)[b]>>4];
		dst[b*3+1] = hex[((const unsigned char *)data)[b]&0xf];
		dst[b*3+2] = ' ';
		dst[b*3+3] = 0;
	}
}

void str_timestamp_ex(time_t time_data, char *buffer, int buffer_size, const char *format)
{
	struct tm *time_info;
	dbg_assert(buffer_size > 0, "buffer_size invalid");
	time_info = localtime(&time_data);
	strftime(buffer, buffer_size, format, time_info);
	buffer[buffer_size-1] = 0;	/* assure null termination */
}

void str_timestamp(char *buffer, int buffer_size)
{
	time_t time_data;
	time(&time_data);
	str_timestamp_ex(time_data, buffer, buffer_size, "%Y-%m-%d_%H-%M-%S");
}

int mem_comp(const void *a, const void *b, int size)
{
	return memcmp(a,b,size);
}

const MEMSTATS *mem_stats()
{
	return &memory_stats;
}

void net_stats(NETSTATS *stats_inout)
{
	*stats_inout = network_stats;
}

void gui_messagebox(const char *title, const char *message)
{
#if defined(CONF_PLATFORM_MACOSX)
	DialogRef theItem;
	DialogItemIndex itemIndex;

	/* FIXME: really needed? can we rely on glfw? */
	/* HACK - get events without a bundle */
	ProcessSerialNumber psn;
	GetCurrentProcess(&psn);
	TransformProcessType(&psn,kProcessTransformToForegroundApplication);
	SetFrontProcess(&psn);
	/* END HACK */

	CreateStandardAlert(kAlertStopAlert,
			CFStringCreateWithCString(NULL, title, kCFStringEncodingASCII),
			CFStringCreateWithCString(NULL, message, kCFStringEncodingASCII),
			NULL,
			&theItem);

	RunStandardAlert(theItem, NULL, &itemIndex);
#elif defined(CONF_FAMILY_UNIX)
	static char cmd[1024];
	int err;
	/* use xmessage which is available on nearly every X11 system */
	snprintf(cmd, sizeof(cmd), "xmessage -center -title '%s' '%s'",
		title,
		message);

	err = system(cmd);
	dbg_msg("gui/msgbox", "result = %i", err);
#elif defined(CONF_FAMILY_WINDOWS)
	MessageBox(NULL,
		message,
		title,
		MB_ICONEXCLAMATION | MB_OK);
#else
	/* this is not critical */
	#warning not implemented
#endif
}

int str_isspace(char c) { return c == ' ' || c == '\n' || c == '\t'; }

char str_uppercase(char c)
{
	if(c >= 'a' && c <= 'z')
		return 'A' + (c-'a');
	return c;
}

int str_toint(const char *str) { return atoi(str); }
int str_toint_base(const char *str, int base) { return strtol(str, NULL, base); }
float str_tofloat(const char *str) { return atof(str); }


int str_utf8_comp_names(const char *a, const char *b)
{
	int codeA;
	int codeB;
	int diff;

	while(*a && *b)
	{
		do
		{
			codeA = str_utf8_decode(&a);
		}
		while(*a && !str_utf8_isspace(codeA));

		do
		{
			codeB = str_utf8_decode(&b);
		}
		while(*b && !str_utf8_isspace(codeB));

		diff = codeA - codeB;

		if((diff < 0 && !str_utf8_is_confusable(codeA, codeB))
		|| (diff > 0 && !str_utf8_is_confusable(codeB, codeA)))
			return diff;
	}

	return *a - *b;
}

int str_utf8_isspace(int code)
{
	return code > 0x20 && code != 0xA0 && code != 0x034F && code != 0x2800 &&
		(code < 0x2000 || code > 0x200F) && (code < 0x2028 || code > 0x202F) &&
		(code < 0x205F || code > 0x2064) && (code < 0x206A || code > 0x206F) &&
		(code < 0xFE00 || code > 0xFE0F) && code != 0xFEFF &&
		(code < 0xFFF9 || code > 0xFFFC);
}

const char *str_utf8_skip_whitespaces(const char *str)
{
	const char *str_old;
	int code;

	while(*str)
	{
		str_old = str;
		code = str_utf8_decode(&str);

		// check if unicode is not empty
		if(str_utf8_isspace(code))
		{
			return str_old;
		}
	}

	return str;
}

int str_utf8_isstart(char c)
{
	if((c&0xC0) == 0x80) /* 10xxxxxx */
		return 0;
	return 1;
}

int str_utf8_rewind(const char *str, int cursor)
{
	while(cursor)
	{
		cursor--;
		if(str_utf8_isstart(*(str + cursor)))
			break;
	}
	return cursor;
}

int str_utf8_forward(const char *str, int cursor)
{
	const char *buf = str + cursor;
	if(!buf[0])
		return cursor;

	if((*buf&0x80) == 0x0)  /* 0xxxxxxx */
		return cursor+1;
	else if((*buf&0xE0) == 0xC0) /* 110xxxxx */
	{
		if(!buf[1]) return cursor+1;
		return cursor+2;
	}
	else  if((*buf & 0xF0) == 0xE0)	/* 1110xxxx */
	{
		if(!buf[1]) return cursor+1;
		if(!buf[2]) return cursor+2;
		return cursor+3;
	}
	else if((*buf & 0xF8) == 0xF0)	/* 11110xxx */
	{
		if(!buf[1]) return cursor+1;
		if(!buf[2]) return cursor+2;
		if(!buf[3]) return cursor+3;
		return cursor+4;
	}

	/* invalid */
	return cursor+1;
}

int str_utf8_encode(char *ptr, int chr)
{
	/* encode */
	if(chr <= 0x7F)
	{
		ptr[0] = (char)chr;
		return 1;
	}
	else if(chr <= 0x7FF)
	{
		ptr[0] = 0xC0|((chr>>6)&0x1F);
		ptr[1] = 0x80|(chr&0x3F);
		return 2;
	}
	else if(chr <= 0xFFFF)
	{
		ptr[0] = 0xE0|((chr>>12)&0x0F);
		ptr[1] = 0x80|((chr>>6)&0x3F);
		ptr[2] = 0x80|(chr&0x3F);
		return 3;
	}
	else if(chr <= 0x10FFFF)
	{
		ptr[0] = 0xF0|((chr>>18)&0x07);
		ptr[1] = 0x80|((chr>>12)&0x3F);
		ptr[2] = 0x80|((chr>>6)&0x3F);
		ptr[3] = 0x80|(chr&0x3F);
		return 4;
	}

	return 0;
}

static unsigned char str_byte_next(const char **ptr)
{
	unsigned char byte = **ptr;
	(*ptr)++;
	return byte;
}

static void str_byte_rewind(const char **ptr)
{
	(*ptr)--;
}

int str_utf8_decode(const char **ptr)
{
	// As per https://encoding.spec.whatwg.org/#utf-8-decoder.
	unsigned char utf8_lower_boundary = 0x80;
	unsigned char utf8_upper_boundary = 0xBF;
	int utf8_code_point = 0;
	int utf8_bytes_seen = 0;
	int utf8_bytes_needed = 0;
	while(1)
	{
		unsigned char byte = str_byte_next(ptr);
		if(utf8_bytes_needed == 0)
		{
			if(byte <= 0x7F)
			{
				return byte;
			}
			else if(0xC2 <= byte && byte <= 0xDF)
			{
				utf8_bytes_needed = 1;
				utf8_code_point = byte - 0xC0;
			}
			else if(0xE0 <= byte && byte <= 0xEF)
			{
				if(byte == 0xE0) utf8_lower_boundary = 0xA0;
				if(byte == 0xED) utf8_upper_boundary = 0x9F;
				utf8_bytes_needed = 2;
				utf8_code_point = byte - 0xE0;
			}
			else if(0xF0 <= byte && byte <= 0xF4)
			{
				if(byte == 0xF0) utf8_lower_boundary = 0x90;
				if(byte == 0xF4) utf8_upper_boundary = 0x8F;
				utf8_bytes_needed = 3;
				utf8_code_point = byte - 0xF0;
			}
			else
			{
				return -1; // Error.
			}
			utf8_code_point = utf8_code_point << (6 * utf8_bytes_needed);
			continue;
		}
		if(!(utf8_lower_boundary <= byte && byte <= utf8_upper_boundary))
		{
			// Resetting variables not necessary, will be done when
			// the function is called again.
			str_byte_rewind(ptr);
			return -1;
		}
		utf8_lower_boundary = 0x80;
		utf8_upper_boundary = 0xBF;
		utf8_bytes_seen += 1;
		utf8_code_point = utf8_code_point + ((byte - 0x80) << (6 * (utf8_bytes_needed - utf8_bytes_seen)));
		if(utf8_bytes_seen != utf8_bytes_needed)
		{
			continue;
		}
		// Resetting variables not necessary, see above.
		return utf8_code_point;
	}
}

int str_utf8_check(const char *str)
{
	int codepoint;
	while((codepoint = str_utf8_decode(&str)))
	{
		if(codepoint == -1)
		{
			return 0;
		}
	}
	return 1;
}


unsigned str_quickhash(const char *str)
{
	unsigned hash = 5381;
	for(; *str; str++)
		hash = ((hash << 5) + hash) + (*str); /* hash * 33 + c */
	return hash;
}

static const char *str_token_get(const char *str, const char *delim, int *length)
{
	size_t len = strspn(str, delim);
	if(len > 1)
		str++;
	else
		str += len;
	if(!*str)
		return NULL;

	*length = strcspn(str, delim);
	return str;
}

const char *str_next_token(const char *str, const char *delim, char *buffer, int buffer_size)
{
	int len = 0;
	const char *tok = str_token_get(str, delim, &len);
	if(len < 0 || tok == NULL)
	{
		buffer[0] = '\0';
		return NULL;
	}

	len = buffer_size > len ? len : buffer_size - 1;
	mem_copy(buffer, tok, len);
	buffer[len] = '\0';

	return tok + len;
}

int pid()
{
#if defined(CONF_FAMILY_WINDOWS)
	return _getpid();
#else
	return getpid();
#endif
}

void shell_execute(const char *file)
{
#if defined(CONF_FAMILY_WINDOWS)
	ShellExecute(NULL, NULL, file, NULL, NULL, SW_SHOWDEFAULT);
#elif defined(CONF_FAMILY_UNIX)
	char* argv[2];
	argv[0] = (char*) file;
	argv[1] = NULL;
	pid_t pid = fork();
	if(!pid)
		execv(file, argv);
#endif
}

int os_compare_version(int major, int minor)
{
#if defined(CONF_FAMILY_WINDOWS)
	OSVERSIONINFO ver;
	mem_zero(&ver, sizeof(OSVERSIONINFO));
	ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&ver);
	if(ver.dwMajorVersion > major || (ver.dwMajorVersion == major && ver.dwMinorVersion > minor))
		return 1;
	else if(ver.dwMajorVersion == major && ver.dwMinorVersion == minor)
		return 0;
	else
		return -1;
#else
	return 0; // unimplemented
#endif
}

struct SECURE_RANDOM_DATA
{
	int initialized;
#if defined(CONF_FAMILY_WINDOWS)
	HCRYPTPROV provider;
#else
	IOHANDLE urandom;
#endif
};

static struct SECURE_RANDOM_DATA secure_random_data = { 0 };

int secure_random_init()
{
	if(secure_random_data.initialized)
	{
		return 0;
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(CryptAcquireContext(&secure_random_data.provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		secure_random_data.initialized = 1;
		return 0;
	}
	else
	{
		return 1;
	}
#else
	secure_random_data.urandom = io_open("/dev/urandom", IOFLAG_READ);
	if(secure_random_data.urandom)
	{
		secure_random_data.initialized = 1;
		return 0;
	}
	else
	{
		return 1;
	}
#endif
}

void secure_random_fill(void *bytes, size_t length)
{
	if(!secure_random_data.initialized)
	{
		dbg_msg("secure", "called secure_random_fill before secure_random_init");
		dbg_break();
	}
#if defined(CONF_FAMILY_WINDOWS)
	if(!CryptGenRandom(secure_random_data.provider, length, bytes))
	{
		dbg_msg("secure", "CryptGenRandom failed, last_error=%d", GetLastError());
		dbg_break();
	}
#else
	if(length != io_read(secure_random_data.urandom, bytes, length))
	{
		dbg_msg("secure", "io_read returned with a short read");
		dbg_break();
	}
#endif
}

int secure_rand()
{
	unsigned int i;
	secure_random_fill(&i, sizeof(i));
	return (int)(i%RAND_MAX);
}

#if defined(__cplusplus)
}
#endif
