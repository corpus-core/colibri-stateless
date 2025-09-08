// storage.go - Preconfirmation storage management with TTL and cleanup
package main

import (
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// newPreconfStorage creates a new preconf storage with automatic cleanup
func newPreconfStorage(chainID uint64, baseDir string, maxAge time.Duration) *preconfStorage {
	storage := &preconfStorage{
		blockIndex:    make(map[uint64]*preconfEntry),
		chainID:       chainID,
		baseDir:       baseDir,
		maxAge:        maxAge,
		cleanupTicker: time.NewTicker(5 * time.Minute), // Cleanup every 5 minutes
	}

	// Load existing preconfs on startup
	storage.loadExistingPreconfs()

	// Start cleanup goroutine
	go storage.cleanupLoop()

	return storage
}

// loadExistingPreconfs loads existing preconf files on startup
func (s *preconfStorage) loadExistingPreconfs() {
	indexPath := filepath.Join(s.baseDir, "block_index.json")
	data, err := os.ReadFile(indexPath)
	if err != nil {
		log.Printf("No existing block index found, starting fresh")
		return
	}

	var entries []*preconfEntry
	if err := json.Unmarshal(data, &entries); err != nil {
		log.Printf("Failed to parse block index: %v", err)
		return
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	loaded := 0
	for _, entry := range entries {
		// Check if file still exists
		if _, err := os.Stat(entry.FilePath); err == nil {
			s.blockIndex[entry.BlockNumber] = entry
			loaded++
		}
	}

	log.Printf("📚 Loaded %d existing preconfs from block index", loaded)
}

// addPreconf adds a new preconf to the index
func (s *preconfStorage) addPreconf(entry *preconfEntry) {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.blockIndex[entry.BlockNumber] = entry
	log.Printf("📦 Added preconf for block %d to index (total: %d)", entry.BlockNumber, len(s.blockIndex))
}

// getPreconfForBlock searches for a preconf for a specific block
func (s *preconfStorage) getPreconfForBlock(blockNumber uint64) (*preconfEntry, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	entry, exists := s.blockIndex[blockNumber]
	return entry, exists
}

// cleanup removes preconfs older than maxAge
func (s *preconfStorage) cleanup() {
	now := time.Now()
	cutoff := now.Add(-s.maxAge)

	s.mu.Lock()
	defer s.mu.Unlock()

	var toDelete []uint64
	for blockNum, entry := range s.blockIndex {
		entryTime := time.Unix(entry.ReceivedUnix, 0)
		if entryTime.Before(cutoff) {
			toDelete = append(toDelete, blockNum)
		}
	}

	if len(toDelete) == 0 {
		return
	}

	log.Printf("🧹 Cleaning up %d old preconfs (older than %v)", len(toDelete), s.maxAge)

	for _, blockNum := range toDelete {
		entry := s.blockIndex[blockNum]
		// Delete files
		os.Remove(entry.FilePath)
		os.Remove(strings.Replace(entry.FilePath, ".raw", ".json", 1))
		// Remove from index
		delete(s.blockIndex, blockNum)
	}

	log.Printf("📦 Block index now contains %d preconfs", len(s.blockIndex))
}

// cleanupLoop automatically removes old preconfs
func (s *preconfStorage) cleanupLoop() {
	for range s.cleanupTicker.C {
		s.cleanup()
	}
}

// close stops the storage and performs final cleanup
func (s *preconfStorage) close() {
	if s.cleanupTicker != nil {
		s.cleanupTicker.Stop()
	}
	s.cleanup() // Final cleanup
	log.Printf("📦 Preconf storage closed")
}
