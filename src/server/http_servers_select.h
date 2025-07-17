#pragma once

#include "server.h"
#include <stdbool.h>
#include <stdint.h>

// Load balancing functions
int  c4_select_best_server(server_list_t* servers, uint32_t exclude_mask);
void c4_update_server_health(server_list_t* servers, int server_index, uint64_t response_time, bool success);
void c4_calculate_server_weights(server_list_t* servers);

bool c4_should_reset_health_stats(server_list_t* servers);
void c4_reset_server_health_stats(server_list_t* servers);
bool c4_is_user_error_response(long http_code);
bool c4_has_available_servers(server_list_t* servers, uint32_t exclude_mask);
void c4_attempt_server_recovery(server_list_t* servers);
