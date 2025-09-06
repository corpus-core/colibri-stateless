// main.go - OP Stack preconfirmation capture bridge
// Supports both HTTP polling and libp2p/DHT-based capture methods
package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"
)

func main() {
	var chainID uint64
	var hf int
	var outDir string
	var boots multiAddrs
	var httpEndpoint string
	var useHTTP bool
	var verifySignatures bool
	var ttlMinutes int
	var cleanupIntervalMinutes int
	// New parameters from centralized C configuration
	var chainName string
	var sequencerAddress string

	flag.Uint64Var(&chainID, "chain-id", 10, "OP Stack chain id (e.g. 10 for OP Mainnet, 8453 for Base)")
	flag.IntVar(&hf, "hf", 3, "hardfork version (0=v1,1=v2,2=v3,3=v4/Isthmus)")
	flag.Var(&boots, "bootnode", "libp2p multiaddr of peer (repeatable)")
	flag.StringVar(&outDir, "out-dir", "./preconfs", "directory to write captured messages")
	flag.StringVar(&httpEndpoint, "http-endpoint", "", "HTTP endpoint for preconf polling")
	flag.BoolVar(&useHTTP, "use-http", true, "Use HTTP polling instead of libp2p (default true)")
	flag.BoolVar(&verifySignatures, "verify-signatures", true, "Verify sequencer signatures (default true)")
	flag.IntVar(&ttlMinutes, "ttl-minutes", 30, "TTL for preconfirmations in minutes")
	flag.IntVar(&cleanupIntervalMinutes, "cleanup-interval", 5, "Cleanup interval in minutes")
	// Parameters from centralized C configuration
	flag.StringVar(&chainName, "chain-name", "", "Chain name from centralized config")
	flag.StringVar(&sequencerAddress, "sequencer-address", "", "Expected sequencer address from centralized config")
	flag.Parse()

	if err := os.MkdirAll(outDir, 0o755); err != nil {
		log.Fatalf("mkdir out-dir: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Graceful shutdown
	sigc := make(chan os.Signal, 1)
	signal.Notify(sigc, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-sigc
		log.Printf("Received shutdown signal, stopping...")
		cancel()
	}()

	if useHTTP {
		// Validate required parameters from centralized config
		if httpEndpoint == "" {
			log.Fatalf("❌ HTTP endpoint not provided by server configuration")
		}
		if chainName == "" {
			log.Printf("⚠️  Chain name not provided, using fallback")
			chainName = fmt.Sprintf("Chain_%d", chainID)
		}

		log.Printf("🌐 Starting HTTP-based preconf capture for %s", chainName)
		log.Printf("📡 Endpoint: %s", httpEndpoint)
		log.Printf("📁 Output directory: %s", outDir)
		log.Printf("⛓️  Chain ID: %d, Hardfork: %d", chainID, hf)
		log.Printf("🔐 Signature verification: %v", verifySignatures)
		if verifySignatures && sequencerAddress != "" {
			log.Printf("✅ Expected sequencer: %s", sequencerAddress)
		}

		// Create storage with TTL configuration
		storage := newPreconfStorage(chainID, outDir, time.Duration(ttlMinutes)*time.Minute)
		storage.cleanupTicker = time.NewTicker(time.Duration(cleanupIntervalMinutes) * time.Minute)

		// Create chain config from passed parameters
		chainConfig := chainConfig{
			chainID:      chainID,
			name:         chainName,
			endpoint:     httpEndpoint,
			unsafeSigner: sequencerAddress,
		}

		httpPreconfCapture(ctx, httpEndpoint, chainID, hf, outDir, verifySignatures, storage, chainConfig)

		// Cleanup on shutdown
		defer storage.close()
		return
	}

	// Use libp2p/DHT-based capture
	if err := gossipPreconfCapture(ctx, chainID, hf, outDir, boots); err != nil {
		log.Fatalf("Gossip preconf capture failed: %v", err)
	}
}
