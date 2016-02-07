#pragma once
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <functional>
#include <stdio.h>

#define USE_LOG

/** Replace some HRESULT-Functionality used by this */
#ifndef WIN32
#define FAILED(x) (x != 0)
#else
#include <windows.h>
#endif

#ifdef _MSC_VER
#define FUNCTION_SIGNATURE __FUNCSIG__
#elif defined(__GNUC__)
#define FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

/** Log if a boolen expression returned false */
#define LEB(x) {if(!x){LogError() << #x " failed!";}}

/** Log if a boolen expression returned false and return false in calling function */
#define LEB_R(x) {if(!x){LogError() << #x " failed!"; return false;}}

/** Different types of messageboxes */
#ifdef WIN32
#define ErrorBox(Msg) MessageBoxA(NULL,Msg,"Error!",MB_OK|MB_ICONERROR|MB_TOPMOST)
#define InfoBox(Msg) MessageBoxA(NULL,Msg,"Info!",MB_OK|MB_ICONASTERISK|MB_TOPMOST)
#define WarnBox(Msg) MessageBoxA(NULL,Msg,"Warning!",MB_OK|MB_ICONEXCLAMATION|MB_TOPMOST)
#else
#define ErrorBox(Msg) /** TODO **/
#define InfoBox(Msg)
#define WarnBox(Msg)
#endif

/** Logging macros */
#define LogInfo() Utils::Log("Info",__FILE__, __LINE__, FUNCTION_SIGNATURE, false, Utils::Log::EMessageType::MT_Info)
#define LogWarn() Utils::Log("Warning",__FILE__, __LINE__, FUNCTION_SIGNATURE, true, Utils::Log::EMessageType::MT_Warning)
#define LogError() Utils::Log("Error",__FILE__, __LINE__, FUNCTION_SIGNATURE, true, Utils::Log::EMessageType::MT_Error)


/** Displays a messagebox and loggs its content */
#define LogInfoBox() Utils::Log("Info",__FILE__, __LINE__, FUNCTION_SIGNATURE, false, Utils::Log::EMessageType::MT_Info)
#define LogWarnBox() Utils::Log("Warning",__FILE__, __LINE__, FUNCTION_SIGNATURE, true, Utils::Log::EMessageType::MT_Warning)
#define LogErrorBox() Utils::Log("Error",__FILE__, __LINE__, FUNCTION_SIGNATURE, true, Utils::Log::EMessageType::MT_Error)

namespace Utils
{
#ifdef USE_LOG
	class Log
	{
	public:

		enum EMessageType
		{
			MT_Info,
			MT_Warning,
			MT_Error
		};

		/** Append starting information here and wait for more messages using the <<-operator */
		Log(const char* type, const  char* file, int line, const  char* function, bool includeInfo = false, EMessageType typeID = MT_Info, bool messageBox = false)
		{
			if(includeInfo)
				m_Info << file << "(" << line << "):" << "[" << function << "] "; // {0}({1}): <message here>
			else
				m_Info << type << ": ";

			m_TypeID = typeID;
			m_MessageBox = messageBox;
		}

		/** Flush the log message to wherever we need to */
		~Log()
		{
			Flush();
		}

		/** Clears the logfile */
		static void Clear()
		{
#ifdef WIN32
			char path[MAX_PATH + 1];
			GetModuleFileNameA(NULL, path, MAX_PATH);
			s_LogFile = std::string(path);
			s_LogFile = s_LogFile.substr(0, s_LogFile.find_last_of('\\') + 1);
			s_LogFile += "Log.txt";
#else
            s_LogFile = std::string("./Log.txt");
#endif

			FILE* f;
			f = fopen(s_LogFile.c_str(), "w");
			fclose(f);
		}

		/** STL stringstream feature */
		template< typename T >
		inline Log& operator << (const T &obj)
		{
			m_Message << obj;
			return *this;
		}

		inline Log& operator << (std::wostream& (*fn)(std::wostream&))
		{
			m_Message << fn;
			return *this;
		}

		/** Called when the object is getting destroyed, which happens immediately if simply calling the constructor of this class */
		inline void Flush()
		{
			FILE* f;
			f = fopen(s_LogFile.c_str(), "a");

			// Append log-data to file
			if(f)
			{
				fputs(m_Info.str().c_str(), f);
				fputs(m_Message.str().c_str(), f);
				fputs("\n", f);

				fclose(f);

				// Do callback
				if(s_LogCallback)
					s_LogCallback(m_Message.str());
			}

#ifdef WIN32
			OutputDebugString((m_Info.str() + m_Message.str() + "\n").c_str());
#endif

			// Pop a messagebox, if wanted
			switch(m_TypeID)
			{
			case MT_Info:
				std::cout << m_Info.str() << m_Message.str() << std::endl;
                if(m_MessageBox) InfoBox(m_Message.str().c_str());
				break;

			case MT_Warning:
				std::cerr << m_Info.str() << m_Message.str() << std::endl;
				if(m_MessageBox) WarnBox(m_Message.str().c_str());
				break;

			case MT_Error:
				std::cerr << m_Info.str() << m_Message.str() << std::endl;
				if(m_MessageBox) ErrorBox(m_Message.str().c_str());
				break;
			}
		}

		/** Sets the function to be called when a log should be printed */
		static void SetLogCallback(std::function<void(const std::string&)> fn)
		{
			s_LogCallback = fn;
		}

	private:

		static std::function<void(const std::string&)> s_LogCallback;
    public: static std::string s_LogFile;

		std::stringstream m_Info; // Contains an information like "Info", "Warning" or "Error"
		std::stringstream m_Message; // Text to write into the logfile
		EMessageType m_TypeID; // Type of log
		bool m_MessageBox; // Whether to display a messagebox
	};
#else

	class Log
	{
	public:
		Log(const char* Type, const  char* File, int Line, const  char* Function, bool bIncludeInfo = false, UINT MessageBox = 0) {}

		~Log(){	}

		/** Clears the logfile */
		static void Clear()	{ }

		/** STL stringstream feature */
		template< typename T >
		inline Log& operator << (const T &obj)
		{
			return *this;
		}

		inline Log& operator << (std::wostream& (*fn)(std::wostream&))
		{
			return *this;
		}

		/** Called when the object is getting destroyed, which happens immediately if simply calling the constructor of this class */
		inline void Flush()	{ }

		/** Sets the function to be called when a log should be printed */
		static void SetLogCallback(std::function<void(const std::string&)> fn) {}
	private:

	};

#endif

}
