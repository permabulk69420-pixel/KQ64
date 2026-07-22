#include "QuestVrDiagnostics.h"

#include <jni.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

namespace QuestVrDiagnostics {
namespace {

std::mutex gLogMutex;
FILE* gLogFile = nullptr;
std::string gLogPath;

char priorityName(android_LogPriority priority) {
    switch (priority) {
        case ANDROID_LOG_ERROR: return 'E';
        case ANDROID_LOG_WARN: return 'W';
        case ANDROID_LOG_DEBUG: return 'D';
        case ANDROID_LOG_VERBOSE: return 'V';
        default: return 'I';
    }
}

}  // namespace

long currentThreadId() {
    return static_cast<long>(syscall(SYS_gettid));
}

void configurePersistentLog(const char* path) {
    {
        std::lock_guard<std::mutex> lock(gLogMutex);
        if (gLogFile != nullptr) {
            std::fclose(gLogFile);
            gLogFile = nullptr;
        }
        gLogPath = path == nullptr ? "" : path;
        if (!gLogPath.empty()) {
            gLogFile = std::fopen(gLogPath.c_str(), "a");
            if (gLogFile != nullptr) {
                std::setvbuf(gLogFile, nullptr, _IOLBF, 0);
            }
        }
    }

    if (gLogFile == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "M64P-QuestVR",
                            "Unable to open persistent diagnostic log: %s",
                            path == nullptr ? "(null)" : path);
    } else {
        log(ANDROID_LOG_INFO, "M64P-QuestVR",
            "Native persistent diagnostics attached to %s", gLogPath.c_str());
    }
}

void log(android_LogPriority priority, const char* tag, const char* format, ...) {
    char message[4096];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    __android_log_write(priority, tag, message);

    std::lock_guard<std::mutex> lock(gLogMutex);
    if (gLogFile == nullptr) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&time, &localTime);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &localTime);

    std::fprintf(gLogFile, "%s.%03lld %c pid=%d tid=%ld [%s] %s\n",
                 timestamp, static_cast<long long>(milliseconds.count()),
                 priorityName(priority), static_cast<int>(getpid()), currentThreadId(),
                 tag, message);
    std::fflush(gLogFile);
}

}  // namespace QuestVrDiagnostics

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeConfigureDiagnosticLog(
        JNIEnv* env, jclass, jstring path) {
    if (path == nullptr) {
        QuestVrDiagnostics::configurePersistentLog(nullptr);
        return;
    }
    const char* value = env->GetStringUTFChars(path, nullptr);
    QuestVrDiagnostics::configurePersistentLog(value);
    env->ReleaseStringUTFChars(path, value);
}
