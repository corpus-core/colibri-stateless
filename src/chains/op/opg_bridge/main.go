// cmd/opg_bridge/main.go
package main

import (
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"math/big"
	"net/http"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"

	pubsub "github.com/libp2p/go-libp2p-pubsub"
	pb "github.com/libp2p/go-libp2p-pubsub/pb"
	libp2p "github.com/libp2p/go-libp2p"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/p2p/discovery/routing"
	"github.com/libp2p/go-libp2p/p2p/discovery/util"
	dht "github.com/libp2p/go-libp2p-kad-dht"
	ma "github.com/multiformats/go-multiaddr"
	"github.com/multiformats/go-multihash"

	"github.com/golang/snappy"
	ethcrypto "github.com/ethereum/go-ethereum/crypto"
	"golang.org/x/crypto/sha3"
	"github.com/klauspost/compress/zstd"
)

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

type chainConfig struct {
	chainID      uint64
	name         string
	endpoint     string
	unsafeSigner string // Expected sequencer address
}

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

type httpPreconfResponse struct {
	Data      string `json:"data"`
	Signature struct {
		R       string `json:"r"`
		S       string `json:"s"`
		YParity string `json:"yParity"`
		V       string `json:"v"`
	} `json:"signature"`
}

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
	FilePath       string `json:"file_path"`        // Path to .raw file
}

type preconfStorage struct {
	mu             sync.RWMutex
	blockIndex     map[uint64]*preconfEntry // Block number -> preconf
	chainID        uint64
	baseDir        string
	maxAge         time.Duration
	cleanupTicker  *time.Ticker
}

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

func beUint256FromChainID(chainID uint64) []byte {
	// big-endian uint256 encoding
	b := make([]byte, 32)
	big.NewInt(0).SetUint64(chainID).FillBytes(b)
	return b
}

func keccak(b []byte) []byte {
	h := sha3.NewLegacyKeccak256()
	h.Write(b)
	return h.Sum(nil)
}

// extractBlockNumberFromSSZ extrahiert die Block-Nummer aus SSZ-codiertem Execution Payload
// Basierend auf korrekter SSZ ExecutionPayload Struktur-Analyse:
// Block-Nummer befindet sich bei Offset 13*32+20 = 436 (little-endian uint64)
func extractBlockNumberFromSSZ(payload []byte) (uint64, error) {
	const blockNumberOffset = 13*32 + 20  // 436 bytes
	const blockNumberSize = 8
	
	if len(payload) < blockNumberOffset + blockNumberSize {
		return 0, fmt.Errorf("payload too short for block number extraction: %d bytes, need %d", len(payload), blockNumberOffset + blockNumberSize)
	}
	
	// Block-Nummer ist little-endian uint64 bei festem Offset 436
	blockNumberBytes := payload[blockNumberOffset:blockNumberOffset + blockNumberSize]
	blockNumber := binary.LittleEndian.Uint64(blockNumberBytes)
	
	log.Printf("🔍 Extracted block number %d from SSZ payload at offset %d", blockNumber, blockNumberOffset)
	return blockNumber, nil
}

