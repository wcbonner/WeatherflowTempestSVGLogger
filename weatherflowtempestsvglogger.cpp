#include "wimiso8601.h"
#include <arpa/inet.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <getopt.h>
#include <iostream>
#include <queue>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// WeatherflowTempestSVGLogger.cpp : Defines the entry point for the application.

using namespace std;

/////////////////////////////////////////////////////////////////////////////
#if __has_include("weatherflowtempestsvglogger-version.h")
#include "weatherflowtempestsvglogger-version.h"
#endif
#ifndef WeatherFlowTempestLogger_VERSION
#define WeatherFlowTempestLogger_VERSION "(non-CMake)"
#endif // !WeatherFlowTempestLogger_VERSION
/////////////////////////////////////////////////////////////////////////////
static const std::string ProgramVersionString("WeatherFlowTempestLogger Version " WeatherFlowTempestLogger_VERSION " Built on: " __DATE__ " at " __TIME__);
/////////////////////////////////////////////////////////////////////////////
int ConsoleVerbosity(1);
std::filesystem::path LogDirectory;	// If this remains empty, log Files are not created.
std::filesystem::path CacheDirectory;	// If this remains empty, cache Files are not used. Cache Files should greatly speed up startup of the program if logged data runs multiple years over many devices.
std::filesystem::path SVGDirectory;	// If this remains empty, SVG Files are not created. If it's specified, _day, _week, _month, and _year.svg files are created for each bluetooth address seen.
int LogFileTime(60);	// Time between log file writes, to reduce frequency of writing to SD Card
int SVGBattery(0); // 0x01 = Draw Battery line on daily, 0x02 = Draw Battery line on weekly, 0x04 = Draw Battery line on monthly, 0x08 = Draw Battery line on yearly
int SVGMinMax(0); // 0x01 = Draw Temperature and Humiditiy Minimum and Maximum line on daily, 0x02 = on weekly, 0x04 = on monthly, 0x08 = on yearly
bool SVGFahrenheit(true);
std::filesystem::path SVGTitleMapFilename;
std::filesystem::path SVGIndexFilename;
// The following details were taken from https://github.com/oetiker/mrtg
const size_t DAY_COUNT(600);			/* 400 samples is 33.33 hours */
const size_t WEEK_COUNT(600);			/* 400 samples is 8.33 days */
const size_t MONTH_COUNT(600);			/* 400 samples is 33.33 days */
const size_t YEAR_COUNT(2 * 366);		/* 1 sample / day, 366 days, 2 years */
const size_t DAY_SAMPLE(5 * 60);		/* Sample every 5 minutes */
const size_t WEEK_SAMPLE(30 * 60);		/* Sample every 30 minutes */
const size_t MONTH_SAMPLE(2 * 60 * 60);	/* Sample every 2 hours */
const size_t YEAR_SAMPLE(24 * 60 * 60);	/* Sample every 24 hours */
/////////////////////////////////////////////////////////////////////////////
bool ValidateDirectory(const std::filesystem::path& DirectoryName)
{
	bool rval = false;
	// https://linux.die.net/man/2/stat
	struct stat64 StatBuffer;
	if (0 == stat64(DirectoryName.c_str(), &StatBuffer))
		if (S_ISDIR(StatBuffer.st_mode))
		{
			// https://linux.die.net/man/2/access
			if (0 == access(DirectoryName.c_str(), R_OK | W_OK))
				rval = true;
			else
			{
				switch (errno)
				{
				case EACCES:
					std::cerr << DirectoryName << " (" << errno << ") The requested access would be denied to the file, or search permission is denied for one of the directories in the path prefix of pathname." << std::endl;
					break;
				case ELOOP:
					std::cerr << DirectoryName << " (" << errno << ") Too many symbolic links were encountered in resolving pathname." << std::endl;
					break;
				case ENAMETOOLONG:
					std::cerr << DirectoryName << " (" << errno << ") pathname is too long." << std::endl;
					break;
				case ENOENT:
					std::cerr << DirectoryName << " (" << errno << ") A component of pathname does not exist or is a dangling symbolic link." << std::endl;
					break;
				case ENOTDIR:
					std::cerr << DirectoryName << " (" << errno << ") A component used as a directory in pathname is not, in fact, a directory." << std::endl;
					break;
				case EROFS:
					std::cerr << DirectoryName << " (" << errno << ") Write permission was requested for a file on a read-only file system." << std::endl;
					break;
				case EFAULT:
					std::cerr << DirectoryName << " (" << errno << ") pathname points outside your accessible address space." << std::endl;
					break;
				case EINVAL:
					std::cerr << DirectoryName << " (" << errno << ") mode was incorrectly specified." << std::endl;
					break;
				case EIO:
					std::cerr << DirectoryName << " (" << errno << ") An I/O error occurred." << std::endl;
					break;
				case ENOMEM:
					std::cerr << DirectoryName << " (" << errno << ") Insufficient kernel memory was available." << std::endl;
					break;
				case ETXTBSY:
					std::cerr << DirectoryName << " (" << errno << ") Write access was requested to an executable which is being executed." << std::endl;
					break;
				default:
					std::cerr << DirectoryName << " (" << errno << ") An unknown error." << std::endl;
				}
			}
		}
	return(rval);
}
std::filesystem::path GenerateLogFileName(time_t timer = 0)
{
	std::ostringstream OutputFilename;
	OutputFilename << "weatherflow";
	if (timer == 0)
		time(&timer);
	struct tm UTC;
	if (0 != gmtime_r(&timer, &UTC))
		if (!((UTC.tm_year == 70) && (UTC.tm_mon == 0) && (UTC.tm_mday == 1)))
			OutputFilename << "-" << std::dec << UTC.tm_year + 1900 << "-" << std::setw(2) << std::setfill('0') << UTC.tm_mon + 1;
	OutputFilename << ".txt";
	std::filesystem::path FQFileName(LogDirectory / OutputFilename.str());
	return(FQFileName);
}
bool GenerateLogFile(std::queue<std::string> & Data)
{
	bool rval = false;
	if (!LogDirectory.empty())
	{
		std::filesystem::path filename(GenerateLogFileName());
		if (ConsoleVerbosity > 1)
			std::cout << "[" << getTimeISO8601() << "] GenerateLogFile: " << filename << std::endl;
		std::ofstream LogFile(filename, std::ios_base::out | std::ios_base::app | std::ios_base::ate);
		if (LogFile.is_open())
		{
			//time_t MostRecentData(0);
			while (!Data.empty())
			{
				LogFile << Data.front() << std::endl;
				Data.pop();
			}
			LogFile.close();
			//struct utimbuf Log_ut;
			//Log_ut.actime = MostRecentData;
			//Log_ut.modtime = MostRecentData;
			//utime(filename.c_str(), &Log_ut);
			rval = true;
		}
	}
	else
	{
		// clear the queued data if LogDirectory not specified
		while (!Data.empty())
			Data.pop();
	}
	return(rval);
}
/////////////////////////////////////////////////////////////////////////////

