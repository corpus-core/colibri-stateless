#ifndef C4_CHAIN_PROPS_H
#define C4_CHAIN_PROPS_H

#include "util/chains.h"

#ifdef CHAIN_PROPS_PATH
#include CHAIN_PROPS_PATH
#else
// Fallback for editors/linters before generated header exists
bool c4_chains_get_props(chain_id_t chain_id, chain_properties_t* props);
#endif

#endif // C4_CHAIN_PROPS_H


