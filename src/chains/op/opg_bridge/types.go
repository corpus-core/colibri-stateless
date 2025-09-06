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

// supportedChains maps chain IDs to their configurations
var supportedChains = map[uint64]chainConfig{
	10: {
		chainID:      10,
		name:         "OP Mainnet",
		endpoint:     "https://op-mainnet.operationsolarstorm.org/latest",
		unsafeSigner: "0xAAAA45d9549EDA09E70937013520214382Ffc4A2",
	},
	8453: {
		chainID:      8453,
		name:         "Base",
		endpoint:     "https://base.operationsolarstorm.org/latest",
		unsafeSigner: "0xAf6E19BE0F9cE7f8afd49a1824851023A8249e8a",
	},
	480: {
		chainID:      480,
		name:         "Worldchain",
		endpoint:     "https://worldchain.operationsolarstorm.org/latest",
		unsafeSigner: "0x2270d6eC8E760daA317DD978cFB98C8f144B1f3A",
	},
	7777777: {
		chainID:      7777777,
		name:         "Zora",
		endpoint:     "https://zora.operationsolarstorm.org/latest",
		unsafeSigner: "0x3Dc8Dfd0709C835cAd15a6A27e089FF4cF4C9228",
	},
	130: {
		chainID:      130,
		name:         "Unichain",
		endpoint:     "https://unichain.operationsolarstorm.org/latest",
		unsafeSigner: "0x833C6f278474A78658af91aE8edC926FE33a230e",
	},
}

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
