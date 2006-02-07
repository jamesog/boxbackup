// emulates unix syscalls to win32 functions

#ifndef EMU_INCLUDE
#define EMU_INCLUDE

#if defined WIN32 || defined __CYGWIN__
	#include <errno.h>
	#define LOG_INFO 6
	#define LOG_WARNING 4
	#define LOG_ERR 3
	#define LOG_PID 0
	#define LOG_LOCAL6 0
#endif

#ifdef WIN32

#define _STAT_DEFINED
#define _INO_T_DEFINED

#include <winsock2.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
//#include <winsock.h>
//#include <sys/types.h>
//#include <sys/stat.h>

#include <string>

#define gmtime_r( _clock, _result ) \
	( *(_result) = *gmtime( (_clock) ), \
	(_result) )


//signal in unix SIGVTALRM does not exist in win32 - but looking at the 
#define SIGVTALRM 254
#define SIGALRM SIGVTALRM
#define ITIMER_VIRTUAL 0

int setitimer(int type , struct itimerval *timeout, int);
void InitTimer(void);
void FiniTimer(void);

inline int geteuid(void)
{
	//lets pretend to be root!
	return 0;
}

struct passwd {
	char *pw_name;
	char *pw_passwd;
	uid_t pw_uid;
	gid_t pw_gid;
	time_t pw_change;
	char *pw_class;
	char *pw_gecos;
	char *pw_dir;
	char *pw_shell;
	time_t pw_expire;
};

extern passwd gTempPasswd;
inline struct passwd * getpwnam(const char * name)
{
	//for the mo pretend to be root
	gTempPasswd.pw_uid = 0;
	gTempPasswd.pw_gid = 0;

	return &gTempPasswd;
}

#define S_IRWXG 1
#define S_IRWXO 2
#define S_ISUID 4
#define S_ISGID 8
#define S_ISVTX 16

#ifndef __MINGW32__
	//not sure if these are correct
	//S_IWRITE -   writing permitted
	//_S_IREAD -    reading permitted
	//_S_IREAD | _S_IWRITE - 
	#define S_IRUSR S_IWRITE
	#define S_IWUSR S_IREAD
	#define S_IRWXU (S_IREAD|S_IWRITE|S_IEXEC)

	#define S_ISREG(x) (S_IFREG & x)
	#define S_ISDIR(x) (S_IFDIR & x)
#endif

inline int utimes(const char * Filename, timeval[])
{
	//again I am guessing this is quite important to
	//be functioning, as large restores would be a problem

	//indicate success
	return 0;
}
inline int chown(const char * Filename, u_int32_t uid, u_int32_t gid)
{
	//important - this needs implementing
	//If a large restore is required then 
	//it needs to restore files AND permissions
	//reference AdjustTokenPrivileges
	//GetAccountSid
	//InitializeSecurityDescriptor
	//SetSecurityDescriptorOwner
	//The next function looks like the guy to use...
	//SetFileSecurity

	//indicate success
	return 0;
}

inline int chmod(const char * Filename, int uid)
{
	//indicate sucsess
	return 0;
}

//I do not perceive a need to change the user or group on a backup client
//at any rate the owner of a service can be set in the service settings
inline int setegid(int)
{
	return true;
}
inline int seteuid(int)
{
	return true;
}
inline int setgid(int)
{
	return true;
}
inline int setuid(int)
{
	return true;
}
inline int getgid(void)
{
	return 0;
}
inline int getuid(void)
{
	return 0;
}

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

// MinGW provides a getopt implementation
#ifndef __MINGW32__

//this will need to be implimented if we see fit that command line
//options are going to be used! (probably then:)
//where the calling function looks for the parsed parameter
extern char *optarg;
//optind looks like an index into the string - how far we have moved along
extern int optind;
extern char nextchar;

inline int getopt(int count, char * const * args, char * tolookfor)
{
	if ( optind >= count ) return -1;

	std::string str((const char *)args[optind]);
	std::string interestin(tolookfor);
	int opttolookfor = 0;
	int index = -1;
	//just initialize the string - just in case it is used.
	//optarg[0] = 0;
	std::string opt;

	if ( count == 0 ) return -1;

	do 
	{
		if ( index != -1 )
		{
			str = str.substr(index+1, str.size());
		}

		index = str.find('-');

		if ( index == -1 ) return -1;

		opt = str[1];

		optind ++;
		str = args[optind];
	}
	while ( ( opttolookfor = interestin.find(opt)) == -1 );

	if ( interestin[opttolookfor+1] == ':' ) 
	{

		//strcpy(optarg, str.c_str());
		optarg = args[optind];
		optind ++;
	}

	//indicate we have finished
	return opt[0];
}
#endif // !__MINGW32__

#define timespec timeval

//not available in win32
struct itimerval
{
	timeval 	it_interval;
	timeval 	it_value;
};

//win32 deals in usec not nsec - so need to ensure this follows through
#define tv_nsec tv_usec 

#ifndef __MINGW32__
	typedef unsigned __int64 u_int64_t;
	typedef unsigned __int64 uint64_t;
	typedef __int64 int64_t;
	typedef unsigned __int32 uint32_t;
	typedef unsigned __int32 u_int32_t;
	typedef __int32 int32_t;
	typedef unsigned __int16 uint16_t;
	typedef __int16 int16_t;
	typedef unsigned __int8 uint8_t;
	typedef __int8 int8_t;

	typedef int socklen_t;
