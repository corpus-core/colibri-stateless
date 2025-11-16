#ifndef ETH_CLIENTS_H
#define ETH_CLIENTS_H

// Bitmask-based beacon client types for feature detection
#define BEACON_CLIENT_UNKNOWN      0x00000000 // No specific client requirement
#define BEACON_CLIENT_NIMBUS       0x00000001 // (1 << 0)
#define BEACON_CLIENT_LODESTAR     0x00000002 // (1 << 1)
#define BEACON_CLIENT_PRYSM        0x00000004 // (1 << 2)
#define BEACON_CLIENT_LIGHTHOUSE   0x00000008 // (1 << 3)
#define BEACON_CLIENT_TEKU         0x00000010 // (1 << 4)
#define BEACON_CLIENT_GRANDINE     0x00000020 // (1 << 5)
#define BEACON_CLIENT_EVENT_SERVER 0x01000000 // defines the first or server detecting the events

// RPC client types
#define RPC_CLIENT_UNKNOWN    0x00000000
#define RPC_CLIENT_GETH       0x00000100 // (1 << 8)
#define RPC_CLIENT_NETHERMIND 0x00000200 // (1 << 9)
#define RPC_CLIENT_ERIGON     0x00000400 // (1 << 10)
#define RPC_CLIENT_BESU       0x00000800 // (1 << 11)

#endif // ETH_CLIENTS_H
