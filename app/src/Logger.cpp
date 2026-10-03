#include "pch.h"
#include "Logger.h"

#include <cstdarg>

using namespace winrt;

namespace psx
{
	static constexpr size_t kMaxLines = 200;

	static void ChiakiLogCb(ChiakiLogLevel level, const char *msg, void *user)
	{
		static_cast<Logger *>(user)->Write(level, msg);
	}

	Logger &Logger::Get()
	{
		static Logger instance;
		return instance;
	}

	Logger::Logger()
	{
		chiaki_log_init(&log_, CHIAKI_LOG_ALL & ~CHIAKI_LOG_VERBOSE & ~CHIAKI_LOG_DEBUG, ChiakiLogCb, this);
		try
		{
			std::wstring path = std::wstring(Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + L"\\psxbox.log";
			file_ = _wfopen(path.c_str(), L"w");
		}
		catch(...)
		{
			file_ = nullptr;
		}
	}

	Logger::~Logger()
	{
		if(file_)
			fclose(file_);
	}

	void Logger::Write(ChiakiLogLevel level, std::string const &msg)
	{
		std::string line = std::string("[") + chiaki_log_level_char(level) + "] " + msg;
		while(!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();

		OutputDebugStringA((line + "\n").c_str());

		std::function<void()> cb;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			lines_.push_back(line);
			while(lines_.size() > kMaxLines)
				lines_.pop_front();
			if(file_)
			{
				fputs(line.c_str(), file_);
				fputc('\n', file_);
				fflush(file_);
			}
			cb = on_changed_;
		}
		if(cb)
			cb();
	}

	std::vector<std::string> Logger::Lines()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return std::vector<std::string>(lines_.begin(), lines_.end());
	}

	void Logger::SetOnChanged(std::function<void()> cb)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		on_changed_ = std::move(cb);
	}

	std::string Format(const char *fmt, ...)
	{
		char buf[1024];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		return buf;
	}

	std::string Narrow(hstring const &s)
	{
		return to_string(s);
	}

	hstring Widen(std::string const &s)
	{
		return to_hstring(s);
	}
}
