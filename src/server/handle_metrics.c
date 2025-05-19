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
#include <unistd.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>
#elif defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
#endif

#if (defined(__APPLE__) && defined(__MACH__)) || defined(__FreeBSD__)
#include <sys/resource.h> // For getrusage()
#endif

// Forward declaration for c4_proofer_cache_stats if PROOFER_CACHE is defined
#ifdef PROOFER_CACHE
void c4_proofer_cache_stats(uint64_t* entries, uint64_t* size, uint64_t* max_size, uint64_t* capacity);
#endif

typedef struct {
  char*    name;
  uint64_t count;
  uint64_t total_size;
  uint64_t total_duration;
  uint64_t total_cached;
} method_entry_t;

typedef struct {
  uint64_t        total_requests; // total of number of requests
  uint64_t        total_errors;   // total of number of errors
  method_entry_t* entries;        // list of method entries
  size_t          entries_length; // length of the list of method entries
} methods_counts_t;

typedef struct {
  uint64_t page_faults_minor;        // Minor page faults
  uint64_t page_faults_major;        // Major page faults (or total on Windows)
  uint64_t ctx_switches_voluntary;   // Voluntary context switches
  uint64_t ctx_switches_involuntary; // Involuntary context switches
  uint64_t io_read_bytes;            // Bytes read from storage
  uint64_t io_written_bytes;         // Bytes written to storage
  uint64_t io_read_ops;              // Read operations
  uint64_t io_write_ops;             // Write operations
} process_platform_stats_t;

static methods_counts_t public_requests = {0};
static methods_counts_t eth_requests    = {0};
static methods_counts_t beacon_requests = {0};

void c4_metrics_add_request(data_request_type_t type, const char* method, uint64_t size, uint64_t duration, bool success, bool cached) {
  if (!method) return;
  char tmp[500];
  if (type == C4_DATA_TYPE_BEACON_API) {
    char* end = strchr(method, '?');
    if (!end) {
      int l = strlen(method);
      for (int i = l - 1; i >= 0; i--) {
        if (method[i] == '/') {
          if (method[i + 1] >= '0' && method[i + 1] <= '9') {
            end = method + i;
            break;
          }
        }
      }
    }
    if (end) {
      int l = end - method;
      if (l >= 500) l = 499;
      memcpy(tmp, method, l);
      tmp[l] = 0;
      method = tmp;
    }
  }
  methods_counts_t* metrics = NULL;
  switch (type) {
    case C4_DATA_TYPE_BEACON_API:
      metrics = &beacon_requests;
      break;
    case C4_DATA_TYPE_ETH_RPC:
      metrics = &eth_requests;
      break;
    default:
      metrics = &public_requests;
      break;
  }

  int index = -1;
  for (size_t i = 0; i < metrics->entries_length; i++) {
    if (strcmp(metrics->entries[i].name, method) == 0) {
      index = i;
      break;
    }
  }

  if (index == -1) {
    metrics->entries_length++;
    metrics->entries                                             = realloc(metrics->entries, metrics->entries_length * sizeof(method_entry_t));
    metrics->entries[metrics->entries_length - 1].name           = strdup(method);
    metrics->entries[metrics->entries_length - 1].count          = 0;
    metrics->entries[metrics->entries_length - 1].total_size     = 0;
    metrics->entries[metrics->entries_length - 1].total_duration = 0;
    metrics->entries[metrics->entries_length - 1].total_cached   = 0;
    index                                                        = metrics->entries_length - 1;
  }

  metrics->entries[index].count++;
  metrics->entries[index].total_size += size;
  metrics->entries[index].total_duration += duration;
  metrics->total_requests++;
  if (!success) metrics->total_errors++;
  if (cached) metrics->entries[index].total_cached++;
}
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

/**
 * Retrieves the total user and system CPU time consumed by the current process.
 * Times are returned in seconds.
 * Returns true on success, false on failure.
 */