// extractBlockHashFromSSZ extrahiert den Block-Hash aus SSZ-codiertem Execution Payload
// WICHTIG: In SSZ werden variable Felder (extra_data, transactions, withdrawals) am Ende gespeichert!
// Der fixe Teil enthält nur Offsets (4 bytes) für variable Felder.
// Korrekte fixe Struktur:
// parent_hash(32) + fee_recipient(20) + state_root(32) + receipts_root(32) + logs_bloom(256) + 
// prev_randao(32) + block_number(8) + gas_limit(8) + gas_used(8) + timestamp(8) + 
// extra_data_offset(4) + base_fee_per_gas(32) + block_hash(32) + 
// transactions_offset(4) + withdrawals_offset(4) + blob_gas_used(8) + excess_blob_gas(8) + withdrawals_root(32)
func extractBlockHashFromSSZ(payload []byte) (string, error) {
	const blockNumberOffset = 13*32 + 20  // 436 bytes  
	// Korrekter Block-Hash-Offset basierend auf Hex-Editor-Analyse: 504
	const blockHashOffset = 504  // Verifiziert mit Base Explorer
	const blockHashSize = 32
	
	if len(payload) < blockHashOffset + blockHashSize {
		return "", fmt.Errorf("payload too short for block hash extraction: %d bytes, need %d", len(payload), blockHashOffset + blockHashSize)
	}
	
	// Block-Hash bei empirisch ermitteltem Offset 506
	blockHashBytes := payload[blockHashOffset:blockHashOffset + blockHashSize]
	blockHash := "0x" + hex.EncodeToString(blockHashBytes)
	
	log.Printf("🔍 Extracted block hash %s from SSZ payload at offset %d", blockHash[:18]+"...", blockHashOffset)
	return blockHash, nil
}

