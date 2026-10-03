#pragma once

namespace psx
{
	// Collects log lines from chiaki and the app. Lines are kept in memory (shown on screen) and
	// appended to LocalState\psxbox.log, which can be downloaded through the Xbox Device Portal.
	class Logger
	{
	public:
		static Logger &Get();

		ChiakiLog *Chiaki() { return &log_; }

		void Write(ChiakiLogLevel level, std::string const &msg);
		void Info(std::string const &msg) { Write(CHIAKI_LOG_INFO, msg); }
		void Error(std::string const &msg) { Write(CHIAKI_LOG_ERROR, msg); }

		std::vector<std::string> Lines();

		// Called (from any thread) whenever a line is added.
		void SetOnChanged(std::function<void()> cb);

	private:
		Logger();
		~Logger();

		ChiakiLog log_;
		std::mutex mutex_;
		std::deque<std::string> lines_;
		std::function<void()> on_changed_;
		FILE *file_ = nullptr;
	};
}

#define PSX_LOGI(...) ::psx::Logger::Get().Info(::psx::Format(__VA_ARGS__))
#define PSX_LOGE(...) ::psx::Logger::Get().Error(::psx::Format(__VA_ARGS__))

namespace psx
{
	std::string Format(const char *fmt, ...);
	std::string Narrow(winrt::hstring const &s);
	winrt::hstring Widen(std::string const &s);
}