static bool get_process_cpu_seconds(uint64_t* user_seconds, uint64_t* system_seconds) {
  if (!user_seconds || !system_seconds) {
    return false;
  }

#if defined(_WIN32)
  FILETIME creation_time, exit_time, kernel_time_ft, user_time_ft;
  if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time_ft, &user_time_ft)) {
    ULARGE_INTEGER kernel_ul, user_ul;
    kernel_ul.LowPart  = kernel_time_ft.dwLowDateTime;
    kernel_ul.HighPart = kernel_time_ft.dwHighDateTime;
    user_ul.LowPart    = user_time_ft.dwLowDateTime;
    user_ul.HighPart   = user_time_ft.dwHighDateTime;
    // Convert 100-nanosecond intervals to seconds
    *system_seconds = kernel_ul.QuadPart / 10000000ULL;
    *user_seconds   = user_ul.QuadPart / 10000000ULL;
    return true;
  }
  return false;

#elif defined(__APPLE__) && defined(__MACH__)
  struct mach_task_basic_info taskinfo;
  mach_msg_type_number_t      count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &taskinfo, &count) == KERN_SUCCESS) {
    *user_seconds = taskinfo.user_time.seconds;
    // Add microseconds for more precision if needed, but uint64_t for seconds is fine for Prometheus counters
    // *user_seconds   += (uint64_t)(taskinfo.user_time.microseconds / 1000000.0);
    *system_seconds = taskinfo.system_time.seconds;
    // *system_seconds += (uint64_t)(taskinfo.system_time.microseconds / 1000000.0);
    return true;
  }
  return false;

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
  FILE* fp = fopen("/proc/self/stat", "r");
  if (fp == NULL) {
    return false;
  }
  // Fields from proc(5) /proc/[pid]/stat:
  // (14) utime  %lu (user time, measured in clock ticks)
  // (15) stime  %lu (system time, measured in clock ticks)
  unsigned long utime_ticks, stime_ticks;
  // Scan for the required fields. The format string skips all preceding fields.
  // %*s consumes a string, %*d a decimal, %*c a char, %*u an unsigned decimal.
  int items = fscanf(fp, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime_ticks, &stime_ticks);
  fclose(fp);
  if (items == 2) {
    long clock_ticks_per_second = sysconf(_SC_CLK_TCK);
    if (clock_ticks_per_second <= 0) return false; // Should not happen
    *user_seconds   = utime_ticks / clock_ticks_per_second;
    *system_seconds = stime_ticks / clock_ticks_per_second;
    return true;
  }
  return false;

#elif defined(__FreeBSD__)
  struct rusage rusage_self;
  if (getrusage(RUSAGE_SELF, &rusage_self) == 0) {
    *user_seconds = rusage_self.ru_utime.tv_sec;
    // *user_seconds += rusage_self.ru_utime.tv_usec / 1000000ULL;
    *system_seconds = rusage_self.ru_stime.tv_sec;
    // *system_seconds += rusage_self.ru_stime.tv_usec / 1000000ULL;
    return true;
  }
  return false;

#else
  /* Unknown OS - return 0 or handle error */
  *user_seconds   = 0;
  *system_seconds = 0;
  return false; // Unsupported
#endif
}

static void c4_write_process_cpu_metrics(buffer_t* data) {
  uint64_t cpu_user_seconds   = 0;
  uint64_t cpu_system_seconds = 0;
  if (get_process_cpu_seconds(&cpu_user_seconds, &cpu_system_seconds)) {
    bprintf(data, "# HELP colibri_process_cpu_user_seconds_total Total CPU time spent in user mode by the process.\n");
    bprintf(data, "# TYPE colibri_process_cpu_user_seconds_total counter\n");
    bprintf(data, "colibri_process_cpu_user_seconds_total %l\n\n", cpu_user_seconds);

    bprintf(data, "# HELP colibri_process_cpu_system_seconds_total Total CPU time spent in system mode by the process.\n");
    bprintf(data, "# TYPE colibri_process_cpu_system_seconds_total counter\n");
    bprintf(data, "colibri_process_cpu_system_seconds_total %l\n\n", cpu_system_seconds);
  }
}