// msgs are super small
#define BUF_SIZE 1024

/////////////////////////////////////////////////////////////////////////////
volatile bool bRun = true; // This is declared volatile so that the compiler won't optimized it out of loops later in the code
void SignalHandlerSIGINT(int signal)
{
	bRun = false;
	std::cerr << "***************** SIGINT: Caught Ctrl-C, finishing loop and quitting. *****************" << std::endl;
}
void SignalHandlerSIGHUP(int signal)
{
	bRun = false;
	std::cerr << "***************** SIGHUP: Caught HangUp, finishing loop and quitting. *****************" << std::endl;
}
void SignalHandlerSIGALRM(int signal)
{
	if (ConsoleVerbosity > 0)
		std::cout << "[" << getTimeISO8601() << "] ***************** SIGALRM: Caught Alarm. *****************" << std::endl;
}
/////////////////////////////////////////////////////////////////////////////
static void usage(int argc, char** argv)
{
	std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
	std::cout << "  " << ProgramVersionString << std::endl;
	std::cout << "  Options:" << std::endl;
	std::cout << "    -h | --help          Print this message" << std::endl;
	std::cout << "    -l | --log name      Logging Directory [" << LogDirectory << "]" << std::endl;
	std::cout << "    -t | --time seconds  Time between log file writes [" << LogFileTime << "]" << std::endl;
	std::cout << "    -v | --verbose level stdout verbosity level [" << ConsoleVerbosity << "]" << std::endl;
	std::cout << "    -f | --cache name    cache file directory [" << CacheDirectory << "]" << std::endl;
	std::cout << "    -s | --svg name      SVG output directory [" << SVGDirectory << "]" << std::endl;
	std::cout << "    -c | --celsius       SVG output using degrees C [" << std::boolalpha << !SVGFahrenheit << "]" << std::endl;
	std::cout << "    -b | --battery graph Draw the battery status on SVG graphs. 1:daily, 2:weekly, 4:monthly, 8:yearly" << std::endl;
	std::cout << "    -x | --minmax graph  Draw the minimum and maximum temperature and humidity status on SVG graphs. 1:daily, 2:weekly, 4:monthly, 8:yearly" << std::endl;
	std::cout << std::endl;
}
static const char short_options[] = "hl:t:v:f:s:cb:x";
static const struct option long_options[] = {
		{ "help",   no_argument,       NULL, 'h' },
		{ "log",    required_argument, NULL, 'l' },
		{ "time",   required_argument, NULL, 't' },
		{ "verbose",required_argument, NULL, 'v' },
		{ "cache",	required_argument, NULL, 'f' },
		{ "svg",	required_argument, NULL, 's' },
		{ "celsius",no_argument,       NULL, 'c' },
		{ "battery",required_argument, NULL, 'b' },
		{ "minmax",	required_argument, NULL, 'x' },
		{ 0, 0, 0, 0 }
};
/////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
	// Log WeatherFlow Tempest UDP broadcast messages to stdout
	// Ref: https://weatherflow.github.io/Tempest/api/udp/v171/

	///////////////////////////////////////////////////////////////////////////////////////////////
	for (;;)
	{
		std::string TempString;
		std::filesystem::path TempPath;
		int idx;
		int c = getopt_long(argc, argv, short_options, long_options, &idx);
		if (-1 == c)
			break;
		switch (c)
		{
		case 0: /* getopt_long() flag */
			break;
		case '?':
		case 'h':	// --help
			usage(argc, argv);
			exit(EXIT_SUCCESS);
		case 'l':	// --log
			TempPath = std::string(optarg);
			while (TempPath.filename().empty() && (TempPath != TempPath.root_directory())) // This gets rid of the "/" on the end of the path
				TempPath = TempPath.parent_path();
			if (ValidateDirectory(TempPath))
				LogDirectory = TempPath;
			break;
		case 't':	// --time
			try { LogFileTime = std::stoi(optarg); }
			catch (const std::invalid_argument& ia) { std::cerr << "Invalid argument: " << ia.what() << std::endl; exit(EXIT_FAILURE); }
			catch (const std::out_of_range& oor) { std::cerr << "Out of Range error: " << oor.what() << std::endl; exit(EXIT_FAILURE); }
			break;
		case 'v':	// --verbose
			try { ConsoleVerbosity = std::stoi(optarg); }
			catch (const std::invalid_argument& ia) { std::cerr << "Invalid argument: " << ia.what() << std::endl; exit(EXIT_FAILURE); }
			catch (const std::out_of_range& oor) { std::cerr << "Out of Range error: " << oor.what() << std::endl; exit(EXIT_FAILURE); }
			break;
		case 'f':	// --cache
			TempPath = std::string(optarg);
			while (TempPath.filename().empty() && (TempPath != TempPath.root_directory())) // This gets rid of the "/" on the end of the path
				TempPath = TempPath.parent_path();
			if (ValidateDirectory(TempPath))
				CacheDirectory = TempPath;
			break;
		case 's':	// --svg
			TempPath = std::string(optarg);
			while (TempPath.filename().empty() && (TempPath != TempPath.root_directory())) // This gets rid of the "/" on the end of the path
				TempPath = TempPath.parent_path();
			if (ValidateDirectory(TempPath))
				SVGDirectory = TempPath;
			break;
		case 'c':	// --celsius
			SVGFahrenheit = false;
			break;
		case 'b':	// --battery
			try { SVGBattery = std::stoi(optarg); }
			catch (const std::invalid_argument& ia) { std::cerr << "Invalid argument: " << ia.what() << std::endl; exit(EXIT_FAILURE); }
			catch (const std::out_of_range& oor) { std::cerr << "Out of Range error: " << oor.what() << std::endl; exit(EXIT_FAILURE); }
			break;
		case 'x':	// --minmax
			try { SVGMinMax = std::stoi(optarg); }
			catch (const std::invalid_argument& ia) { std::cerr << "Invalid argument: " << ia.what() << std::endl; exit(EXIT_FAILURE); }
			catch (const std::out_of_range& oor) { std::cerr << "Out of Range error: " << oor.what() << std::endl; exit(EXIT_FAILURE); }
			break;
		default:
			usage(argc, argv);
			exit(EXIT_FAILURE);
		}
	}
	///////////////////////////////////////////////////////////////////////////////////////////////
	int ExitValue = EXIT_SUCCESS;
	///////////////////////////////////////////////////////////////////////////////////////////////
	if (ConsoleVerbosity > 0)
	{
		std::cout << "[" << getTimeISO8601() << "] " << ProgramVersionString << std::endl;
		if (ConsoleVerbosity > 1)
		{
			std::cout << "[                   ]      log: " << LogDirectory << std::endl;
			std::cout << "[                   ]    cache: " << CacheDirectory << std::endl;
			std::cout << "[                   ]      svg: " << SVGDirectory << std::endl;
			std::cout << "[                   ]  battery: " << SVGBattery << std::endl;
			std::cout << "[                   ]   minmax: " << SVGMinMax << std::endl;
			std::cout << "[                   ]  celsius: " << std::boolalpha << !SVGFahrenheit << std::endl;
			std::cout << "[                   ] titlemap: " << SVGTitleMapFilename << std::endl;
			std::cout << "[                   ]     time: " << LogFileTime << std::endl;
		}
	}
	else
		std::cerr << ProgramVersionString << " (starting)" << std::endl;
	///////////////////////////////////////////////////////////////////////////////////////////////
	tzset();
	///////////////////////////////////////////////////////////////////////////////////////////////
	auto previousHandlerSIGINT = std::signal(SIGINT, SignalHandlerSIGINT);	// Install CTR-C signal handler
	auto previousHandlerSIGHUP = std::signal(SIGHUP, SignalHandlerSIGHUP);	// Install Hangup signal handler
	auto previousAlarmHandler = std::signal(SIGALRM, SignalHandlerSIGALRM);	// Install Alarm signal handler
	bRun = true;
	std::queue<std::string> DataToBeLogged;
	struct sockaddr_in si_me;
	memset(&si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(50222);
	si_me.sin_addr.s_addr = INADDR_ANY;
	int UDPSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int broadcast = 1;
	setsockopt(UDPSocket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast);
	::bind(UDPSocket, (sockaddr*)&si_me, sizeof(sockaddr));
	time_t TimeStart(0);
	time(&TimeStart);
	while (bRun)
	{
		char buf[BUF_SIZE];
		unsigned slen = sizeof(sockaddr);
		// This select() call coming up will sit and wait until until the socket read would return something that's not EAGAIN/EWOULDBLOCK
		// But first we need to set a timeout -- we need to do this every time before we call select()
		struct timeval select_timeout = { 60, 0 };	// 60 second timeout, 0 microseconds
		// and reset the value of check_set, since that's what will tell us what descriptors were ready
		// Set up the file descriptor set that select() will use
		fd_set check_set;
		FD_ZERO(&check_set);
		FD_SET(UDPSocket, &check_set);
		// This will block until either a read is ready (i.e. won’t return EWOULDBLOCK) -1 on error, 0 on timeout, otherwise number of FDs changed
		if (0 < select(UDPSocket + 1, &check_set, NULL, NULL, &select_timeout))	// returns number of handles ready to read. 0 or negative indicate other than good data to read.
		{
			// We got data ready to read, check and make sure it's the right descriptor, just as a sanity check (it shouldn't be possible ot get anything else)
			if (FD_ISSET(UDPSocket, &check_set))
			{
				// okay, if we made it this far, we can read our descriptor, and shouldn't get EAGAIN. Ideally, the right way to process this is 'read in a loop
				// until you get EAGAIN and then go back to select()', but worst case is that you don't read everything availableand select() immediately returns, so not
				// a *huge* deal just doing one read and then back to select, here.
				struct sockaddr_in si_other;
				auto bufDataLen = recvfrom(UDPSocket, buf, sizeof(buf), 0, (sockaddr*)&si_other, &slen);
				//auto bufDataLen = read(s, buf, sizeof(buf));
				std::string JSonData(buf, 0, bufDataLen);
				DataToBeLogged.push(JSonData);
				if (ConsoleVerbosity > 0)
					std::cout << "[" << getTimeISO8601() << "] " << JSonData << std::endl;
			}
		}
		time_t TimeNow;
		time(&TimeNow);
		if (difftime(TimeNow, TimeStart) > LogFileTime)
		{
			if (ConsoleVerbosity > 0)
				std::cout << "[" << getTimeISO8601() << "] " << std::dec << LogFileTime << " seconds or more have passed. Writing LOG Files" << std::endl;
			TimeStart = TimeNow;
			GenerateLogFile(DataToBeLogged);
		}
	}
	close(UDPSocket);
	GenerateLogFile(DataToBeLogged);
	std::signal(SIGALRM, previousAlarmHandler);	// Restore original Alarm signal handler
	std::signal(SIGHUP, previousHandlerSIGHUP);	// Restore original Hangup signal handler
	std::signal(SIGINT, previousHandlerSIGINT);	// Restore original Ctrl-C signal handler
	///////////////////////////////////////////////////////////////////////////////////////////////
	std::cerr << ProgramVersionString << " (exiting)" << std::endl;
	return(ExitValue);
}
