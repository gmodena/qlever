// Copyright 2011 - 2024, University of Freiburg
// Chair of Algorithms and Data Structures
// Authors: Björn Buchhold <buchhold@cs.uni-freiburg.de> [2011 - 2014]
//          Johannes Kalmbach <bast@cs.uni-freiburg.de>
//          Hannah Bast <bast@cs.uni-freiburg.de>

#ifndef QLEVER_SRC_UTIL_LOG_H
#define QLEVER_SRC_UTIL_LOG_H

#include <absl/strings/str_format.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>

#include <iostream>
#include <locale>
#include <sstream>
#include <streambuf>
#include <string>

#include "backports/keywords.h"
#include "util/ConstexprMap.h"
#include "util/TypeTraits.h"
#include "util/json.h"

#ifndef LOGLEVEL
#define LOGLEVEL INFO
#endif

#define AD_LOG(x)   \
  if (x > LOGLEVEL) \
    ;               \
  else              \
    ad_utility::Log::getLog<x>()  // NOLINT

enum class LogLevel {
  FATAL = 0,
  ERROR = 1,
  WARN = 2,
  INFO = 3,
  DEBUG = 4,
  TIMING = 5,
  TRACE = 6
};

// Macros for the different log levels.
#define AD_LOG_FATAL AD_LOG(LogLevel::FATAL)
#define AD_LOG_ERROR AD_LOG(LogLevel::ERROR)
#define AD_LOG_WARN AD_LOG(LogLevel::WARN)
#define AD_LOG_INFO AD_LOG(LogLevel::INFO)
#define AD_LOG_DEBUG AD_LOG(LogLevel::DEBUG)
#define AD_LOG_TIMING AD_LOG(LogLevel::TIMING)
#define AD_LOG_TRACE AD_LOG(LogLevel::TRACE)

using enum LogLevel;

namespace ad_utility {
// A singleton that holds a pointer to a single `std::ostream`. This enables us
// to globally redirect the `AD_LOG_...` macros to another output stream.
struct LogstreamChoice {
  std::ostream& getStream() { return *_stream; }
  void setStream(std::ostream* stream) { _stream = stream; }

  static LogstreamChoice& get() {
    static LogstreamChoice s;
    return s;
  }  // instance
  LogstreamChoice(const LogstreamChoice&) = delete;
  LogstreamChoice& operator=(const LogstreamChoice&) = delete;

 private:
  LogstreamChoice() {}
  ~LogstreamChoice() {}

  // default to cout since it was the default before
  std::ostream* _stream = &std::cout;
  bool ecsMode_ = false;

 public:
  void setEcsMode(bool enabled) { ecsMode_ = enabled; }
  bool isEcsMode() const { return ecsMode_; }
};

// After this call, every use of `AD_LOG_...` will use the specified stream.
// Used in various tests to redirect or suppress logging output.
inline void setGlobalLoggingStream(std::ostream* stream) {
  LogstreamChoice::get().setStream(stream);
}

// Enable or disable ECS JSON formatting for all `AD_LOG_...` output.
inline void setEcsLogging(bool enabled) {
  LogstreamChoice::get().setEcsMode(enabled);
}

// ECS specification version used in all emitted JSON records.
inline constexpr std::string_view ECS_VERSION = "9.4.0";

// A std::streambuf that accumulates a single log entry and on sync() emits it
// as a one-line ECS JSON record to the LogstreamChoice stream.
class EcsLogBuffer : public std::streambuf {
 public:
  void startEntry(LogLevel level, std::string timestamp) {
    if (!buffer_.empty()) {
      emitEntry();
      buffer_.clear();
    }
    level_ = level;
    timestamp_ = std::move(timestamp);
  }

 protected:
  int overflow(int c) override {
    if (c != EOF) buffer_ += static_cast<char>(c);
    return c;
  }

  int sync() override {
    if (!buffer_.empty()) {
      emitEntry();
      buffer_.clear();
    }
    return 0;
  }

 private:
  static constexpr std::string_view levelToString(LogLevel level) {
    switch (level) {
      case TRACE:  return "TRACE";
      case TIMING: return "TIMING";
      case DEBUG:  return "DEBUG";
      case INFO:   return "INFO";
      case WARN:   return "WARN";
      case ERROR:  return "ERROR";
      case FATAL:  return "FATAL";
    }
    return "UNKNOWN";
  }

  void emitEntry() {
    auto end = buffer_.find_last_not_of("\n\r");
    if (end == std::string::npos) return;
    nlohmann::json j{
        {"@timestamp",  timestamp_},
        {"log.level",   std::string(levelToString(level_))},
        {"message",     buffer_.substr(0, end + 1)},
        {"ecs.version", ECS_VERSION},
    };
    LogstreamChoice::get().getStream() << j.dump() << "\n";
  }

  std::string buffer_;
  LogLevel level_ = INFO;
  std::string timestamp_;
};

// Helper class to get thousandth separators in a locale
class CommaNumPunct : public std::numpunct<char> {
 protected:
  virtual char do_thousands_sep() const { return ','; }

  virtual std::string do_grouping() const { return "\03"; }
};

const static std::locale commaLocale(std::locale(), new CommaNumPunct());

// The class that actually does the logging.
class Log {
 public:
  template <LogLevel LEVEL>
  static std::ostream& getLog() {
    if (LogstreamChoice::get().isEcsMode()) {
      thread_local EcsLogBuffer ecsBuf;
      thread_local std::ostream ecsStream(&ecsBuf);
      ecsBuf.startEntry(LEVEL, getTimeStamp());
      return ecsStream;
    }
    // use the singleton logging stream as target.
    return LogstreamChoice::get().getStream()
           << getTimeStamp() << " - " << getLevel<LEVEL>() << ": ";
  }

  static void imbue(const std::locale& locale) { std::cout.imbue(locale); }

  static std::string getTimeStamp() {
    return absl::FormatTime("%Y-%m-%d %H:%M:%E3S", absl::Now(),
                            absl::LocalTimeZone());
  }

  template <LogLevel LEVEL>
  static QL_CONSTEVAL std::string_view getLevel() {
    using P = ConstexprMapPair<LogLevel, std::string_view>;
    constexpr ConstexprMap map{std::array<P, 7>{
        P(TRACE, "TRACE"),
        P(TIMING, "TIMING"),
        P(DEBUG, "DEBUG"),
        P(INFO, "INFO"),
        P(WARN, "WARN"),
        P(ERROR, "ERROR"),
        P(FATAL, "FATAL"),
    }};
    return map.at(LEVEL);
  }
};
}  // namespace ad_utility

#endif  // QLEVER_SRC_UTIL_LOG_H
