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

typedef struct {
  uint64_t  total_requests;  // total of number of requests
  uint64_t  total_errors;    // total of number of errors
  char**    names;           // list of method names
  size_t    names_length;    // length of the list of method names
  uint64_t* counters;        // list of counters for each method
  uint64_t* total_sizes;     // list of total size for each method
  uint64_t* total_durations; // list of total duration for each method
  uint64_t* total_cached;    // total of number of cached requests
} methode_counts_t;

static methode_counts_t public_requests = {0};
static methode_counts_t eth_requests    = {0};
static methode_counts_t beacon_requests = {0};

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
  methode_counts_t* metrics = NULL;
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
  for (size_t i = 0; i < metrics->names_length; i++) {
    if (strcmp(metrics->names[i], method) == 0) {
      index = i;
      break;
    }
  }

  if (index == -1) {
    metrics->names_length++;
    metrics->names                            = realloc(metrics->names, metrics->names_length * sizeof(char*));
    metrics->names[metrics->names_length - 1] = strdup(method);
    metrics->counters                         = realloc(metrics->counters, metrics->names_length * sizeof(uint64_t));
    metrics->total_sizes                      = realloc(metrics->total_sizes, metrics->names_length * sizeof(uint64_t));
    metrics->total_durations                  = realloc(metrics->total_durations, metrics->names_length * sizeof(uint64_t));
    metrics->total_cached                     = realloc(metrics->total_cached, metrics->names_length * sizeof(uint64_t));
    index                                     = metrics->names_length - 1;
    metrics->total_cached[index]              = 0;
  }

  metrics->counters[index]++;
  metrics->total_sizes[index] += size;
  metrics->total_durations[index] += duration;
  metrics->total_requests++;
  if (!success) metrics->total_errors++;
  if (cached) metrics->total_cached[index]++;
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

static void c4_write_prometheus_bucket_metrics(buffer_t*         data,
                                               methode_counts_t* metrics,
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
  for (size_t i = 0; i < metrics->names_length; i++) {
    bucket_total_cached += metrics->total_cached[i];
  }
  bprintf(data, "# HELP colibri_%s_cached_total Total number of cached %s requests.\n", bucket_type_name, bucket_description_prefix);
  bprintf(data, "# TYPE colibri_%s_cached_total counter\n", bucket_type_name);
  bprintf(data, "colibri_%s_cached_total %l\n\n", bucket_type_name, bucket_total_cached);

  if (metrics->names_length > 0) {
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
    for (size_t i = 0; i < metrics->names_length; i++) {
      // TODO: Methodennamen escapen, falls sie Sonderzeichen enthalten können, die von Prometheus nicht erlaubt sind.
      // Für einfache Methodennamen ist dies normalerweise nicht erforderlich.
      bprintf(data, "colibri_method_requests_total{type=\"%s\",method=\"%s\"} %l\n", bucket_type_name, metrics->names[i], metrics->counters[i]);
      bprintf(data, "colibri_method_duration_milliseconds_total{type=\"%s\",method=\"%s\"} %l\n", bucket_type_name, metrics->names[i], metrics->total_durations[i]);
      bprintf(data, "colibri_method_size_bytes_total{type=\"%s\",method=\"%s\"} %l\n", bucket_type_name, metrics->names[i], metrics->total_sizes[i]);
      bprintf(data, "colibri_method_cached_total{type=\"%s\",method=\"%s\"} %l\n", bucket_type_name, metrics->names[i], metrics->total_cached[i]);
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

  // RSS Metrik
  bprintf(&data, "# HELP colibri_process_resident_memory_bytes Current resident set size (RSS) of the process in bytes.\n");
  bprintf(&data, "# TYPE colibri_process_resident_memory_bytes gauge\n");
  bprintf(&data, "colibri_process_resident_memory_bytes %l\n\n", (uint64_t) current_rss);

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