// OP message-id fn: SHA256(domain + (decompressed|raw))[:20]
func msgIDFn() pubsub.MsgIdFunction {
	validDomain := []byte("MESSAGE_DOMAIN_VALID_SNAPPY")
	invalidDomain := []byte("MESSAGE_DOMAIN_INVALID_SNAPPY")
	return func(pmsg *pb.Message) string {
		data := pmsg.Data
		var sum [32]byte
		if dec, err := snappy.Decode(nil, data); err == nil {
			h := sha256.New()
			h.Write(validDomain)
			h.Write(dec)
			copy(sum[:], h.Sum(nil))
		} else {
			h := sha256.New()
			h.Write(invalidDomain)
			h.Write(data)
			copy(sum[:], h.Sum(nil))
		}
		return string(sum[:20])
	}
}

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
	flag.Uint64Var(&chainID, "chain-id", 10, "OP Stack chain id (e.g. 10 for OP Mainnet, 8453 for Base)")
	flag.IntVar(&hf, "hf", 3, "hardfork version (0=v1,1=v2,2=v3,3=v4/Isthmus)")
	flag.Var(&boots, "bootnode", "libp2p multiaddr of peer (repeatable)")
	flag.StringVar(&outDir, "out-dir", "./preconfs", "directory to write captured messages")
	flag.StringVar(&httpEndpoint, "http-endpoint", "", "HTTP endpoint for preconf polling (auto-detected from chain-id)")
	flag.BoolVar(&useHTTP, "use-http", true, "Use HTTP polling instead of libp2p (default true)")
	flag.BoolVar(&verifySignatures, "verify-signatures", true, "Verify sequencer signatures (default true)")
	flag.IntVar(&ttlMinutes, "ttl-minutes", 30, "TTL for preconfirmations in minutes")
	flag.IntVar(&cleanupIntervalMinutes, "cleanup-interval", 5, "Cleanup interval in minutes")
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
		// Auto-detect endpoint if not provided
		if httpEndpoint == "" {
			if config, ok := supportedChains[chainID]; ok {
				httpEndpoint = config.endpoint
				log.Printf("🔍 Auto-detected endpoint for %s (Chain ID %d): %s", config.name, chainID, httpEndpoint)
			} else {
				log.Fatalf("❌ Chain ID %d not supported. Supported chains: OP Mainnet (10), Base (8453), Worldchain (480), Zora (7777777), Unichain (130)", chainID)
			}
		}
		
		chainConfig, exists := supportedChains[chainID]
		if !exists {
			log.Fatalf("❌ Chain ID %d not supported. Supported chains: OP Mainnet (10), Base (8453), Worldchain (480), Zora (7777777), Unichain (130)", chainID)
		}
		log.Printf("🌐 Starting HTTP-based preconf capture for %s", chainConfig.name)
		log.Printf("📡 Endpoint: %s", httpEndpoint)
		log.Printf("📁 Output directory: %s", outDir)
		log.Printf("⛓️  Chain ID: %d, Hardfork: %d", chainID, hf)
		log.Printf("🔐 Signature verification: %v", verifySignatures)
		if verifySignatures {
			log.Printf("✅ Expected sequencer: %s", chainConfig.unsafeSigner)
		}
		
		// Erstelle Storage mit TTL-Konfiguration
		storage := newPreconfStorage(chainID, outDir, time.Duration(ttlMinutes)*time.Minute)
		storage.cleanupTicker = time.NewTicker(time.Duration(cleanupIntervalMinutes) * time.Minute)
		
		httpPreconfCapture(ctx, httpEndpoint, chainID, hf, outDir, verifySignatures, storage)
		
		// Cleanup beim Shutdown
		defer storage.close()
		return
	}

	log.Printf("🔗 Starting libp2p-based preconf capture")

	// Erstelle libp2p Host mit DHT-Support
	h, err := libp2p.New(
		libp2p.EnableRelay(),
		libp2p.EnableNATService(),
		libp2p.EnableHolePunching(),
	)
	if err != nil { log.Fatalf("libp2p host: %v", err) }

	log.Printf("libp2p host ID: %s", h.ID().String())
	log.Printf("libp2p addresses: %v", h.Addrs())

	// Initialisiere DHT für Peer-Discovery mit expliziten Bootstrap-Nodes
	kdht, err := dht.New(ctx, h, dht.Mode(dht.ModeAuto))
	if err != nil { log.Fatalf("DHT initialization: %v", err) }

	// Verbinde zu bekannten libp2p Bootstrap-Nodes
	bootstrapPeers := []string{
		"/dnsaddr/bootstrap.libp2p.io/p2p/QmNnooDu7bfjPFoTZYxMNLWUQJyrVwtbZg5gBMjTezGAJN",
		"/dnsaddr/bootstrap.libp2p.io/p2p/QmQCU2EcMqAqQPR2i9bChDtGNJchTbq5TbXJJ16u19uLTa", 
		"/dnsaddr/bootstrap.libp2p.io/p2p/QmbLHAnMoJPWSCR5Zhtx6BHJX9KiKNN6tpvbUcqanj75Nb",
		"/dnsaddr/bootstrap.libp2p.io/p2p/QmcZf59bWwK5XFi76CZX8cbJ4BhTzzA3gU1ZjYZcYW3dwt",
	}

	log.Printf("Connecting to %d libp2p bootstrap peers...", len(bootstrapPeers))
	for _, addrStr := range bootstrapPeers {
		addr, err := ma.NewMultiaddr(addrStr)
		if err != nil {
			log.Printf("Invalid bootstrap address %s: %v", addrStr, err)
			continue
		}
		
		peerInfo, err := peer.AddrInfoFromP2pAddr(addr)
		if err != nil {
			log.Printf("Failed to parse bootstrap peer %s: %v", addr, err)
			continue
		}
		
		log.Printf("Connecting to bootstrap peer: %s", peerInfo.ID.String())
		if err := h.Connect(ctx, *peerInfo); err != nil {
			log.Printf("Failed to connect to bootstrap peer %s: %v", peerInfo.ID.String(), err)
		} else {
			log.Printf("Connected to bootstrap peer: %s", peerInfo.ID.String())
		}
	}

	// Bootstrap DHT
	log.Printf("Bootstrapping DHT...")
	if err := kdht.Bootstrap(ctx); err != nil {
		log.Printf("DHT bootstrap failed: %v", err)
	} else {
		log.Printf("DHT bootstrap successful")
	}

	// Erstelle Discovery-Service
	disc := routing.NewRoutingDiscovery(kdht)

	// Warte kurz für DHT-Bootstrap
	time.Sleep(2 * time.Second)

	ps, err := pubsub.NewGossipSub(ctx, h,
		pubsub.WithMessageIdFn(msgIDFn()),
		pubsub.WithNoAuthor(),
		pubsub.WithMessageSignaturePolicy(pubsub.StrictNoSign),
		pubsub.WithPeerScore(&pubsub.PeerScoreParams{
			AppSpecificScore: func(peer.ID) float64 { return 0 },
			DecayInterval:    time.Second,
			DecayToZero:      0.01,
		}, &pubsub.PeerScoreThresholds{}),
		pubsub.WithFloodPublish(true),
		pubsub.WithPeerExchange(true),
	)
	if err != nil { log.Fatalf("gossipsub: %v", err) }

	// Legacy bootnode connections (als Fallback)
	log.Printf("Attempting to connect to %d legacy bootnodes...", len(boots))
	for _, bootStr := range boots {
		m, err := parseBootnode(bootStr)
		if err != nil { 
			log.Printf("failed to parse bootnode %s: %v", bootStr, err)
			continue 
		}
		
		info, err := peer.AddrInfoFromP2pAddr(m)
		if err != nil { 
			log.Printf("bad bootnode %s: %v", m, err)
			continue 
		}
		
		log.Printf("Connecting to bootnode: %s", m.String())
		if err := h.Connect(ctx, *info); err != nil {
			log.Printf("failed to connect to bootnode %s: %v", m, err)
		}
	}

	topicStr := fmt.Sprintf("/optimism/%d/%d/blocks", chainID, hf) // v4/Isthmus ⇒ hf=3
	topic, err := ps.Join(topicStr)
	if err != nil { log.Fatalf("join topic: %v", err) }
	sub, err := topic.Subscribe()
	if err != nil { log.Fatalf("subscribe: %v", err) }

	log.Printf("Subscribed to topic: %s", topicStr)

	// DHT-basierte Peer-Discovery für den spezifischen Topic
	go func() {
		log.Printf("Starting DHT-based peer discovery for topic: %s", topicStr)
		
		// Advertise unsere Teilnahme am Topic
		util.Advertise(ctx, disc, topicStr)
		log.Printf("Advertising participation in topic: %s", topicStr)

		// Kontinuierliche Peer-Discovery
		ticker := time.NewTicker(30 * time.Second)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				// Suche nach Peers für diesen Topic
				log.Printf("Searching for peers interested in topic: %s", topicStr)
				peerCh, err := disc.FindPeers(ctx, topicStr)
				if err != nil {
					log.Printf("DHT peer discovery failed: %v", err)
					continue
				}

				// Verbinde zu gefundenen Peers
				peersFound := 0
				for peer := range peerCh {
					if peer.ID == h.ID() {
						continue // Skip self
					}
					
					peersFound++
					log.Printf("Discovered peer: %s with %d addresses", peer.ID.String(), len(peer.Addrs))
					
					if len(peer.Addrs) > 0 {
						if err := h.Connect(ctx, peer); err != nil {
							log.Printf("Failed to connect to discovered peer %s: %v", peer.ID.String(), err)
						} else {
							log.Printf("Successfully connected to peer: %s", peer.ID.String())
						}
					}
				}
				
				if peersFound == 0 {
					log.Printf("No peers found for topic %s in this round", topicStr)
				} else {
					log.Printf("Discovery round completed: found %d peers for topic %s", peersFound, topicStr)
				}

				// Zeige aktuelle Peer-Statistiken
				connectedPeers := h.Network().Peers()
				log.Printf("Currently connected to %d peers total", len(connectedPeers))
				
				// Zeige Topic-spezifische Peers
				topicPeers := topic.ListPeers()
				log.Printf("Topic %s has %d subscribed peers", topicStr, len(topicPeers))
			}
		}
	}()



	for {
		msg, err := sub.Next(ctx)
		if err != nil { 
			log.Printf("Error receiving message: %v", err)
			break 
		}

		log.Printf("🎉 RECEIVED MESSAGE! From: %s, Size: %d bytes, Topic: %s", 
			msg.ReceivedFrom.String(), len(msg.Data), topicStr)

		raw := msg.Data
		rawHash := sha256.Sum256(raw)
		dec, err := snappy.Decode(nil, raw)
		if err != nil {
			log.Printf("⚠️  Invalid snappy compression, dropping message: %v", err)
			continue
		}
		
		log.Printf("✅ Valid preconfirmation decoded! Decompressed size: %d bytes", len(dec))

		// Decode by hf-version
		if len(dec) < 65 { continue }
		sigBytes := dec[:65]
		offset := 65

		var parentBBR []byte
		if hf >= 2 { // v3/v4 have parentBeaconBlockRoot
			if len(dec) < offset+32 { continue }
			parentBBR = dec[offset : offset+32]
			offset += 32
		}
		payload := dec[offset:]
		if len(payload) == 0 { continue }

		// Build signing message: keccak( domain(32x0) || chain_id(be-uint256) || keccak(payload or parentBBR||payload) )
		domain := make([]byte, 32)
		body := payload
		if hf >= 2 {
			body = append(parentBBR, payload...)
		}
		payloadHash := keccak(body)

		msgBuf := append(domain, beUint256FromChainID(chainID)...)
		msgBuf = append(msgBuf, payloadHash...)
		msgHash := keccak(msgBuf)

		// Recover signer
		if sigBytes[64] > 1 { continue } // y_parity guard
		sig := make([]byte, 65)
		copy(sig, sigBytes)
		// go-ethereum expects V in {27,28}
		sig[64] = 27 + sig[64]
		pubkey, err := ethcrypto.SigToPub(msgHash, sig)
		if err != nil { continue }
		addr := ethcrypto.PubkeyToAddress(*pubkey)

		// Write files
		ts := time.Now().Unix()
		base := fmt.Sprintf("%d_%x", ts, rawHash[:8])
		rawPath := filepath.Join(outDir, base+".raw")        // original pubsub bytes (snappy-compressed)
		metaPath := filepath.Join(outDir, base+".json")      // metadata
		decPath := filepath.Join(outDir, base+".bin")        // decompressed concat (sig || [parentBBR] || payload)

		_ = os.WriteFile(rawPath, raw, 0o644)
		_ = os.WriteFile(decPath, dec, 0o644)

		meta := metaFile{
			ChainID:        fmt.Sprintf("%d", chainID),
			Hardfork:       hf,
			Topic:          topicStr,
			MsgID:          hex.EncodeToString([]byte(msg.ID)),
			ReceivedUnix:   ts,
			FromPeer:       msg.ReceivedFrom.String(),
			SignerAddress:  strings.ToLower(addr.Hex()),
			SignatureHex:   "0x"+hex.EncodeToString(sigBytes),
			ParentBBRHex:   hexOrEmpty(parentBBR),
			PayloadKeccak:  "0x"+hex.EncodeToString(payloadHash),
			RawSHA256:      "0x"+hex.EncodeToString(rawHash[:]),
			DecompressedSz: len(dec),
		}
		b, _ := json.MarshalIndent(&meta, "", "  ")
		_ = os.WriteFile(metaPath, b, 0o644)
	}
}

