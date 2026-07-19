#pragma once

#include <android/log.h>

namespace QuestVrDiagnostics {

void configurePersistentLog(const char* path);
void log(android_LogPriority priority, const char* tag, const char* format, ...)
        __attribute__((format(printf, 3, 4)));
long currentThreadId();

}  // namespace QuestVrDiagnostics