static bool get_process_platform_stats(process_platform_stats_t* stats) {
  if (!stats) return false;
  memset(stats, 0, sizeof(process_platform_stats_t)); // Initialize all to 0

#if defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
  // --- Page Faults (from /proc/self/stat) ---
  // Re-use part of the logic from get_process_cpu_seconds for /proc/self/stat
  FILE* fp_stat = fopen("/proc/self/stat", "r");
  if (fp_stat) {
    // Fields from proc(5) /proc/[pid]/stat:
    // (10) minflt  %lu (Minor faults)
    // (12) majflt  %lu (Major faults)
    // (14) utime   %lu (user time)
    // (15) stime   %lu (system time)
    // We need to skip fields carefully. pid (1), comm (2) %s, state (3) %c, ppid (4), pgrp (5), session (6), tty_nr (7), tpgid (8), flags (9) %u
    // then minflt (10) %lu, cminflt (11) %lu, majflt (12) %lu, cmajflt (13) %lu, utime (14) %lu, stime (15) %lu
    unsigned long min_flt, maj_flt;
    // The following fscanf will also read utime and stime, but we ignore them here as get_process_cpu_seconds handles them.
    // We are interested in fields 10 and 12 for faults.
    int items = fscanf(fp_stat, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %lu %*u %lu", &min_flt, &maj_flt);
    fclose(fp_stat);
    if (items == 2) {
      stats->page_faults_minor = min_flt;
      stats->page_faults_major = maj_flt;
    }
    else {
      // Failed to parse, but don't make the whole function fail, some other stats might be available.
    }
  }

  // --- Context Switches (from /proc/self/status) ---
  FILE* fp_status = fopen("/proc/self/status", "r");
  if (fp_status) {
    char line[256];
    while (fgets(line, sizeof(line), fp_status)) {
      if (strncmp(line, "voluntary_ctxt_switches:", 24) == 0) {
        sscanf(line + 24, "%lu", &stats->ctx_switches_voluntary);
      }
      if (strncmp(line, "nonvoluntary_ctxt_switches:", 27) == 0) {
        sscanf(line + 27, "%lu", &stats->ctx_switches_involuntary);
      }
    }
    fclose(fp_status);
  }

  // --- I/O Stats (from /proc/self/io) ---
  FILE* fp_io = fopen("/proc/self/io", "r");
  if (fp_io) {
    char line[256];
    while (fgets(line, sizeof(line), fp_io)) {
      if (strncmp(line, "rchar:", 6) == 0) { // Bytes read (from storage)
        sscanf(line + 6, "%lu", &stats->io_read_bytes);
      }
      else if (strncmp(line, "wchar:", 6) == 0) { // Bytes written (to storage)
        sscanf(line + 6, "%lu", &stats->io_written_bytes);
      }
      else if (strncmp(line, "syscr:", 6) == 0) { // Read syscalls
        sscanf(line + 6, "%lu", &stats->io_read_ops);
      }
      else if (strncmp(line, "syscw:", 6) == 0) { // Write syscalls
        sscanf(line + 6, "%lu", &stats->io_write_ops);
      }
    }
    fclose(fp_io);
  }
  return true; // Indicate success for Linux if files were processed, even if some parts failed.

#elif (defined(__APPLE__) && defined(__MACH__)) || defined(__FreeBSD__)
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    stats->page_faults_minor        = usage.ru_minflt;
    stats->page_faults_major        = usage.ru_majflt;
    stats->ctx_switches_voluntary   = usage.ru_nvcsw;
    stats->ctx_switches_involuntary = usage.ru_nivcsw;
    // getrusage provides block operations, not byte counts directly for I/O.
    // Map block operations to our _ops fields.
    stats->io_read_ops  = usage.ru_inblock;
    stats->io_write_ops = usage.ru_oublock;
    // stats->io_read_bytes and stats->io_written_bytes remain 0 unless a different method is used.
    return true;
  }
  return false;

#elif defined(_WIN32)
  // Page Faults
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    stats->page_faults_major = pmc.PageFaultCount; // Total page faults
    // stats->page_faults_minor remains 0 as Windows provides a combined count here.
  } // Continue even if this fails, to try and get I/O counters

  // I/O Counters
  IO_COUNTERS io_counters;
  if (GetProcessIoCounters(GetCurrentProcess(), &io_counters)) {
    stats->io_read_ops      = io_counters.ReadOperationCount;
    stats->io_write_ops     = io_counters.WriteOperationCount;
    stats->io_read_bytes    = io_counters.ReadTransferCount;
    stats->io_written_bytes = io_counters.WriteTransferCount;
  }
  // Context switches are not easily available, they will remain 0.
  return true; // Return true if we attempted to get the stats for Windows

