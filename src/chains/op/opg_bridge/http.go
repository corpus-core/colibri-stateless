// http.go - HTTP-based preconfirmation capture for OP Stack chains
package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/klauspost/compress/zstd"
)

// httpPreconfCapture performs HTTP-based preconf capture
func httpPreconfCapture(ctx context.Context, endpoint string, chainID uint64, hf int, outDir string, verifySignatures bool, storage *preconfStorage) {
	client := &http.Client{Timeout: 30 * time.Second}

	var lastDataHash string
	pollInterval := 2 * time.Second

	log.Printf("🔄 Starting HTTP polling every %v", pollInterval)

	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			log.Printf("HTTP preconf capture stopped")
			return
		case <-ticker.C:
			resp, err := client.Get(endpoint)
			if err != nil {
				log.Printf("❌ HTTP request failed: %v", err)
				continue
			}

			body, err := io.ReadAll(resp.Body)
			resp.Body.Close()

			if err != nil {
				log.Printf("❌ Failed to read response: %v", err)
				continue
			}

			if resp.StatusCode != 200 {
				log.Printf("❌ HTTP %d: %s", resp.StatusCode, string(body))
				continue
			}

			var preconf httpPreconfResponse
			if err := json.Unmarshal(body, &preconf); err != nil {
				log.Printf("❌ Failed to parse JSON: %v", err)
				continue
			}

			// Check if this is new data
			dataHash := fmt.Sprintf("%x", sha256.Sum256([]byte(preconf.Data)))
			if dataHash == lastDataHash {
				continue // Same data, skip
			}

			lastDataHash = dataHash
			log.Printf("🎉 NEW PRECONF! Data size: %d bytes, Hash: %s", len(preconf.Data), dataHash[:16])

			// Process the preconf
			if err := processHTTPPreconf(preconf, chainID, hf, outDir, endpoint, verifySignatures, storage); err != nil {
				log.Printf("❌ Failed to process preconf: %v", err)
			} else {
				log.Printf("✅ Preconf saved successfully")
			}
		}
	}
}