type multiAddrs []string
func (m *multiAddrs) String() string { return "" }
func (m *multiAddrs) Set(s string) error {
	*m = append(*m, s)
	return nil
}

func hexOrEmpty(b []byte) string {
	if len(b) == 0 { return "" }
	return "0x"+hex.EncodeToString(b)
}

// convertEnodeToMultiaddr konvertiert eine enode:// URL zu einer libp2p Multiaddr
func convertEnodeToMultiaddr(enodeURL string) (ma.Multiaddr, error) {
	if !strings.HasPrefix(enodeURL, "enode://") {
		return nil, fmt.Errorf("invalid enode URL: %s", enodeURL)
	}

	// Entferne "enode://" Präfix
	nodeInfo := strings.TrimPrefix(enodeURL, "enode://")
	
	// Trenne node_id von ip:port
	parts := strings.Split(nodeInfo, "@")
	if len(parts) != 2 {
		return nil, fmt.Errorf("invalid enode format: %s", enodeURL)
	}
	
	nodeID := parts[0]
	ipPort := parts[1]
	
	// Trenne IP und Port
	lastColon := strings.LastIndex(ipPort, ":")
	if lastColon == -1 {
		return nil, fmt.Errorf("invalid ip:port format: %s", ipPort)
	}
	
	ip := ipPort[:lastColon]
	port := ipPort[lastColon+1:]
	
	// Konvertiere Ethereum Node ID zu libp2p Peer ID
	// Ethereum node IDs sind 64 Hex-Zeichen (32 Bytes)
	if len(nodeID) != 128 { // 64 bytes * 2 hex chars
		return nil, fmt.Errorf("invalid node ID length: %d", len(nodeID))
	}
	
	nodeIDBytes, err := hex.DecodeString(nodeID)
	if err != nil {
		return nil, fmt.Errorf("invalid hex node ID: %v", err)
	}
	
	// Konvertiere Ethereum secp256k1 Public Key zu libp2p Peer ID
	// Ethereum enode ID ist der Keccak-256 Hash des uncompressed public key (ohne 0x04 Präfix)
	// Für libp2p brauchen wir den vollen public key
	
	// Da wir nur den Hash haben, können wir nicht den originalen public key rekonstruieren
	// Stattdessen erstellen wir eine identity-basierte Peer ID aus dem Hash
	
	// Erstelle einen identity multihash aus dem node ID
	mh, err := multihash.EncodeName(nodeIDBytes, "identity")
	if err != nil {
		return nil, fmt.Errorf("failed to create multihash: %v", err)
	}
	
	// Konvertiere zu Peer ID
	peerID, err := peer.IDFromBytes(mh)
	if err != nil {
		return nil, fmt.Errorf("failed to create peer ID: %v", err)
	}
	
	// Erstelle Multiaddr
	addrStr := fmt.Sprintf("/ip4/%s/tcp/%s/p2p/%s", ip, port, peerID.String())
	return ma.NewMultiaddr(addrStr)
}

