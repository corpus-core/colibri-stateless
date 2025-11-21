#include "eth_conf.h"
#include "configure.h"

eth_config_t eth_config = {
    .stream_beacon_events  = 0,
    .eth_logs_cache_blocks = 0,
};

void eth_configure() {
  conf_int(&eth_config.stream_beacon_events, "ETH_STREAM_BEACON_EVENTS", "stream_beacon_events", 0, "stream beacon events (0/1)", 0, 1);
  conf_int(&eth_config.eth_logs_cache_blocks, "ETH_LOGS_CACHE_BLOCKS", "eth_logs_cache_blocks", 0, "eth logs cache blocks", 0, 131072);
}