// processHTTPPreconf processes an HTTP preconf and saves it
func processHTTPPreconf(preconf httpPreconfResponse, chainID uint64, hf int, outDir, endpoint string, verifySignatures bool, storage *preconfStorage) error {
	// Decode hex data
	dataBytes, err := hex.DecodeString(strings.TrimPrefix(preconf.Data, "0x"))
	if err != nil {
		return fmt.Errorf("invalid hex data: %v", err)
	}

	// Create signature bytes (r + s + v) with proper padding
	rHex := strings.TrimPrefix(preconf.Signature.R, "0x")
	sHex := strings.TrimPrefix(preconf.Signature.S, "0x")

	// Pad to even length if needed
	if len(rHex)%2 == 1 {
		rHex = "0" + rHex
	}
	if len(sHex)%2 == 1 {
		sHex = "0" + sHex
	}

	rBytes, err := hex.DecodeString(rHex)
	if err != nil {
		return fmt.Errorf("invalid r value: %v", err)
	}
	sBytes, err := hex.DecodeString(sHex)
	if err != nil {
		return fmt.Errorf("invalid s value: %v", err)
	}

	// Convert yParity to v (27 + yParity)
	var vByte byte = 27
	if preconf.Signature.YParity == "0x1" {
		vByte = 28
	}

	// Pad r and s to 32 bytes
	sigBytes := make([]byte, 65)
	copy(sigBytes[32-len(rBytes):32], rBytes)   // Right-pad r to 32 bytes
	copy(sigBytes[64-len(sBytes):64], sBytes)   // Right-pad s to 32 bytes
	sigBytes[64] = vByte

	// Extract block number and hash from SSZ payload
	blockNumber, err := extractBlockNumberFromSSZ(dataBytes)
	if err != nil {
		log.Printf("⚠️  Failed to extract block number: %v, using timestamp fallback", err)
		blockNumber = uint64(time.Now().Unix()) // Fallback
	}

	blockHash, err := extractBlockHashFromSSZ(dataBytes)
	if err != nil {
		log.Printf("⚠️  Failed to extract block hash: %v", err)
		hash := sha256.Sum256(dataBytes)
		blockHash = "0x" + hex.EncodeToString(hash[:16]) // Fallback
	}

	// Calculate hashes
	rawHash := sha256.Sum256(dataBytes)
	payloadHash := keccak(dataBytes)

	// Log basic info
	log.Printf("📊 Payload: %d bytes, Chain: %d", len(dataBytes), chainID)

	// Verify signature if requested
	var signerAddress string
	if verifySignatures {
		chainConfig, exists := supportedChains[chainID]
		expectedSigner := ""
		if exists {
			expectedSigner = chainConfig.unsafeSigner
		}

		recoveredSigner, err := verifySequencerSignature(dataBytes, sigBytes, expectedSigner, chainID)
		if err != nil {
			log.Printf("⚠️  Signature verification failed: %v", err)
			signerAddress = "INVALID_SIGNATURE"
		} else {
			signerAddress = recoveredSigner
			log.Printf("🔐 Signature verified! Signer: %s", signerAddress)
		}
	} else {
		signerAddress = "NOT_VERIFIED"
	}

	// Generate deterministic block-based file names for C-access
	// Format: block_{chainID}_{blockNumber} (without timestamp for deterministic C access)
	// If multiple preconfs exist for the same block, use the newest one
	ts := time.Now().Unix()
	base := fmt.Sprintf("block_%d_%d", chainID, blockNumber)
	rawPath := filepath.Join(outDir, base+".raw")
	metaPath := filepath.Join(outDir, base+".json")

	// Check if a preconf already exists for this block
	if _, err := os.Stat(rawPath); err == nil {
		log.Printf("📦 Updating existing preconf for block %d: %s", blockNumber, base)
	} else {
		log.Printf("📦 Creating new preconf for block %d: %s", blockNumber, base)
	}

	// Compress payload with ZSTD
	encoder, err := zstd.NewWriter(nil)
	if err != nil {
		return fmt.Errorf("failed to create ZSTD encoder: %v", err)
	}
	defer encoder.Close()

	compressedPayload := encoder.EncodeAll(dataBytes, nil)
	log.Printf("🗜️  ZSTD compression: %d bytes -> %d bytes (%.1f%% reduction)",
		len(dataBytes), len(compressedPayload),
		100.0*(1.0-float64(len(compressedPayload))/float64(len(dataBytes))))

	// Save compressed data with appended signature (as expected by C-verifier)
	// Format: ZSTD-compressed-payload + 65-byte signature
	combinedData := make([]byte, len(compressedPayload)+65)
	copy(combinedData, compressedPayload)
	copy(combinedData[len(compressedPayload):], sigBytes)

	// Atomic write: write to temp file first, then rename
	tempRawPath := rawPath + ".tmp"
	if err := os.WriteFile(tempRawPath, combinedData, 0o644); err != nil {
		return fmt.Errorf("failed to write temp raw file: %v", err)
	}
	
	// Atomic rename (this is atomic on most filesystems)
	if err := os.Rename(tempRawPath, rawPath); err != nil {
		os.Remove(tempRawPath) // Cleanup on failure
		return fmt.Errorf("failed to rename temp raw file: %v", err)
	}

	// Update latest.raw symlink to point to the newest preconf
	latestPath := filepath.Join(outDir, "latest.raw")

	// Remove existing symlink if it exists
	os.Remove(latestPath)

	// Create new symlink pointing to the current raw file
	if err := os.Symlink(filepath.Base(rawPath), latestPath); err != nil {
		log.Printf("⚠️  Failed to create latest.raw symlink: %v", err)
	} else {
		log.Printf("🔗 Updated latest.raw symlink to %s", filepath.Base(rawPath))
	}

	// Create metadata
	chainName := fmt.Sprintf("Chain_%d", chainID)
	if config, exists := supportedChains[chainID]; exists {
		chainName = config.name
	}

	meta := metaFile{
		ChainID:        fmt.Sprintf("%d", chainID),
		Hardfork:       hf,
		Topic:          fmt.Sprintf("http://%s/%d/preconfs", chainName, hf),
		MsgID:          fmt.Sprintf("%x", rawHash[:20]),
		ReceivedUnix:   ts,
		FromPeer:       endpoint,
		SignerAddress:  signerAddress,
		SignatureHex:   "0x" + hex.EncodeToString(sigBytes),
		PayloadKeccak:  "0x" + hex.EncodeToString(payloadHash),
		RawSHA256:      "0x" + hex.EncodeToString(rawHash[:]),
		DecompressedSz: len(dataBytes),
		HTTPEndpoint:   endpoint,
		BlockNumber:    blockNumber,
		BlockHash:      blockHash,
	}

	metaJSON, err := json.MarshalIndent(&meta, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshal metadata: %v", err)
	}

	// Atomic write for metadata file too
	tempMetaPath := metaPath + ".tmp"
	if err := os.WriteFile(tempMetaPath, metaJSON, 0o644); err != nil {
		return fmt.Errorf("failed to write temp meta file: %v", err)
	}
	
	if err := os.Rename(tempMetaPath, metaPath); err != nil {
		os.Remove(tempMetaPath) // Cleanup on failure
		return fmt.Errorf("failed to rename temp meta file: %v", err)
	}

	// Add to storage for fast access
	entry := &preconfEntry{
		BlockNumber:    blockNumber,
		BlockHash:      blockHash,
		ChainID:        fmt.Sprintf("%d", chainID),
		Hardfork:       hf,
		Topic:          fmt.Sprintf("http://%s/%d/preconfs", chainName, hf),
		MsgID:          fmt.Sprintf("%x", rawHash[:20]),
		ReceivedUnix:   ts,
		FromPeer:       endpoint,
		SignerAddress:  signerAddress,
		SignatureHex:   "0x" + hex.EncodeToString(sigBytes),
		ParentBBRHex:   "", // TODO: Extract from payload if needed
		PayloadKeccak:  "0x" + hex.EncodeToString(payloadHash),
		RawSHA256:      "0x" + hex.EncodeToString(rawHash[:]),
		DecompressedSz: len(dataBytes),
		HTTPEndpoint:   endpoint,
		FilePath:       rawPath,
	}

	storage.addPreconf(entry)

	return nil
}
