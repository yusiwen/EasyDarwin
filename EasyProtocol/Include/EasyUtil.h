/*
	Copyright (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.easydarwin.org
*/
/*
 * File:   EasyUtil.h
 * Author: wellsen
 *
 * Created on 2014年11月22日, 上午10:16
*/

#ifndef EASY_UTIL_H
#define	EASY_UTIL_H

#include <EasyProtocolDef.h>
#include <string>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

using namespace std;

class Easy_API EasyUtil
{
public:
	static std::string TimeT2String(EasyDarwinTimeFormat whatFormat, unsigned long time);

	//if whatFormat == EASY_TIME_FORMAT_YYYYMMDD, the time will be set to now,
	//if whatFormat == EASY_TIME_FORMAT_HHMMSS, the date will be set to today   
	static unsigned long String2TimeT(EasyDarwinTimeFormat whatFormat, const std::string &timeString);

	static unsigned long String2TimeT(const std::string &dateYYMMDD/*2014-11-23*/, const std::string &timeHHMMSS/*08:30:00*/);

	static std::string NowTime(EasyDarwinTimeFormat whatFormat);

	static unsigned long NowTime();

	static std::string GetUUID();

	static int String2Int(const std::string &value);

	static std::string Int2String(int value);

	template<typename T>
	static std::string ToString(T value)
	{
		try
		{
			return boost::lexical_cast<std::string>(value);
		}
		catch (std::exception &e)
		{
			printf("EasyUtil::ToString error: %s\n", e.what());
		}
		return std::string();
	}

	static string Base64Encode(const char* src, size_t len);
	static string Base64Decode(const char* src, size_t len);

	static bool Base64Encode(const std::string &sInput, string &sOutput);
	static bool Base64Decode(const std::string &sInput, string &sOutput);

	static string Base64Encode(const string &sInput);
	static string Base64Decode(const string &sInput);

	static void DelChar(std::string &sInput, char ch);//删除字符串中的某个特定字符 

	static unsigned char* Urldecode(unsigned char* encd, unsigned char* decd);
	static std::string Urldecode(const std::string& encd);

	static string ToLower(const string& inStr)
	{
		string result = inStr;
		boost::to_lower(result);
		return result;
	}

	static string ToUpper(const string& inStr)
	{
		string result = inStr;
		boost::to_upper(result);
		return result;
	}


	static string& ReplaceInPlace(string& str, const string& from, const string& to, string::size_type start = 0)
	{
		string result;
		string::size_type pos = 0;
		result.append(str, 0, start);
		do
		{
			pos = str.find(from, start);
			if (pos != string::npos)
			{
				result.append(str, start, pos - start);
				result.append(to);
				start = pos + from.length();
			}
			else result.append(str, start, str.size() - start);
		} while (pos != string::npos);
		str.swap(result);
		return str;
	}

	static string Replace(const string& str, const string& from, const string& to, string::size_type start = 0)
	{
		string result(str);
		ReplaceInPlace(result, from, to);
		return result;
	}

	template <class S, class It>
	static S Cat(const S& delim, const It& begin, const It& end)
	{
		S result;
		for (It it = begin; it != end; ++it)
		{
			if (!result.empty()) result.append(delim);
			result += *it;
		}
		return result;
	}

};

#endif	/* EASY_UTIL_H */