#else
  return false; // Unsupported OS
#endif
}

static void c4_write_prometheus_bucket_metrics(buffer_t*         data,
                                               methods_counts_t* metrics,
                                               const char*       bucket_type_name,
                                               const char*       bucket_description_prefix,
                                               bool*             method_metrics_described_flag) {
  // --- {Bucket Type Name} Requests ---
  bprintf(data, "# HELP colibri_%s_requests_total Total number of %s requests.\n", bucket_type_name, bucket_description_prefix);
  bprintf(data, "# TYPE colibri_%s_requests_total counter\n", bucket_type_name);
  bprintf(data, "colibri_%s_requests_total %l\n", bucket_type_name, metrics->total_requests);

  bprintf(data, "# HELP colibri_%s_errors_total Total number of errors for %s requests.\n", bucket_type_name, bucket_description_prefix);
  bprintf(data, "# TYPE colibri_%s_errors_total counter\n", bucket_type_name);
  bprintf(data, "colibri_%s_errors_total %l\n", bucket_type_name, metrics->total_errors);

  // Calculate sum of cached entries for the bucket
  uint64_t bucket_total_cached = 0;
  for (size_t i = 0; i < metrics->entries_length; i++) {
    bucket_total_cached += metrics->entries[i].total_cached;
  }
  bprintf(data, "# HELP colibri_%s_cached_total Total number of cached %s requests.\n", bucket_type_name, bucket_description_prefix);
  bprintf(data, "# TYPE colibri_%s_cached_total counter\n", bucket_type_name);
  bprintf(data, "colibri_%s_cached_total %l\n\n", bucket_type_name, bucket_total_cached);

  if (metrics->entries_length > 0) {
    if (!(*method_metrics_described_flag)) {
      bprintf(data, "# HELP colibri_method_requests_total Total number of requests for a specific API method, partitioned by type (public, eth, beacon).\n");
      bprintf(data, "# TYPE colibri_method_requests_total counter\n");
      bprintf(data, "# HELP colibri_method_duration_milliseconds_total Total duration in milliseconds spent processing a specific API method, partitioned by type.\n");
      bprintf(data, "# TYPE colibri_method_duration_milliseconds_total counter\n");
      bprintf(data, "# HELP colibri_method_size_bytes_total Total response size in bytes for a specific API method, partitioned by type.\n");
      bprintf(data, "# TYPE colibri_method_size_bytes_total counter\n");
      bprintf(data, "# HELP colibri_method_cached_total Total number of cached requests for a specific API method, partitioned by type.\n");
      bprintf(data, "# TYPE colibri_method_cached_total counter\n\n");
      *method_metrics_described_flag = true;
    }
    for (size_t i = 0; i < metrics->entries_length; i++) {
      // TODO: Methodennamen escapen, falls sie Sonderzeichen enthalten können, die von Prometheus nicht erlaubt sind.
      // Für einfache Methodennamen ist dies normalerweise nicht erforderlich.
      bprintf(data, "colibri_method_requests_total{type=\"%s\",method=\"%s\"} %l\n", bucket_type_name, metrics->entries[i].name, metrics->entries[i].count);
      bprintf(data, "colibri_method_duration_milliseconds_total{type=\"%s\",method=\"%s\"} %l\n", bucket_type_name, metrics->entries[i].name, metrics->entries[i].total_duration);
      bprintf(data, "colibri_method_size_bytes_total{type=\"%s\",method=\"%s\"} %l\n", bucket_type_name, metrics->entries[i].name, metrics->entries[i].total_size);
      bprintf(data, "colibri_method_cached_total{type=\"%s\",method=\"%s\"} %l\n", bucket_type_name, metrics->entries[i].name, metrics->entries[i].total_cached);
    }
    bprintf(data, "\n"); // Leerzeile nach den Methoden dieses Typs
  }
}

