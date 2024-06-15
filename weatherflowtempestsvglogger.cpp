#include "wimiso8601.h"
#include <arpa/inet.h>
#include <cfloat>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <jsoncpp/json/json.h>
#include <queue>
#include <regex>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
int LogFileTime(300);	// Time between log file writes, to reduce frequency of writing to SD Card
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
class  TempestObservation {
public:
	time_t Time;
	//std::string WriteTXT(const char seperator = '\t') const;
	std::string WriteCache(void) const;
	bool ReadCache(const std::string& data);
	//bool ReadMSG(const uint8_t* const data);
	TempestObservation() : Time(0), Temperature(0), TemperatureMin(DBL_MAX), TemperatureMax(-DBL_MAX), Humidity(0), HumidityMin(DBL_MAX), HumidityMax(-DBL_MAX), Battery(INT_MAX), Averages(0) { };
	TempestObservation(const time_t tim, const double tem, const double hum, const int bat)
	{
		Time = tim;
		Temperature = tem;
		TemperatureMin = tem;
		TemperatureMax = tem;
		Humidity = hum;
		HumidityMin = hum;
		HumidityMax = hum;
		Battery = bat;
		Averages = 1;
	};
	TempestObservation(const std::string& data);
	//double GetTemperature(const bool Fahrenheit = false, const int index = 0) const { if (Fahrenheit) return((Temperature[index] * 9.0 / 5.0) + 32.0); return(Temperature[index]); };
	//double GetTemperatureMin(const bool Fahrenheit = false, const int index = 0) const { if (Fahrenheit) return(std::min(((Temperature[index] * 9.0 / 5.0) + 32.0), ((TemperatureMin[index] * 9.0 / 5.0) + 32.0))); return(std::min(Temperature[index], TemperatureMin[index])); };
	//double GetTemperatureMax(const bool Fahrenheit = false, const int index = 0) const { if (Fahrenheit) return(std::max(((Temperature[index] * 9.0 / 5.0) + 32.0), ((TemperatureMax[index] * 9.0 / 5.0) + 32.0))); return(std::max(Temperature[index], TemperatureMax[index])); };
	//void SetMinMax(const Govee_Temp& a);
	//double GetHumidity(void) const { return(Humidity); };
	//double GetHumidityMin(void) const { return(std::min(Humidity, HumidityMin)); };
	//double GetHumidityMax(void) const { return(std::max(Humidity, HumidityMax)); };
	//int GetBattery(void) const { return(Battery); };
	//ThermometerType GetModel(void) const { return(Model); };
	//ThermometerType SetModel(const std::string& Name);
	//ThermometerType SetModel(const unsigned short* UUID);
	enum granularity { day, week, month, year };
	void NormalizeTime(granularity type);
	granularity GetTimeGranularity(void) const;
	bool IsValid(void) const { return(Averages > 0); };
	TempestObservation& operator +=(const TempestObservation& b);
protected:
	int Averages;
	double WindSpeed;
	double WindSpeedMin;
	double WindSpeedMax;
	int WindDirection;
	int WindInterval;
	double OutsidePressure;
	double OutsidePressureMin;
	double OutsidePressureMax;
	double Temperature;
	double TemperatureMin;
	double TemperatureMax;
	double Humidity;
	double HumidityMin;
	double HumidityMax;
	//auto illuminance = observation[0][5].asInt();
	//auto UV = observation[0][5].asFloat();
	//auto solar_radiation = observation[0][5].asInt();
	//auto rain_accumulation_over_the_previous_minute = observation[0][5].asFloat();
	//auto precipitation_type = observation[0][5].asInt();
	//auto lightning_strike_average_distance = observation[0][5].asInt();
	//auto lightning_strike_count = observation[0][5].asInt();
	double Battery;
	int ReportingInterval;
};
TempestObservation::TempestObservation(const std::string& JSonData)
{
	// https://github.com/open-source-parsers/jsoncpp
	const auto rawJsonLength = static_cast<int>(JSonData.length());
	JSONCPP_STRING err;
	Json::Value root;
	Json::CharReaderBuilder builder;
	const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	if (!reader->parse(JSonData.c_str(), JSonData.c_str() + rawJsonLength, &root, &err))
	{
		if (ConsoleVerbosity > 0)
			std::cout << "json reader error" << std::endl;
	}
	else
	{
		const Json::Value observation = root["obs"];
		if (observation.size() == 1)
			if (observation[0].size() == 18)
			{
				//	{"serial_number":"ST-00145757","type":"obs_st","hub_sn":"HB-00147479","obs":[[1718217086,1.58,2.25,3.22,340,3,1025.33,14.58,60.34,138057,10.17,1150,0.000000,0,0,0,2.805,1]],"firmware_revision":176}
				Time = observation[0][0].asLargestInt();
				WindSpeedMin = observation[0][1].asDouble();
				WindSpeed = observation[0][2].asDouble();
				WindSpeedMax = observation[0][3].asDouble();
				WindDirection = observation[0][4].asInt();
				WindInterval = observation[0][5].asInt();
				OutsidePressure = OutsidePressureMin = OutsidePressureMax = observation[0][6].asDouble();
				Temperature = TemperatureMin = TemperatureMax = observation[0][7].asDouble();
				Humidity = HumidityMin = HumidityMax = observation[0][8].asDouble();
				auto illuminance = observation[0][9].asInt();
				auto UV = observation[0][10].asDouble();
				auto solar_radiation = observation[0][11].asInt();
				auto rain_accumulation_over_the_previous_minute = observation[0][12].asDouble();
				auto precipitation_type = observation[0][13].asInt();
				auto lightning_strike_average_distance = observation[0][14].asInt();
				auto lightning_strike_count = observation[0][15].asInt();
				Battery = observation[0][16].asDouble();
				ReportingInterval = observation[0][17].asInt();
			}
	}
}
std::string TempestObservation::WriteCache(void) const
{
	std::ostringstream ssValue;
	ssValue << Time;
	ssValue << "\t" << Temperature;
	ssValue << "\t" << TemperatureMin;
	ssValue << "\t" << TemperatureMax;
	ssValue << "\t" << Humidity;
	ssValue << "\t" << HumidityMin;
	ssValue << "\t" << HumidityMax;
	ssValue << "\t" << Battery;
	ssValue << "\t" << Averages;
	return(ssValue.str());
}
bool TempestObservation::ReadCache(const std::string& data)
{
	bool rval = false;
	std::istringstream ssValue(data);
	ssValue >> Time;
	ssValue >> Temperature;
	ssValue >> TemperatureMin;
	ssValue >> TemperatureMax;
	ssValue >> Humidity;
	ssValue >> HumidityMin;
	ssValue >> HumidityMax;
	ssValue >> Battery;
	ssValue >> Averages;
	return(rval);
}
void TempestObservation::NormalizeTime(granularity type)
{
	if (type == day)
		Time = (Time / DAY_SAMPLE) * DAY_SAMPLE;
	else if (type == week)
		Time = (Time / WEEK_SAMPLE) * WEEK_SAMPLE;
	else if (type == month)
		Time = (Time / MONTH_SAMPLE) * MONTH_SAMPLE;
	else if (type == year)
	{
		struct tm UTC;
		if (0 != localtime_r(&Time, &UTC))
		{
			UTC.tm_hour = 0;
			UTC.tm_min = 0;
			UTC.tm_sec = 0;
			Time = mktime(&UTC);
		}
	}
}
TempestObservation::granularity TempestObservation::GetTimeGranularity(void) const
{
	granularity rval = granularity::day;
	struct tm UTC;
	if (0 != localtime_r(&Time, &UTC))
	{
		//if (((UTC.tm_hour == 0) && (UTC.tm_min == 0)) || ((UTC.tm_hour == 23) && (UTC.tm_min == 0) && (UTC.tm_isdst == 1)))
		if ((UTC.tm_hour == 0) && (UTC.tm_min == 0))
			rval = granularity::year;
		else if ((UTC.tm_hour % 2 == 0) && (UTC.tm_min == 0))
			rval = granularity::month;
		else if ((UTC.tm_min == 0) || (UTC.tm_min == 30))
			rval = granularity::week;
	}
	return(rval);
}
TempestObservation& TempestObservation::operator +=(const TempestObservation& b)
{
	if (b.IsValid())
	{
		Time = std::max(Time, b.Time); // Use the maximum time (newest time)
		Temperature = ((Temperature * Averages) + (b.Temperature * b.Averages)) / (Averages + b.Averages);
		TemperatureMin = std::min(std::min(Temperature, TemperatureMin), b.TemperatureMin);
		TemperatureMax = std::max(std::max(Temperature, TemperatureMax), b.TemperatureMax);
		Humidity = ((Humidity * Averages) + (b.Humidity * b.Averages)) / (Averages + b.Averages);
		HumidityMin = std::min(std::min(Humidity, HumidityMin), b.HumidityMin);
		HumidityMax = std::max(std::max(Humidity, HumidityMax), b.HumidityMax);
		Battery = std::min(Battery, b.Battery);
		Averages += b.Averages; // existing average + new average
	}
	return(*this);
}
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
	if (!LogDirectory.empty() && !Data.empty())
	{
		std::filesystem::path filename(GenerateLogFileName());
		if (ConsoleVerbosity > 0)
			std::cout << "[" << getTimeISO8601() << "] GenerateLogFile: " << filename << std::endl;
		else
			std::cerr << "GenerateLogFile: " << filename << std::endl;
		std::ofstream LogFile(filename, std::ios_base::out | std::ios_base::app | std::ios_base::ate);
		if (LogFile.is_open())
		{
			while (!Data.empty())
			{
				LogFile << Data.front() << std::endl;
				Data.pop();
			}
			LogFile.close();
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
std::vector<TempestObservation> TempestMRTGLogs; // vector structure similar to MRTG Log Files
/////////////////////////////////////////////////////////////////////////////
void UpdateMRTGData(TempestObservation& TheValue)
{
	if (TempestMRTGLogs.empty())
	{
		TempestMRTGLogs.resize(2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT + YEAR_COUNT);
		TempestMRTGLogs[0] = TheValue;	// current value
		TempestMRTGLogs[1] = TheValue;
		for (auto index = 0; index < DAY_COUNT; index++)
			TempestMRTGLogs[index + 2].Time = TempestMRTGLogs[index + 1].Time - DAY_SAMPLE;
		for (auto index = 0; index < WEEK_COUNT; index++)
			TempestMRTGLogs[index + 2 + DAY_COUNT].Time = TempestMRTGLogs[index + 1 + DAY_COUNT].Time - WEEK_SAMPLE;
		for (auto index = 0; index < MONTH_COUNT; index++)
			TempestMRTGLogs[index + 2 + DAY_COUNT + WEEK_COUNT].Time = TempestMRTGLogs[index + 1 + DAY_COUNT + WEEK_COUNT].Time - MONTH_SAMPLE;
		for (auto index = 0; index < YEAR_COUNT; index++)
			TempestMRTGLogs[index + 2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT].Time = TempestMRTGLogs[index + 1 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT].Time - YEAR_SAMPLE;
	}
	else
	{
		if (TheValue.Time > TempestMRTGLogs[0].Time)
		{
			TempestMRTGLogs[0] = TheValue;	// current value
			TempestMRTGLogs[1] += TheValue; // averaged value up to DAY_SAMPLE size
		}
	}
	bool ZeroAccumulator = false;
	auto DaySampleFirst = TempestMRTGLogs.begin() + 2;
	auto DaySampleLast = TempestMRTGLogs.begin() + 1 + DAY_COUNT;
	auto WeekSampleFirst = TempestMRTGLogs.begin() + 2 + DAY_COUNT;
	auto WeekSampleLast = TempestMRTGLogs.begin() + 1 + DAY_COUNT + WEEK_COUNT;
	auto MonthSampleFirst = TempestMRTGLogs.begin() + 2 + DAY_COUNT + WEEK_COUNT;
	auto MonthSampleLast = TempestMRTGLogs.begin() + 1 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT;
	auto YearSampleFirst = TempestMRTGLogs.begin() + 2 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT;
	auto YearSampleLast = TempestMRTGLogs.begin() + 1 + DAY_COUNT + WEEK_COUNT + MONTH_COUNT + YEAR_COUNT;
	// For every time difference between FakeMRTGFile[1] and FakeMRTGFile[2] that's greater than DAY_SAMPLE we shift that data towards the back.
	while (difftime(TempestMRTGLogs[1].Time, DaySampleFirst->Time) > DAY_SAMPLE)
	{
		ZeroAccumulator = true;
		// shuffle all the day samples toward the end
		std::copy_backward(DaySampleFirst, DaySampleLast - 1, DaySampleLast);
		*DaySampleFirst = TempestMRTGLogs[1];
		DaySampleFirst->NormalizeTime(TempestObservation::granularity::day);
		if (difftime(DaySampleFirst->Time, (DaySampleFirst + 1)->Time) > DAY_SAMPLE)
			DaySampleFirst->Time = (DaySampleFirst + 1)->Time + DAY_SAMPLE;
		if (DaySampleFirst->GetTimeGranularity() == TempestObservation::granularity::year)
		{
			if (ConsoleVerbosity > 2)
				std::cout << "[" << getTimeISO8601() << "] shuffling year " << timeToExcelLocal(DaySampleFirst->Time) << " > " << timeToExcelLocal(YearSampleFirst->Time) << std::endl;
			// shuffle all the year samples toward the end
			std::copy_backward(YearSampleFirst, YearSampleLast - 1, YearSampleLast);
			*YearSampleFirst = TempestObservation();
			for (auto iter = DaySampleFirst; (iter->IsValid() && ((iter - DaySampleFirst) < (12 * 24))); iter++) // One Day of day samples
				*YearSampleFirst += *iter;
		}
		if ((DaySampleFirst->GetTimeGranularity() == TempestObservation::granularity::year) ||
			(DaySampleFirst->GetTimeGranularity() == TempestObservation::granularity::month))
		{
			if (ConsoleVerbosity > 2)
				std::cout << "[" << getTimeISO8601() << "] shuffling month " << timeToExcelLocal(DaySampleFirst->Time) << std::endl;
			// shuffle all the month samples toward the end
			std::copy_backward(MonthSampleFirst, MonthSampleLast - 1, MonthSampleLast);
			*MonthSampleFirst = TempestObservation();
			for (auto iter = DaySampleFirst; (iter->IsValid() && ((iter - DaySampleFirst) < (12 * 2))); iter++) // two hours of day samples
				*MonthSampleFirst += *iter;
		}
		if ((DaySampleFirst->GetTimeGranularity() == TempestObservation::granularity::year) ||
			(DaySampleFirst->GetTimeGranularity() == TempestObservation::granularity::month) ||
			(DaySampleFirst->GetTimeGranularity() == TempestObservation::granularity::week))
		{
			if (ConsoleVerbosity > 2)
				std::cout << "[" << getTimeISO8601() << "] shuffling week " << timeToExcelLocal(DaySampleFirst->Time) << std::endl;
			// shuffle all the month samples toward the end
			std::copy_backward(WeekSampleFirst, WeekSampleLast - 1, WeekSampleLast);
			*WeekSampleFirst = TempestObservation();
			for (auto iter = DaySampleFirst; (iter->IsValid() && ((iter - DaySampleFirst) < 6)); iter++) // Half an hour of day samples
				*WeekSampleFirst += *iter;
		}
	}
	if (ZeroAccumulator)
		TempestMRTGLogs[1] = TempestObservation();
}
void ReadLoggedData(const std::filesystem::path& filename)
{
	// Only read the file if it's newer than what we may have cached
	bool bReadFile = true;
	struct stat64 FileStat;
	FileStat.st_mtim.tv_sec = 0;
	if (0 == stat64(filename.c_str(), &FileStat))	// returns 0 if the file-status information is obtained
	{
		if (!TempestMRTGLogs.empty())
			if (FileStat.st_mtim.tv_sec < (TempestMRTGLogs.begin()->Time))	// only read the file if it more recent than existing data
				bReadFile = false;
	}

	if (bReadFile)
	{
		if (ConsoleVerbosity > 0)
			std::cout << "[" << getTimeISO8601() << "] Reading: " << filename.string() << std::endl;
		else
			std::cerr << "Reading: " << filename.string() << std::endl;
		std::ifstream TheFile(filename);
		if (TheFile.is_open())
		{
			std::vector<std::string> SortableFile;
			std::string TheLine;
			while (std::getline(TheFile, TheLine))
				SortableFile.push_back(TheLine);
			TheFile.close();
			sort(SortableFile.begin(), SortableFile.end());
			for (auto iter = SortableFile.begin(); iter != SortableFile.end(); iter++)
			{
				TempestObservation TheValue(*iter);
				if (TheValue.IsValid())
					UpdateMRTGData(TheValue);
			}
		}
	}
}
// Finds log files specific to this program then reads the contents into the memory mapped structure simulating MRTG log files.
void ReadLoggedData(void)
{
	const std::regex LogFileRegex("weatherflow-[[:digit:]]{4}-[[:digit:]]{2}.txt");
	if (!LogDirectory.empty())
	{
		if (ConsoleVerbosity > 1)
			std::cout << "[" << getTimeISO8601() << "] ReadLoggedData: " << LogDirectory << std::endl;
		std::deque<std::filesystem::path> files;
		for (auto const& dir_entry : std::filesystem::directory_iterator{ LogDirectory })
			if (dir_entry.is_regular_file())
				if (std::regex_match(dir_entry.path().filename().string(), LogFileRegex))
					files.push_back(dir_entry);
		if (!files.empty())
		{
			sort(files.begin(), files.end());
			while (!files.empty())
			{
				ReadLoggedData(*files.begin());
				files.pop_front();
			}
		}
	}
}
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
			std::cout << "[                   ]      C++: ";
				if (__cplusplus == 202101L) std::cout << "C++23";
				else if (__cplusplus == 202002L) std::cout << "C++20";
				else if (__cplusplus == 201703L) std::cout << "C++17";
				else if (__cplusplus == 201402L) std::cout << "C++14";
				else if (__cplusplus == 201103L) std::cout << "C++11";
				else if (__cplusplus == 199711L) std::cout << "C++98";
				else std::cout << "pre-standard C++." << __cplusplus;
				std::cout << std::endl;
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
		unsigned slen = sizeof(sockaddr);
		// This select() call coming up will sit and wait until until the socket read would return something that's not EAGAIN/EWOULDBLOCK
		// But first we need to set a timeout -- we need to do this every time before we call select()
		struct timeval select_timeout = { 60, 0 };	// 60 second timeout, 0 microseconds
		// and reset the value of check_set, since that's what will tell us what descriptors were ready
		// Set up the file descriptor set that select() will use
		fd_set check_set;
		FD_ZERO(&check_set);
		FD_SET(UDPSocket, &check_set);
		// This will block until either a read is ready (i.e. won�t return EWOULDBLOCK) -1 on error, 0 on timeout, otherwise number of FDs changed
		if (0 < select(UDPSocket + 1, &check_set, NULL, NULL, &select_timeout))	// returns number of handles ready to read. 0 or negative indicate other than good data to read.
		{
			// We got data ready to read, check and make sure it's the right descriptor, just as a sanity check (it shouldn't be possible ot get anything else)
			if (FD_ISSET(UDPSocket, &check_set))
			{
				// okay, if we made it this far, we can read our descriptor, and shouldn't get EAGAIN. Ideally, the right way to process this is 'read in a loop
				// until you get EAGAIN and then go back to select()', but worst case is that you don't read everything availableand select() immediately returns, so not
				// a *huge* deal just doing one read and then back to select, here.

				char buf[1024];	// msgs are super small
				struct sockaddr_in si_other;
				auto bufDataLen = recvfrom(UDPSocket, buf, sizeof(buf), 0, (sockaddr*)&si_other, &slen);
				//auto bufDataLen = read(s, buf, sizeof(buf));
				std::string JSonData(buf, 0, bufDataLen);
				DataToBeLogged.push(JSonData);
				if (ConsoleVerbosity > 0)
					std::cout << "[" << getTimeISO8601() << "] " << JSonData << std::endl;

				// https://github.com/open-source-parsers/jsoncpp
				const auto rawJsonLength = static_cast<int>(JSonData.length());
				JSONCPP_STRING err;
				Json::Value root;
				Json::CharReaderBuilder builder;
				const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
				if (!reader->parse(JSonData.c_str(), JSonData.c_str() + rawJsonLength, &root, &err))
				{
					if (ConsoleVerbosity > 0)
						std::cout << "json reader error" << std::endl;
					//return EXIT_FAILURE;
				}
				else
				{
					// https://apidocs.tempestwx.com/reference/tempest-udp-broadcast
					const std::string msgtype = root["type"].asString();
					if (!msgtype.compare("rapid_wind"))
					{
						const Json::Value observation = root["ob"];
						if (observation.size() == 3)
						{
							//{"serial_number":"ST-00145757","type":"rapid_wind","hub_sn":"HB-00147479","ob":[1718217088,2.38,332]}
							//{"serial_number":"ST-00145757","type":"rapid_wind","hub_sn":"HB-00147479","ob":[1718217091,2.02,335]}
							//{"serial_number":"ST-00145757","type":"rapid_wind","hub_sn":"HB-00147479","ob":[1718217094,2.27,318]}
							//{"serial_number":"ST-00145757","type":"rapid_wind","hub_sn":"HB-00147479","ob":[1718217097,2.66,339]}
							//{"serial_number":"ST-00145757","type":"rapid_wind","hub_sn":"HB-00147479","ob":[1718217100,2.30,352]}
							//{"serial_number":"ST-00145757","type":"rapid_wind","hub_sn":"HB-00147479","ob":[1718217103,1.74,354]}
							//{"serial_number":"ST-00145757","type":"rapid_wind","hub_sn":"HB-00147479","ob":[1718217106,1.58,4]}
							//{"serial_number":"ST-00145757","type":"rapid_wind","hub_sn":"HB-00147479","ob":[1718217109,2.35,351]}
							auto timetick = observation[0].asLargestInt();
							auto windspeed = observation[1].asFloat();
							auto winddirection = observation[2].asInt();
							if (ConsoleVerbosity > 1)
								std::cout << "[" << getTimeISO8601() << "] Rapid Wind: " << timetick << ", " << windspeed << ", " << winddirection << std::endl;
						}
					}
					else if (!msgtype.compare("obs_st"))
					{
						TempestObservation observation(JSonData);
						if (observation.Time != 0)
							if (ConsoleVerbosity > 1)
								std::cout << "[" << timeToISO8601(observation.Time) << "] observation read properly: " << JSonData << std::endl;
					}
				}
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
	if (ConsoleVerbosity > 0)
		std::cout << "[" << getTimeISO8601() << "] " << ProgramVersionString << " (exiting)" << std::endl;
	else
		std::cerr << ProgramVersionString << " (exiting)" << std::endl;
	return(ExitValue);
}
