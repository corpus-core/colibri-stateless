// types.go - Shared types and configurations for OP Stack preconfirmation capture
package main

import (
	"sync"
	"time"
)

// chainConfig holds configuration for a specific OP Stack chain
type chainConfig struct {
	chainID      uint64
	name         string
	endpoint     string
	unsafeSigner string // Expected sequencer address
}

// Note: Chain configurations are now centralized in C code (op_chains_conf.c)
// and passed as parameters from the server. The supportedChains map has been removed.

// httpPreconfResponse represents the JSON response from HTTP preconf endpoints
type httpPreconfResponse struct {
	Data      string `json:"data"`
	Signature struct {
		R       string `json:"r"`
		S       string `json:"s"`
		YParity string `json:"yParity"`
		V       string `json:"v"`
	} `json:"signature"`
}

// preconfEntry represents a stored preconfirmation with metadata
type preconfEntry struct {
	BlockNumber    uint64 `json:"block_number"`
	BlockHash      string `json:"block_hash"`
	ChainID        string `json:"chain_id"`
	Hardfork       int    `json:"hardfork_version"`
	Topic          string `json:"topic"`
	MsgID          string `json:"msg_id"`
	ReceivedUnix   int64  `json:"received_unix"`
	FromPeer       string `json:"from_peer"`
	SignerAddress  string `json:"signer_address"` // 0x…
	SignatureHex   string `json:"signature"`
	ParentBBRHex   string `json:"parent_beacon_block_root,omitempty"`
	PayloadKeccak  string `json:"payload_keccak"`
	RawSHA256      string `json:"raw_sha256"`
	DecompressedSz int    `json:"decompressed_size"`
	HTTPEndpoint   string `json:"http_endpoint,omitempty"`
	FilePath       string `json:"file_path"` // Path to .raw file
}

// preconfStorage manages in-memory indexing and cleanup of preconfirmations
type preconfStorage struct {
	mu             sync.RWMutex
	blockIndex     map[uint64]*preconfEntry // Block number -> preconf
	chainID        uint64
	baseDir        string
	maxAge         time.Duration
	cleanupTicker  *time.Ticker
}

// metaFile represents the JSON metadata file structure
type metaFile struct {
	ChainID        string `json:"chain_id"`
	Hardfork       int    `json:"hardfork_version"`
	Topic          string `json:"topic"`
	MsgID          string `json:"msg_id"`
	ReceivedUnix   int64  `json:"received_unix"`
	FromPeer       string `json:"from_peer"`
	SignerAddress  string `json:"signer_address"` // 0x…
	SignatureHex   string `json:"signature"`
	ParentBBRHex   string `json:"parent_beacon_block_root,omitempty"`
	PayloadKeccak  string `json:"payload_keccak"`
	RawSHA256      string `json:"raw_sha256"`
	DecompressedSz int    `json:"decompressed_size"`
	HTTPEndpoint   string `json:"http_endpoint,omitempty"`
	BlockNumber    uint64 `json:"block_number"`
	BlockHash      string `json:"block_hash"`
}

// multiAddrs is a custom flag type for handling multiple multiaddr arguments
type multiAddrs []string

func (m *multiAddrs) String() string { return "" }
func (m *multiAddrs) Set(s string) error {
	*m = append(*m, s)
	return nil
}