bool c4_handle_metrics(client_t* client) {
  const char* path = "/metrics";
  if (strncmp(client->request.path, path, strlen(path)) != 0) return false;

  buffer_t data                     = {0};
  size_t   current_rss              = get_current_rss();
  bool     method_metrics_described = false; // Flag, um HELP/TYPE für Methoden-Metriken nur einmal zu schreiben
#ifdef PROOFER_CACHE
  uint64_t entries = 0, size = 0, max_size = 0, capacity = 0;
  c4_proofer_cache_stats(&entries, &size, &max_size, &capacity);

  bprintf(&data, "# HELP colibri_proofer_cache_entries Current number of entries in the proofer cache.\n");
  bprintf(&data, "# TYPE colibri_proofer_cache_entries gauge\n");
  bprintf(&data, "colibri_proofer_cache_entries %l\n", entries);
  bprintf(&data, "# HELP colibri_proofer_cache_size Current size of the proofer cache in bytes.\n");
  bprintf(&data, "# TYPE colibri_proofer_cache_size gauge\n");
  bprintf(&data, "colibri_proofer_cache_size %l\n", size);
  bprintf(&data, "# HELP colibri_proofer_cache_max_size Maximum size of the proofer cache in bytes.\n");
  bprintf(&data, "# TYPE colibri_proofer_cache_max_size gauge\n");
  bprintf(&data, "colibri_proofer_cache_max_size %l\n", max_size);
  bprintf(&data, "# HELP colibri_proofer_cache_capacity Maximum capacity of the proofer cache in bytes.\n");
  bprintf(&data, "# TYPE colibri_proofer_cache_capacity gauge\n");
  bprintf(&data, "colibri_proofer_cache_capacity %l\n", capacity);
#endif
  // RSS Metrik
  bprintf(&data, "# HELP colibri_process_resident_memory_bytes Current resident set size (RSS) of the process in bytes.\n");
  bprintf(&data, "# TYPE colibri_process_resident_memory_bytes gauge\n");
  bprintf(&data, "colibri_process_resident_memory_bytes %l\n\n", (uint64_t) current_rss);

  // CPU Time Metriken
  c4_write_process_cpu_metrics(&data);

  // Erweiterte Prozess-Statistiken
  process_platform_stats_t platform_stats = {0}; // Initialisieren mit Nullen
  if (get_process_platform_stats(&platform_stats)) {
    // Page Faults
    bprintf(&data, "# HELP colibri_process_page_faults_minor_total Minor page faults.\n");
    bprintf(&data, "# TYPE colibri_process_page_faults_minor_total counter\n");
    bprintf(&data, "colibri_process_page_faults_minor_total %l\n\n", platform_stats.page_faults_minor);
    bprintf(&data, "# HELP colibri_process_page_faults_major_total Major page faults (or total page faults on some OS).\n");
    bprintf(&data, "# TYPE colibri_process_page_faults_major_total counter\n");
    bprintf(&data, "colibri_process_page_faults_major_total %l\n\n", platform_stats.page_faults_major);

    // Context Switches
    bprintf(&data, "# HELP colibri_process_context_switches_voluntary_total Voluntary context switches.\n");
    bprintf(&data, "# TYPE colibri_process_context_switches_voluntary_total counter\n");
    bprintf(&data, "colibri_process_context_switches_voluntary_total %l\n\n", platform_stats.ctx_switches_voluntary);
    bprintf(&data, "# HELP colibri_process_context_switches_involuntary_total Involuntary context switches.\n");
    bprintf(&data, "# TYPE colibri_process_context_switches_involuntary_total counter\n");
    bprintf(&data, "colibri_process_context_switches_involuntary_total %l\n\n", platform_stats.ctx_switches_involuntary);

    // I/O Bytes
    bprintf(&data, "# HELP colibri_process_io_read_bytes_total Bytes read by the process.\n");
    bprintf(&data, "# TYPE colibri_process_io_read_bytes_total counter\n");
    bprintf(&data, "colibri_process_io_read_bytes_total %l\n\n", platform_stats.io_read_bytes);
    bprintf(&data, "# HELP colibri_process_io_written_bytes_total Bytes written by the process.\n");
    bprintf(&data, "# TYPE colibri_process_io_written_bytes_total counter\n");
    bprintf(&data, "colibri_process_io_written_bytes_total %l\n\n", platform_stats.io_written_bytes);

    // I/O Operations
    bprintf(&data, "# HELP colibri_process_io_read_operations_total Read operations performed by the process.\n");
    bprintf(&data, "# TYPE colibri_process_io_read_operations_total counter\n");
    bprintf(&data, "colibri_process_io_read_operations_total %l\n\n", platform_stats.io_read_ops);
    bprintf(&data, "# HELP colibri_process_io_write_operations_total Write operations performed by the process.\n");
    bprintf(&data, "# TYPE colibri_process_io_write_operations_total counter\n");
    bprintf(&data, "colibri_process_io_write_operations_total %l\n\n", platform_stats.io_write_ops);
  }

  // Libuv Metriken
  // Hinweis: Damit metrics_idle_time einen Wert liefert, muss der Loop mit
  // uv_loop_configure(loop, UV_METRICS_IDLE_TIME, 1); konfiguriert worden sein.
  uv_loop_t* loop = uv_default_loop(); // Annahme: Verwendung des Default-Loops
  if (loop) {
    uint64_t idle_time_ns = uv_metrics_idle_time(loop);
    bprintf(&data, "# HELP colibri_libuv_idle_time_nanoseconds Time the event loop spent idle in the last report interval (nanoseconds).\n");
    bprintf(&data, "# TYPE colibri_libuv_idle_time_nanoseconds gauge\n");
    bprintf(&data, "colibri_libuv_idle_time_nanoseconds %l\n\n", idle_time_ns);

    // Weitere mögliche libuv Metriken (als Info für Sie):
    // int timeout = uv_backend_timeout(loop);
    // bprintf(&data, "# INFO colibri_libuv_backend_timeout_milliseconds %d\n", timeout);

    // uv_metrics_info_t metrics_info;
    // if (uv_metrics_info(loop, &metrics_info) == 0) {
    //   bprintf(&data, "# INFO colibri_libuv_loop_count %l\n", metrics_info.loop_count);
    //   bprintf(&data, "# INFO colibri_libuv_events_waiting %l\n", metrics_info.events_waiting);
    // }
  }
  else {
    // Fallback oder Warnung, falls der Default-Loop nicht verfügbar ist (sollte nicht passieren, wenn libuv genutzt wird)
    bprintf(&data, "# WARN libuv default_loop not available\n");
  }

  // Public Requests
  c4_write_prometheus_bucket_metrics(&data, &public_requests, "public", "public (e.g. eth_getTransactionByHash)", &method_metrics_described);

  // Eth Requests
  c4_write_prometheus_bucket_metrics(&data, &eth_requests, "eth", "ETH JSON-RPC (e.g. eth_getBlockByNumber)", &method_metrics_described);

  // Beacon Requests
  c4_write_prometheus_bucket_metrics(&data, &beacon_requests, "beacon", "Beacon API (e.g. /eth/v1/beacon/genesis)", &method_metrics_described);

  c4_http_respond(client, 200, "text/plain; version=0.0.4; charset=utf-8", data.data);
  buffer_free(&data);
  return true;
}
