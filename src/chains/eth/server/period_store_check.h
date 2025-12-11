#ifndef C4_ETH_PERIOD_STORE_CHECK_H
#define C4_ETH_PERIOD_STORE_CHECK_H

#include <stdint.h>

/**
 * Schedule verification of `blocks.ssz` roots against `historical_summaries`.
 *
 * For the given `hist_period`, this loads `historical_root.json` and verifies
 * all completed periods covered by that summary.
 *
 * @param hist_period Period whose `historical_root.json` is used for verification.
 */
void c4_ps_schedule_verify_all_blocks_for_historical(uint64_t hist_period);

/**
 * Fetch `historical_summaries` for the given period and write `historical_root.json`.
 *
 * This is typically triggered when a final checkpoint is first seen for the period.
 *
 * @param period Period for which historical summaries should be fetched.
 */
void c4_ps_schedule_fetch_historical_root(uint64_t period);

#endif /* C4_ETH_PERIOD_STORE_CHECK_H */
