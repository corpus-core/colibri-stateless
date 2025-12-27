/*
 * Copyright 2025 corpus.core
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "beacon.h"
#include "logger.h"
#include "server.h"
#include "server_handlers.h"

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

// Forward declaration for c4_prover_cache_stats if PROVER_CACHE is defined
#ifdef PROVER_CACHE
void c4_prover_cache_stats(uint64_t* entries, uint64_t* size, uint64_t* max_size, uint64_t* capacity);
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
static methods_counts_t rest_requests   = {0};

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
    case C4_DATA_TYPE_REST_API:
      metrics = &rest_requests;
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

// Helper function to extract clean server name from URL
const char* c4_extract_server_name(const char* url) {
  if (!url) return "unknown";

  // Remove http:// or https:// prefix
  const char* start = url;
  if (strncmp(url, "https://", 8) == 0) {
    start = url + 8;
  }
  else if (strncmp(url, "http://", 7) == 0) {
    start = url + 7;
  }

  // Find first slash to truncate path
  const char* slash = strchr(start, '/');
  if (slash) {
    // Create a static buffer for the result (thread-safe for single-threaded metrics)
    static char server_name[256];
    size_t      len = slash - start;
    if (len >= sizeof(server_name)) len = sizeof(server_name) - 1;
    memcpy(server_name, start, len);
    server_name[len] = '\0';
    return server_name;
  }

  // No slash found, return everything after protocol
  return start;
}

static void c4_write_server_type_metrics(buffer_t* data, data_request_type_t type, const char* type_name) {
  server_list_t* servers = c4_get_server_list(type);
  if (!servers || !servers->health_stats || servers->count == 0) return;

  for (size_t i = 0; i < servers->count; i++) {
    server_health_t* health      = &servers->health_stats[i];
    const char*      server_name = c4_extract_server_name(servers->urls[i]);

    // Calculate derived metrics
    double success_rate      = health->total_requests > 0 ? (double) health->successful_requests / health->total_requests : 0.0;
    double avg_response_time = health->successful_requests > 0 ? (double) health->total_response_time / health->successful_requests : 0.0;

    // Server health status (1 = healthy, 0 = unhealthy)
    bprintf(data, "colibri_server_healthy{type=\"%s\",server=\"%s\",index=\"%l\"} %d\n",
            type_name, server_name, i, health->is_healthy ? 1 : 0);

    // Server weight for load balancing
    bprintf(data, "colibri_server_weight{type=\"%s\",server=\"%s\",index=\"%l\"} %f\n",
            type_name, server_name, i, health->weight);

    // Inflight and max concurrency
    bprintf(data, "colibri_server_inflight{type=\"%s\",server=\"%s\",index=\"%l\"} %l\n",
            type_name, server_name, i, (uint64_t) health->inflight);
    bprintf(data, "colibri_server_max_concurrency{type=\"%s\",server=\"%s\",index=\"%l\"} %l\n",
            type_name, server_name, i, (uint64_t) health->max_concurrency);

    // EWMA latency
    bprintf(data, "colibri_server_ewma_latency_ms{type=\"%s\",server=\"%s\",index=\"%l\"} %f\n",
            type_name, server_name, i, health->ewma_latency_ms);

    // Capacity factor (derived)
    uint32_t max_c      = health->max_concurrency > 0 ? health->max_concurrency : 1;
    uint32_t infl       = health->inflight;
    double   cap_factor = ((double) ((max_c > infl ? (max_c - infl) : 0) + 1)) / ((double) (max_c + 1));
    bprintf(data, "colibri_server_capacity_factor{type=\"%s\",server=\"%s\",index=\"%l\"} %f\n",
            type_name, server_name, i, cap_factor);

    // Recent rate-limit flag
    bprintf(data, "colibri_server_rate_limited_recent{type=\"%s\",server=\"%s\",index=\"%l\"} %d\n",
            type_name, server_name, i, health->rate_limited_recent ? 1 : 0);

    // Success rate (0.0 to 1.0)
    bprintf(data, "colibri_server_success_rate{type=\"%s\",server=\"%s\",index=\"%l\"} %f\n",
            type_name, server_name, i, success_rate);

    // Average response time in milliseconds
    bprintf(data, "colibri_server_avg_response_time_ms{type=\"%s\",server=\"%s\",index=\"%l\"} %f\n",
            type_name, server_name, i, avg_response_time);

    // Consecutive failures count
    bprintf(data, "colibri_server_consecutive_failures{type=\"%s\",server=\"%s\",index=\"%l\"} %l\n",
            type_name, server_name, i, health->consecutive_failures);

    // Total requests to this server
    bprintf(data, "colibri_server_total_requests{type=\"%s\",server=\"%s\",index=\"%l\"} %l\n",
            type_name, server_name, i, health->total_requests);

    // Successful requests to this server
    bprintf(data, "colibri_server_successful_requests{type=\"%s\",server=\"%s\",index=\"%l\"} %l\n",
            type_name, server_name, i, health->successful_requests);

    // Recovery status (1 = recovery allowed, 0 = recovery blocked)
    bprintf(data, "colibri_server_recovery_allowed{type=\"%s\",server=\"%s\",index=\"%l\"} %d\n",
            type_name, server_name, i, health->recovery_allowed ? 1 : 0);

    // Time since last use in milliseconds
    uint64_t time_since_last_use = current_ms() - health->last_used;
    bprintf(data, "colibri_server_last_used_ms_ago{type=\"%s\",server=\"%s\",index=\"%l\"} %l\n",
            type_name, server_name, i, time_since_last_use);

    // Time marked unhealthy (0 if healthy)
    uint64_t unhealthy_duration = health->is_healthy ? 0 : (health->marked_unhealthy_at > 0 ? current_ms() - health->marked_unhealthy_at : 0);
    bprintf(data, "colibri_server_unhealthy_duration_ms{type=\"%s\",server=\"%s\",index=\"%l\"} %l\n",
            type_name, server_name, i, unhealthy_duration);

    // Client type information for this server
    uint32_t    client_type = servers->client_types ? servers->client_types[i] : 0;
    const char* client_name = c4_client_type_to_name(client_type, &http_server); // Pass NULL as we don't have access to http_server here

    // Bitmask value (for compatibility)
    bprintf(data, "colibri_server_client_type{type=\"%s\",server=\"%s\",index=\"%l\"} %d\n",
            type_name, server_name, i, client_type);

    // Human-readable client type (1=detected, 0=unknown/not detected)
    bprintf(data, "colibri_server_client_info{type=\"%s\",server=\"%s\",index=\"%l\",client=\"%s\"} %d\n",
            type_name, server_name, i, client_name, client_type != 0 ? 1 : 0);

    // Estimated head (only meaningful for ETH RPC)
    if (strcmp(type_name, "eth") == 0) {
      bprintf(data, "colibri_server_estimated_head{type=\"%s\",server=\"%s\",index=\"%l\"} %l\n",
              type_name, server_name, i, health->latest_block);
    }
  }
}

static void c4_write_server_health_metrics(buffer_t* data) {
  // Write help and type information once
  bprintf(data, "# HELP colibri_server_healthy Server health status (1=healthy, 0=unhealthy).\n");
  bprintf(data, "# TYPE colibri_server_healthy gauge\n");
  bprintf(data, "# HELP colibri_server_weight Current load balancing weight for the server.\n");
  bprintf(data, "# TYPE colibri_server_weight gauge\n");
  bprintf(data, "# HELP colibri_server_success_rate Success rate of requests to the server (0.0-1.0).\n");
  bprintf(data, "# TYPE colibri_server_success_rate gauge\n");
  bprintf(data, "# HELP colibri_server_avg_response_time_ms Average response time in milliseconds.\n");
  bprintf(data, "# TYPE colibri_server_avg_response_time_ms gauge\n");
  bprintf(data, "# HELP colibri_server_consecutive_failures Number of consecutive failures.\n");
  bprintf(data, "# TYPE colibri_server_consecutive_failures gauge\n");
  bprintf(data, "# HELP colibri_server_total_requests Total number of requests sent to the server.\n");
  bprintf(data, "# TYPE colibri_server_total_requests counter\n");
  bprintf(data, "# HELP colibri_server_successful_requests Total number of successful requests to the server.\n");
  bprintf(data, "# TYPE colibri_server_successful_requests counter\n");
  bprintf(data, "# HELP colibri_server_recovery_allowed Whether the server is allowed to recover (1=yes, 0=no).\n");
  bprintf(data, "# TYPE colibri_server_recovery_allowed gauge\n");
  bprintf(data, "# HELP colibri_server_last_used_ms_ago Time since server was last used in milliseconds.\n");
  bprintf(data, "# TYPE colibri_server_last_used_ms_ago gauge\n");
  bprintf(data, "# HELP colibri_server_unhealthy_duration_ms Time the server has been unhealthy in milliseconds.\n");
  bprintf(data, "# TYPE colibri_server_unhealthy_duration_ms gauge\n");
  bprintf(data, "# HELP colibri_server_client_type Client type bitmask for the server (0=unknown).\n");
  bprintf(data, "# TYPE colibri_server_client_type gauge\n");
  bprintf(data, "# HELP colibri_server_client_info Client type information with human-readable name as label (1=detected, 0=unknown).\n");
  bprintf(data, "# TYPE colibri_server_client_info gauge\n");

  // Write metrics for both server types
  c4_write_server_type_metrics(data, C4_DATA_TYPE_ETH_RPC, "eth");
  c4_write_server_type_metrics(data, C4_DATA_TYPE_BEACON_API, "beacon");

  bprintf(data, "\n");
}

// Aggregates and prints metrics about RPC methods marked as unsupported per server
static void c4_write_unsupported_method_metrics(buffer_t* data) {
  server_list_t* servers = c4_get_server_list(C4_DATA_TYPE_ETH_RPC);
  if (!servers || !servers->health_stats || servers->count == 0) return;

  // Describe metrics once
  bprintf(data, "# HELP colibri_method_unsupported_servers Number of servers that marked the RPC method as unsupported.\n");
  bprintf(data, "# TYPE colibri_method_unsupported_servers gauge\n");
  bprintf(data, "# HELP colibri_server_method_unsupported Whether a specific server marked the RPC method as unsupported (1=yes, 0=no).\n");
  bprintf(data, "# TYPE colibri_server_method_unsupported gauge\n");

  // Simple dynamic array for aggregating counts
  typedef struct {
    char*    name;
    uint64_t count;
  } unsupported_count_t;
  unsupported_count_t* agg   = NULL;
  size_t               agg_n = 0;

  // First pass: aggregate counts per method
  for (size_t i = 0; i < servers->count; i++) {
    method_support_t* cur = servers->health_stats[i].unsupported_methods;
    while (cur) {
      if (!cur->is_supported && cur->method_name) {
        // Find existing entry
        size_t idx = (size_t) -1;
        for (size_t k = 0; k < agg_n; k++) {
          if (strcmp(agg[k].name, cur->method_name) == 0) {
            idx = k;
            break;
          }
        }
        if (idx == (size_t) -1) {
          agg              = (unsupported_count_t*) realloc(agg, (agg_n + 1) * sizeof(unsupported_count_t));
          agg[agg_n].name  = cur->method_name; // only reference, not owning
          agg[agg_n].count = 1;
          agg_n++;
        }
        else {
          agg[idx].count++;
        }
      }
      cur = cur->next;
    }
  }

  // Output aggregate metrics
  for (size_t k = 0; k < agg_n; k++) {
    bprintf(data, "colibri_method_unsupported_servers{method=\"%s\"} %l\n", agg[k].name, agg[k].count);
  }

  // Per-server listing (1 when method is marked unsupported)
  for (size_t i = 0; i < servers->count; i++) {
    const char*       server_name = c4_extract_server_name(servers->urls[i]);
    method_support_t* cur         = servers->health_stats[i].unsupported_methods;
    while (cur) {
      if (!cur->is_supported && cur->method_name) {
        bprintf(data, "colibri_server_method_unsupported{server=\"%s\",method=\"%s\"} 1\n", server_name, cur->method_name);
      }
      cur = cur->next;
    }
  }

  if (agg) free(agg);

  bprintf(data, "\n");
}

// Prints global cURL pool configuration and runtime metrics
static void c4_write_curl_metrics(buffer_t* data) {
  bprintf(data, "# HELP colibri_curl_pool_max_host Max connections per host.\n");
  bprintf(data, "# TYPE colibri_curl_pool_max_host gauge\n");
  bprintf(data, "colibri_curl_pool_max_host %d\n", http_server.curl.pool_max_host);

  bprintf(data, "# HELP colibri_curl_pool_max_total Max total connections.\n");
  bprintf(data, "# TYPE colibri_curl_pool_max_total gauge\n");
  bprintf(data, "colibri_curl_pool_max_total %d\n", http_server.curl.pool_max_total);

  bprintf(data, "# HELP colibri_curl_pool_maxconnects Connection cache size (hint).\n");
  bprintf(data, "# TYPE colibri_curl_pool_maxconnects gauge\n");
  bprintf(data, "colibri_curl_pool_maxconnects %d\n", http_server.curl.pool_maxconnects);

  bprintf(data, "# HELP colibri_curl_http2_enabled HTTP/2 enabled (1/0).\n");
  bprintf(data, "# TYPE colibri_curl_http2_enabled gauge\n");
  bprintf(data, "colibri_curl_http2_enabled %d\n", http_server.curl.http2_enabled);

  bprintf(data, "# HELP colibri_curl_upkeep_interval_ms Upkeep interval in ms.\n");
  bprintf(data, "# TYPE colibri_curl_upkeep_interval_ms gauge\n");
  bprintf(data, "colibri_curl_upkeep_interval_ms %d\n", http_server.curl.upkeep_interval_ms);

  bprintf(data, "# HELP colibri_curl_tcp_keepalive_enabled TCP keepalive enabled (1/0).\n");
  bprintf(data, "# TYPE colibri_curl_tcp_keepalive_enabled gauge\n");
  bprintf(data, "colibri_curl_tcp_keepalive_enabled %d\n", http_server.curl.tcp_keepalive_enabled);

  bprintf(data, "# HELP colibri_curl_tcp_keepidle_seconds TCP keepidle seconds.\n");
  bprintf(data, "# TYPE colibri_curl_tcp_keepidle_seconds gauge\n");
  bprintf(data, "colibri_curl_tcp_keepidle_seconds %d\n", http_server.curl.tcp_keepidle_s);

  bprintf(data, "# HELP colibri_curl_tcp_keepintvl_seconds TCP keepintvl seconds.\n");
  bprintf(data, "# TYPE colibri_curl_tcp_keepintvl_seconds gauge\n");
  bprintf(data, "colibri_curl_tcp_keepintvl_seconds %d\n", http_server.curl.tcp_keepintvl_s);

  bprintf(data, "# HELP colibri_curl_total_requests Total libcurl transfers.\n");
  bprintf(data, "# TYPE colibri_curl_total_requests counter\n");
  bprintf(data, "colibri_curl_total_requests %l\n", http_server.curl.total_requests);

  bprintf(data, "# HELP colibri_curl_total_connects New TCP connects observed.\n");
  bprintf(data, "# TYPE colibri_curl_total_connects counter\n");
  bprintf(data, "colibri_curl_total_connects %l\n", http_server.curl.total_connects);

  bprintf(data, "# HELP colibri_curl_reused_connections_total Reused connections.\n");
  bprintf(data, "# TYPE colibri_curl_reused_connections_total counter\n");
  bprintf(data, "colibri_curl_reused_connections_total %l\n", http_server.curl.reused_connections_total);

  bprintf(data, "# HELP colibri_curl_http2_requests_total HTTP/2 requests.\n");
  bprintf(data, "# TYPE colibri_curl_http2_requests_total counter\n");
  bprintf(data, "colibri_curl_http2_requests_total %l\n", http_server.curl.http2_requests_total);

  bprintf(data, "# HELP colibri_curl_http1_requests_total HTTP/1.x requests.\n");
  bprintf(data, "# TYPE colibri_curl_http1_requests_total counter\n");
  bprintf(data, "colibri_curl_http1_requests_total %l\n", http_server.curl.http1_requests_total);

  bprintf(data, "# HELP colibri_curl_tls_handshakes_total TLS handshakes (heuristic).\n");
  bprintf(data, "# TYPE colibri_curl_tls_handshakes_total counter\n");
  bprintf(data, "colibri_curl_tls_handshakes_total %l\n", http_server.curl.tls_handshakes_total);

  bprintf(data, "# HELP colibri_curl_avg_connect_time_ms Avg TCP connect time (ms).\n");
  bprintf(data, "# TYPE colibri_curl_avg_connect_time_ms gauge\n");
  bprintf(data, "colibri_curl_avg_connect_time_ms %f\n", http_server.curl.avg_connect_time_ms);

  bprintf(data, "# HELP colibri_curl_avg_appconnect_time_ms Avg TLS handshake time (ms).\n");
  bprintf(data, "# TYPE colibri_curl_avg_appconnect_time_ms gauge\n");
  bprintf(data, "colibri_curl_avg_appconnect_time_ms %f\n", http_server.curl.avg_appconnect_time_ms);
}

#ifdef HTTP_SERVER_GEO
// Comparison function for qsort to sort by count descending
static int compare_geo_locations_desc(const void* a, const void* b) {
  const geo_location_t* loc_a = (const geo_location_t*) a;
  const geo_location_t* loc_b = (const geo_location_t*) b;
  if (loc_b->count < loc_a->count) return -1;
  if (loc_b->count > loc_a->count) return 1;
  return 0;
}

static void c4_write_prometheus_bucket_geo_metrics(buffer_t* data) {
  if (http_server.stats.geo_locations_count == 0) return;

  // Sort the main array directly to get the top locations.
  size_t count = http_server.stats.geo_locations_count;
  if (count > 1) {
    qsort(http_server.stats.geo_locations, count, sizeof(geo_location_t), compare_geo_locations_desc);
  }

  bprintf(data, "# HELP colibri_geo_requests_total Total number of HTTP requests by geo location.\n");
  bprintf(data, "# TYPE colibri_geo_requests_total counter\n");

  const size_t METRICS_GEO_LIMIT = 200;
  size_t       output_count      = (count > METRICS_GEO_LIMIT) ? METRICS_GEO_LIMIT : count;

  for (size_t i = 0; i < output_count; i++) {
    geo_location_t* loc = &http_server.stats.geo_locations[i];
    bprintf(data, "colibri_geo_requests_total{city=\"%s\", country=\"%s\", lat=\"%s\", lon=\"%s\"} %l\n", loc->city, loc->country, loc->latitude ? loc->latitude : "",
            loc->longitude ? loc->longitude : "", loc->count);
  }
}
#endif

bool c4_handle_metrics(client_t* client) {
  if (strcmp(client->request.path, "/metrics") != 0) return false;

  buffer_t data                     = {0};
  size_t   current_rss              = get_current_rss();
  bool     method_metrics_described = false; // Flag, um HELP/TYPE für Methoden-Metriken nur einmal zu schreiben
#ifdef PROVER_CACHE
  uint64_t entries = 0, size = 0, max_size = 0, capacity = 0;
  c4_prover_cache_stats(&entries, &size, &max_size, &capacity);

  bprintf(&data, "# HELP colibri_prover_cache_entries Current number of entries in the prover cache.\n");
  bprintf(&data, "# TYPE colibri_prover_cache_entries gauge\n");
  bprintf(&data, "colibri_prover_cache_entries %l\n", entries);
  bprintf(&data, "# HELP colibri_prover_cache_size Current size of the prover cache in bytes.\n");
  bprintf(&data, "# TYPE colibri_prover_cache_size gauge\n");
  bprintf(&data, "colibri_prover_cache_size %l\n", size);
  bprintf(&data, "# HELP colibri_prover_cache_max_size Maximum size of the prover cache in bytes.\n");
  bprintf(&data, "# TYPE colibri_prover_cache_max_size gauge\n");
  bprintf(&data, "colibri_prover_cache_max_size %l\n", max_size);
  bprintf(&data, "# HELP colibri_prover_cache_capacity Maximum capacity of the prover cache in bytes.\n");
  bprintf(&data, "# TYPE colibri_prover_cache_capacity gauge\n");
  bprintf(&data, "colibri_prover_cache_capacity %l\n", capacity);
#endif
  // RSS Metrik
  bprintf(&data, "# HELP colibri_process_resident_memory_bytes Current resident set size (RSS) of the process in bytes.\n");
  bprintf(&data, "# TYPE colibri_process_resident_memory_bytes gauge\n");
  bprintf(&data, "colibri_process_resident_memory_bytes %l\n\n", (uint64_t) current_rss);

  // CPU Time Metriken
  c4_write_process_cpu_metrics(&data);

  // Event-Loop Health (libuv)
  double idle_seconds_total   = (double) http_server.stats.loop_idle_ns_total / 1e9;
  double idle_ratio           = http_server.stats.loop_idle_ratio;
  double loop_lag_seconds     = (double) http_server.stats.loop_lag_ns_last / 1e9;
  double loop_lag_seconds_max = (double) http_server.stats.loop_lag_ns_max / 1e9;
  bprintf(&data, "# HELP colibri_event_loop_idle_seconds_total Cumulative idle time of the libuv loop in seconds.\n");
  bprintf(&data, "# TYPE colibri_event_loop_idle_seconds_total counter\n");
  bprintf(&data, "colibri_event_loop_idle_seconds_total %f\n", idle_seconds_total);
  bprintf(&data, "# HELP colibri_event_loop_idle_ratio Idle ratio over the last sampling window (0..1).\n");
  bprintf(&data, "# TYPE colibri_event_loop_idle_ratio gauge\n");
  bprintf(&data, "colibri_event_loop_idle_ratio %f\n", idle_ratio);
  bprintf(&data, "# HELP colibri_event_loop_lag_seconds Event-loop scheduling lag in seconds (last observed).\n");
  bprintf(&data, "# TYPE colibri_event_loop_lag_seconds gauge\n");
  bprintf(&data, "colibri_event_loop_lag_seconds %f\n", loop_lag_seconds);
  bprintf(&data, "# HELP colibri_event_loop_lag_seconds_max Max observed event-loop lag in seconds since start.\n");
  bprintf(&data, "# TYPE colibri_event_loop_lag_seconds_max gauge\n");
  bprintf(&data, "colibri_event_loop_lag_seconds_max %f\n\n", loop_lag_seconds_max);

  // Beacon Watcher Event-Metriken
  bprintf(&data, "# HELP colibri_beacon_events_total Total number of beacon events processed.\n");
  bprintf(&data, "# TYPE colibri_beacon_events_total counter\n");
  bprintf(&data, "colibri_beacon_events_total %l\n", http_server.stats.beacon_events_total);

  bprintf(&data, "# HELP colibri_beacon_events_head_total Total number of 'head' beacon events processed.\n");
  bprintf(&data, "# TYPE colibri_beacon_events_head_total counter\n");
  bprintf(&data, "colibri_beacon_events_head_total %l\n", http_server.stats.beacon_events_head);

  bprintf(&data, "# HELP colibri_beacon_events_finalized_total Total number of 'finalized_checkpoint' beacon events processed.\n");
  bprintf(&data, "# TYPE colibri_beacon_events_finalized_total counter\n");
  bprintf(&data, "colibri_beacon_events_finalized_total %l\n\n", http_server.stats.beacon_events_finalized);

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

  // Hinweis: Für exakte Idle-Werte muss der Loop mit UV_METRICS_IDLE_TIME konfiguriert sein (aktiviert).

  // Public Requests
  c4_write_prometheus_bucket_metrics(&data, &public_requests, "public", "public (e.g. eth_getTransactionByHash)", &method_metrics_described);

  // Eth Requests
  c4_write_prometheus_bucket_metrics(&data, &eth_requests, "eth", "ETH JSON-RPC (e.g. eth_getBlockByNumber)", &method_metrics_described);

  // Beacon Requests
  c4_write_prometheus_bucket_metrics(&data, &beacon_requests, "beacon", "Beacon API (e.g. /eth/v1/beacon/genesis)", &method_metrics_described);

  // Rest Requests
  c4_write_prometheus_bucket_metrics(&data, &rest_requests, "rest", "Rest API", &method_metrics_described);

  // Server Health Statistics
  c4_write_curl_metrics(&data);

  c4_write_server_health_metrics(&data);

  // Unsupported RPC methods (ETH RPC only)
  c4_write_unsupported_method_metrics(&data);

#ifdef HTTP_SERVER_GEO

  // Geo Requests
  c4_write_prometheus_bucket_geo_metrics(&data);
#endif

  // Chain-specific metrics
  c4_server_handlers_metrics(&http_server, &data);

  c4_http_respond(client, 200, "text/plain; version=0.0.4; charset=utf-8", data.data);
  buffer_free(&data);
  return true;
}
