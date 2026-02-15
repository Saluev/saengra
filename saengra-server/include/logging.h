#pragma once

#include <string>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

// ANSI color codes
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"
#define ANSI_ITALIC  "\033[3m"
#define ANSI_UNDERLINE "\033[4m"

// Basic colors
#define ANSI_BLACK   "\033[30m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_WHITE   "\033[37m"

// Bright colors
#define ANSI_BRIGHT_BLACK   "\033[90m"
#define ANSI_BRIGHT_RED     "\033[91m"
#define ANSI_BRIGHT_GREEN   "\033[92m"
#define ANSI_BRIGHT_YELLOW  "\033[93m"
#define ANSI_BRIGHT_BLUE    "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN    "\033[96m"
#define ANSI_BRIGHT_WHITE   "\033[97m"

// Helper macros for coloring values
#define BLACK(x)   fmt::format(ANSI_BLACK "{}" ANSI_RESET, (x))
#define RED(x)     fmt::format(ANSI_RED "{}" ANSI_RESET, (x))
#define GREEN(x)   fmt::format(ANSI_GREEN "{}" ANSI_RESET, (x))
#define YELLOW(x)  fmt::format(ANSI_YELLOW "{}" ANSI_RESET, (x))
#define BLUE(x)    fmt::format(ANSI_BLUE "{}" ANSI_RESET, (x))
#define MAGENTA(x) fmt::format(ANSI_MAGENTA "{}" ANSI_RESET, (x))
#define CYAN(x)    fmt::format(ANSI_CYAN "{}" ANSI_RESET, (x))
#define WHITE(x)   fmt::format(ANSI_WHITE "{}" ANSI_RESET, (x))

// Bright color helpers
#define BRIGHT_BLACK(x)   fmt::format(ANSI_BRIGHT_BLACK "{}" ANSI_RESET, (x))
#define BRIGHT_RED(x)     fmt::format(ANSI_BRIGHT_RED "{}" ANSI_RESET, (x))
#define BRIGHT_GREEN(x)   fmt::format(ANSI_BRIGHT_GREEN "{}" ANSI_RESET, (x))
#define BRIGHT_YELLOW(x)  fmt::format(ANSI_BRIGHT_YELLOW "{}" ANSI_RESET, (x))
#define BRIGHT_BLUE(x)    fmt::format(ANSI_BRIGHT_BLUE "{}" ANSI_RESET, (x))
#define BRIGHT_MAGENTA(x) fmt::format(ANSI_BRIGHT_MAGENTA "{}" ANSI_RESET, (x))
#define BRIGHT_CYAN(x)    fmt::format(ANSI_BRIGHT_CYAN "{}" ANSI_RESET, (x))
#define BRIGHT_WHITE(x)   fmt::format(ANSI_BRIGHT_WHITE "{}" ANSI_RESET, (x))

// Style helpers
#define BOLD(x)      fmt::format(ANSI_BOLD "{}" ANSI_RESET, (x))
#define DIM(x)       fmt::format(ANSI_DIM "{}" ANSI_RESET, (x))
#define ITALIC(x)    fmt::format(ANSI_ITALIC "{}" ANSI_RESET, (x))
#define UNDERLINE(x) fmt::format(ANSI_UNDERLINE "{}" ANSI_RESET, (x))