// parseBootnode parst eine Bootnode-Adresse (enode, ENR oder Multiaddr)
func parseBootnode(addr string) (ma.Multiaddr, error) {
	if strings.HasPrefix(addr, "enode://") {
		return convertEnodeToMultiaddr(addr)
	}
	if strings.HasPrefix(addr, "enr:") {
		return nil, fmt.Errorf("ENR format not yet supported, please use enode or multiaddr format")
	}
	
	return ma.NewMultiaddr(addr)
}

// verifySequencerSignature verifiziert die Sequencer-Signatur nach OP Stack Standard
func verifySequencerSignature(data []byte, sig []byte, expectedSigner string, chainID uint64) (string, error) {
	// Berechne Signing Message nach OP Stack Standard:
	// keccak256(domain(32x0) || chain_id(be-uint256) || keccak(payload))
	domain := make([]byte, 32) // 32 zero bytes
	
	// Chain ID als big-endian uint256
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

// httpPreconfCapture führt HTTP-basierte Preconf-Capture durch
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

// processHTTPPreconf verarbeitet eine HTTP-Preconf und speichert sie
func processHTTPPreconf(preconf httpPreconfResponse, chainID uint64, hf int, outDir, endpoint string, verifySignatures bool, storage *preconfStorage) error {
	// Decode hex data
	dataBytes, err := hex.DecodeString(strings.TrimPrefix(preconf.Data, "0x"))
	if err != nil {
		return fmt.Errorf("invalid hex data: %v", err)
	}
	
	// Create signature bytes (r + s + v) with proper padding
	rHex := strings.TrimPrefix(preconf.Signature.R, "0x")
	sHex := strings.TrimPrefix(preconf.Signature.S, "0x")
	
	// Pad to 64 chars (32 bytes) if needed
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
	copy(sigBytes[32-len(rBytes):32], rBytes)     // Right-pad r to 32 bytes
	copy(sigBytes[64-len(sBytes):64], sBytes)     // Right-pad s to 32 bytes
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
	// Format: block_{chainID}_{blockNumber} (ohne timestamp für deterministische C-Zugriffe)
	// Falls mehrere Preconfs für denselben Block existieren, verwende die neueste
	ts := time.Now().Unix()
	base := fmt.Sprintf("block_%d_%d", chainID, blockNumber)
	rawPath := filepath.Join(outDir, base+".raw")
	metaPath := filepath.Join(outDir, base+".json")
	
	// Prüfe ob bereits eine Preconf für diesen Block existiert
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
	
	if err := os.WriteFile(rawPath, combinedData, 0o644); err != nil {
		return fmt.Errorf("failed to write raw file: %v", err)
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
	
	if err := os.WriteFile(metaPath, metaJSON, 0o644); err != nil {
		return fmt.Errorf("failed to write meta file: %v", err)
	}
	
	// Füge zur Storage hinzu für schnellen Zugriff
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

// newPreconfStorage erstellt einen neuen Preconf-Storage mit automatischem Cleanup
func newPreconfStorage(chainID uint64, baseDir string, maxAge time.Duration) *preconfStorage {
	storage := &preconfStorage{
		blockIndex:    make(map[uint64]*preconfEntry),
		chainID:       chainID,
		baseDir:       baseDir,
		maxAge:        maxAge,
		cleanupTicker: time.NewTicker(5 * time.Minute), // Cleanup alle 5 Minuten
	}
	
	// Lade existierende Preconfs beim Start
	storage.loadExistingPreconfs()
	
	// Starte Cleanup-Goroutine
	go storage.cleanupLoop()
	
	return storage
}

// loadExistingPreconfs lädt existierende Preconf-Dateien beim Start
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
		// Prüfe ob Datei noch existiert
		if _, err := os.Stat(entry.FilePath); err == nil {
			s.blockIndex[entry.BlockNumber] = entry
			loaded++
		}
	}
	
	log.Printf("📚 Loaded %d existing preconfs from block index", loaded)
}

// addPreconf fügt eine neue Preconf zum Index hinzu
func (s *preconfStorage) addPreconf(entry *preconfEntry) {
	s.mu.Lock()
	defer s.mu.Unlock()
	
	s.blockIndex[entry.BlockNumber] = entry
	log.Printf("📦 Added preconf for block %d to index (total: %d)", entry.BlockNumber, len(s.blockIndex))
}

// getPreconfForBlock sucht eine Preconf für einen spezifischen Block
func (s *preconfStorage) getPreconfForBlock(blockNumber uint64) (*preconfEntry, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	
	entry, exists := s.blockIndex[blockNumber]
	return entry, exists
}

// cleanup entfernt Preconfs älter als maxAge
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
		// Lösche Dateien
		os.Remove(entry.FilePath)
		os.Remove(strings.Replace(entry.FilePath, ".raw", ".json", 1))
		// Entferne aus Index
		delete(s.blockIndex, blockNum)
	}
	
	log.Printf("📦 Block index now contains %d preconfs", len(s.blockIndex))
}

// cleanupLoop entfernt alte Preconfs automatisch
func (s *preconfStorage) cleanupLoop() {
	for range s.cleanupTicker.C {
		s.cleanup()
	}
}

// close stoppt den Storage und führt finales Cleanup durch
func (s *preconfStorage) close() {
	if s.cleanupTicker != nil {
		s.cleanupTicker.Stop()
	}
	s.cleanup() // Final cleanup
	log.Printf("📦 Preconf storage closed")
}
