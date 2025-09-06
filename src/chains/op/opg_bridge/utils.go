// utils.go - Shared utility functions for OP Stack preconfirmation capture
package main

import (
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"log"
	"math/big"
	"strings"

	ethcrypto "github.com/ethereum/go-ethereum/crypto"
	"golang.org/x/crypto/sha3"
)

// min returns the smaller of two integers
func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// beUint256FromChainID converts a chain ID to big-endian uint256 encoding
func beUint256FromChainID(chainID uint64) []byte {
	b := make([]byte, 32)
	big.NewInt(0).SetUint64(chainID).FillBytes(b)
	return b
}

// keccak computes the Keccak-256 hash of the input
func keccak(b []byte) []byte {
	h := sha3.NewLegacyKeccak256()
	h.Write(b)
	return h.Sum(nil)
}

// extractBlockNumberFromSSZ extracts the block number from SSZ-encoded Execution Payload
// Based on correct SSZ ExecutionPayload structure analysis:
// Block number is located at offset 13*32+20 = 436 (little-endian uint64)
func extractBlockNumberFromSSZ(payload []byte) (uint64, error) {
	const blockNumberOffset = 13*32 + 20 // 436 bytes
	const blockNumberSize = 8

	if len(payload) < blockNumberOffset+blockNumberSize {
		return 0, fmt.Errorf("payload too short for block number extraction: %d bytes, need %d", len(payload), blockNumberOffset+blockNumberSize)
	}

	// Block number is little-endian uint64 at fixed offset 436
	blockNumberBytes := payload[blockNumberOffset : blockNumberOffset+blockNumberSize]
	blockNumber := binary.LittleEndian.Uint64(blockNumberBytes)

	log.Printf("🔍 Extracted block number %d from SSZ payload at offset %d", blockNumber, blockNumberOffset)
	return blockNumber, nil
}

// extractBlockHashFromSSZ extracts the block hash from SSZ-encoded Execution Payload
// IMPORTANT: In SSZ, variable fields (extra_data, transactions, withdrawals) are stored at the end!
// The fixed part contains only offsets (4 bytes) for variable fields.
// Correct fixed structure:
// parent_hash(32) + fee_recipient(20) + state_root(32) + receipts_root(32) + logs_bloom(256) +
// prev_randao(32) + block_number(8) + gas_limit(8) + gas_used(8) + timestamp(8) +
// extra_data_offset(4) + base_fee_per_gas(32) + block_hash(32) +
// transactions_offset(4) + withdrawals_offset(4) + blob_gas_used(8) + excess_blob_gas(8) + withdrawals_root(32)
func extractBlockHashFromSSZ(payload []byte) (string, error) {
	// Correct block hash offset based on hex editor analysis: 504
	const blockHashOffset = 504 // Verified with Base Explorer
	const blockHashSize = 32

	if len(payload) < blockHashOffset+blockHashSize {
		return "", fmt.Errorf("payload too short for block hash extraction: %d bytes, need %d", len(payload), blockHashOffset+blockHashSize)
	}

	// Block hash at empirically determined offset 504
	blockHashBytes := payload[blockHashOffset : blockHashOffset+blockHashSize]
	blockHash := "0x" + hex.EncodeToString(blockHashBytes)

	log.Printf("🔍 Extracted block hash %s from SSZ payload at offset %d", blockHash[:18]+"...", blockHashOffset)
	return blockHash, nil
}

// verifySequencerSignature verifies the sequencer signature according to OP Stack standard
func verifySequencerSignature(data []byte, sig []byte, expectedSigner string, chainID uint64) (string, error) {
	// Calculate signing message according to OP Stack standard:
	// keccak256(domain(32x0) || chain_id(be-uint256) || keccak(payload))
	domain := make([]byte, 32) // 32 zero bytes

	// Chain ID as big-endian uint256
	chainIDBytes := beUint256FromChainID(chainID)

	// Payload hash
	payloadHash := keccak(data)

	// Signing message: domain || chain_id || payload_hash
	msgBuf := append(domain, chainIDBytes...)
	msgBuf = append(msgBuf, payloadHash...)
	msgHash := keccak(msgBuf)

	// Recover public key from signature
	if len(sig) != 65 {
		return "", fmt.Errorf("invalid signature length: %d", len(sig))
	}

	// Convert v to recovery ID (0 or 1)
	if sig[64] < 27 {
		return "", fmt.Errorf("invalid recovery ID: %d", sig[64])
	}
	recoverySig := make([]byte, 65)
	copy(recoverySig, sig)
	recoverySig[64] = sig[64] - 27

	pubkey, err := ethcrypto.SigToPub(msgHash, recoverySig)
	if err != nil {
		return "", fmt.Errorf("failed to recover public key: %v", err)
	}

	recoveredAddr := ethcrypto.PubkeyToAddress(*pubkey)
	recoveredAddrStr := strings.ToLower(recoveredAddr.Hex())

	// Verify against expected signer if provided
	if expectedSigner != "" {
		expectedLower := strings.ToLower(expectedSigner)
		if recoveredAddrStr != expectedLower {
			return recoveredAddrStr, fmt.Errorf("signature verification failed: expected %s, got %s", expectedSigner, recoveredAddrStr)
		}
	}

	return recoveredAddrStr, nil
}

// hexOrEmpty returns hex-encoded string or empty string if input is empty
func hexOrEmpty(b []byte) string {
	if len(b) == 0 {
		return ""
	}
	return "0x" + hex.EncodeToString(b)
}