#endif

// I (re-)defined here for the moment; has to be removed later !!! 
#ifndef BOX_VERSION
#define BOX_VERSION "0.09hWin32"
#endif

#define S_IRGRP S_IWRITE
#define S_IWGRP S_IREAD
#define S_IROTH S_IWRITE | S_IREAD
#define S_IWOTH S_IREAD | S_IREAD

//again need to verify these
#define S_IFLNK 1

#define S_ISLNK(x) ( false )

// nasty implementation to get working - TODO get the win32 equiv
#ifdef _DEBUG
#define getpid() 1
#endif

#define vsnprintf _vsnprintf

#ifndef __MINGW32__
typedef unsigned int mode_t;
#endif

inline int mkdir(const char *pathname, mode_t mode)
{
	return mkdir(pathname);
}

#ifdef __MINGW32__
	#include <dirent.h>
#else
	inline int strcasecmp(const char *s1, const char *s2)
	{
		return _stricmp(s1,s2);
	}

	struct dirent
	{
		char *d_name;
	};

	struct DIR
	{
		intptr_t		fd;	// filedescriptor
		// struct _finddata_t	info;
		struct _wfinddata_t	info;
		// struct _finddata_t 	info;
		struct dirent		result;	// d_name (first time null)
		wchar_t			*name;	// null-terminated byte string
	};

	DIR *opendir(const char *name);
	struct dirent *readdir(DIR *dp);
	int closedir(DIR *dp);
#endif

HANDLE openfile(const char *filename, int flags, int mode);

extern HANDLE gSyslogH;
void MyReportEvent(LPCTSTR *szMsg, DWORD errinfo);
inline void openlog(const char * daemonName, int, int)
{
	gSyslogH = RegisterEventSource(
		NULL,  // uses local computer 
		daemonName);    // source name
	if (gSyslogH == NULL) 
	{
	}
}

inline void closelog(void)
{
	DeregisterEventSource(gSyslogH); 
}

void syslog(int loglevel, const char *fmt, ...);

#ifndef __MINGW32__
#define strtoll _strtoi64
#endif

inline unsigned int sleep(unsigned int secs)
{
	Sleep(secs*1000);
	return(ERROR_SUCCESS);
}

#define INFTIM -1
#define POLLIN 0x1
#define POLLERR 0x8
#define POLLOUT 0x4

#define SHUT_RDWR SD_BOTH
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND

struct pollfd
{
	SOCKET fd;
	short int events;
	short int revents;
};

inline int ioctl(SOCKET sock, int flag,  int * something)
{
	//indicate success
	return 0;
}

inline int waitpid(pid_t pid, int *status, int)
{
	return 0;
}

// this shouldn't be needed.
struct statfs
{
	TCHAR f_mntonname[MAX_PATH];
};

// I think this should get us going
// Although there is a warning about 
// mount points in win32 can now exists - which means inode number can be 
// duplicated, so potential of a problem - perhaps this needs to be 
// implemented with a little more thought... TODO

struct stat {
	//_dev_t st_dev;
	u_int64_t st_ino;
	DWORD st_mode;
	short st_nlink;
	short st_uid;
	short st_gid;
	//_dev_t st_rdev;
	u_int64_t st_size;
	time_t st_atime;
	time_t st_mtime;
	time_t st_ctime;
};

#ifndef __MINGW32__
typedef u_int64_t _ino_t;
#endif

int ourstat(const char * name, struct stat * st);
int ourfstat(HANDLE file, struct stat * st);
int statfs(const char * name, struct statfs * s);

//need this for converstions
inline time_t ConvertFileTimeToTime_t(FILETIME *fileTime)
{
	SYSTEMTIME stUTC;
	struct tm timeinfo;

	// Convert the last-write time to local time.
	FileTimeToSystemTime(fileTime, &stUTC);
	// SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);
	
	timeinfo.tm_sec = stUTC.wSecond;
	timeinfo.tm_min = stUTC.wMinute;
	timeinfo.tm_hour = stUTC.wHour;
	timeinfo.tm_mday = stUTC.wDay;
	timeinfo.tm_wday = stUTC.wDayOfWeek;
	timeinfo.tm_mon = stUTC.wMonth - 1;
	// timeinfo.tm_yday = ...;
	timeinfo.tm_year = stUTC.wYear - 1900;

	time_t retVal = mktime(&timeinfo);
	return retVal;
}

#define stat(x,y) ourstat(x,y)
#define fstat(x,y) ourfstat(x,y)
#define lstat(x,y) ourstat(x,y)

int poll (struct pollfd *ufds, unsigned long nfds, int timeout);
bool EnableBackupRights( void );

#endif // WIN32

#ifdef __CYGWIN__
typedef char TCHAR;
#define MAX_PATH 1024
#endif

#if defined WIN32 || defined __CYGWIN__
//
// MessageId: MSG_ERR_EXIST
// MessageText:
//  Box Backup.
//
#define MSG_ERR_EXIST                         ((DWORD)0xC0000004L)
#endif // WIN32 || __CYGWIN__

#endif // !EMU_INCLUDE
