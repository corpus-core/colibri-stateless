#include "beacon.h"
#include "logger.h"
#include "server.h"

#if defined(_WIN32)
#include <psapi.h>
#include <windows.h>
#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>
#elif defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#endif

/**
 * Returns the current resident set size (RSS) of the process in bytes.
 * Returns 0 on failure.
 */
size_t get_current_rss(void) {
#if defined(_WIN32)
  /* Windows -------------------------------------------------- */
  PROCESS_MEMORY_COUNTERS info;
  GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
  return (size_t) info.WorkingSetSize;

#elif defined(__APPLE__) && defined(__MACH__)
  /* OSX ------------------------------------------------------ */
  struct mach_task_basic_info info;
  mach_msg_type_number_t      infoCount = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &infoCount) != KERN_SUCCESS)
    return (size_t) 0L; /* Can't access? */
  return (size_t) info.resident_size;

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
  /* Linux ---------------------------------------------------- */
  long  rss = 0L;
  FILE* fp  = NULL;
  if ((fp = fopen("/proc/self/statm", "r")) == NULL)
    return (size_t) 0L; /* Can't open? */
  if (fscanf(fp, "%*s%ld", &rss) != 1) {
    fclose(fp);
    return (size_t) 0L; /* Can't read? */
  }
  fclose(fp);
  return (size_t) rss * (size_t) sysconf(_SC_PAGESIZE);

#elif defined(__FreeBSD__)
  /* FreeBSD -------------------------------------------------- */
  struct kinfo_proc info;
  size_t            infolen = sizeof(info);
  int               mib[4];
  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PID;
  mib[3] = getpid();
  if (sysctl(mib, 4, &info, &infolen, NULL, 0) != 0)
    return (size_t) 0L; /* Can't access? */
  return (size_t) info.ki_rssize * (size_t) getpagesize();

#else
  /* Unknown OS ----------------------------------------------- */
  return (size_t) 0L; /* Unsupported. */
#endif
}

bool c4_handle_status(client_t* client) {
  buffer_t data        = {0};
  uint64_t now         = current_ms();
  size_t   current_rss = get_current_rss();
  bprintf(&data,
          "{\"status\":\"ok\", "
          "\"stats\":{" // Start of stats object
          "\"total_requests\":%l, "
          "\"total_errors\":%l, "
          "\"last_sync_event\":%l, "
          "\"last_request_time\":%l, "
          "\"open_requests\":%l, "
          "\"memory\":%l}}", // End of stats object and main object
          http_server.stats.total_requests, http_server.stats.total_errors,
          (now - http_server.stats.last_sync_event) / 1000,
          (now - http_server.stats.last_request_time) / 1000,
          http_server.stats.open_requests, (uint64_t) current_rss);
  c4_http_respond(client, 200, "application/json", data.data);
  buffer_free(&data);
  return true;
}
