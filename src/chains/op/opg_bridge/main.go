// cmd/opg_bridge/main.go
package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"math/big"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	pubsub "github.com/libp2p/go-libp2p-pubsub"
	pb "github.com/libp2p/go-libp2p-pubsub/pb"
	libp2p "github.com/libp2p/go-libp2p"
	"github.com/libp2p/go-libp2p/core/peer"
	ma "github.com/multiformats/go-multiaddr"

	"github.com/golang/snappy"
	"github.com/ethereum/go-ethereum/crypto"
	"golang.org/x/crypto/sha3"
)

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
	flag.Uint64Var(&chainID, "chain-id", 10, "OP Stack chain id (e.g. 10 for OP Mainnet, 8453 for Base)")
	flag.IntVar(&hf, "hf", 3, "hardfork version (0=v1,1=v2,2=v3,3=v4/Isthmus)")
	flag.Var(&boots, "bootnode", "libp2p multiaddr of peer (repeatable)")
	flag.StringVar(&outDir, "out-dir", "./preconfs", "directory to write captured messages")
	flag.Parse()

	if err := os.MkdirAll(outDir, 0o755); err != nil {
		log.Fatalf("mkdir out-dir: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	h, err := libp2p.New(
		libp2p.EnableRelay(),
	)
	if err != nil { log.Fatalf("libp2p host: %v", err) }

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

	log.Printf("listening on topic %s", topicStr)
	log.Printf("libp2p host ID: %s", h.ID().String())
	log.Printf("libp2p addresses: %v", h.Addrs())

	// graceful shutdown
	sigc := make(chan os.Signal, 1)
	signal.Notify(sigc, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-sigc
		cancel()
	}()

	for {
		msg, err := sub.Next(ctx)
		if err != nil { break }

		raw := msg.Data
		rawHash := sha256.Sum256(raw)
		dec, err := snappy.Decode(nil, raw)
		if err != nil {
			// invalid per spec ⇒ drop
			continue
		}

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
		pubkey, err := crypto.SigToPub(msgHash, sig)
		if err != nil { continue }
		addr := crypto.PubkeyToAddress(*pubkey)

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
	
	// Für libp2p verwenden wir eine vereinfachte Konvertierung
	// In der Praxis sollte man die kryptographische Konvertierung verwenden
	// Hier nehmen wir die ersten 20 Bytes und erstellen eine Base58-kodierte Peer-ID
	nodeIDBytes, err := hex.DecodeString(nodeID)
	if err != nil {
		return nil, fmt.Errorf("invalid hex node ID: %v", err)
	}
	
	// Erstelle eine libp2p-kompatible Peer-ID aus den ersten 20 Bytes
	// Präfix für secp256k1 public key (0x1205 = identity multihash, 20 bytes)
	peerIDBytes := make([]byte, 22)
	peerIDBytes[0] = 0x12 // identity multihash
	peerIDBytes[1] = 0x14 // 20 bytes length
	copy(peerIDBytes[2:], nodeIDBytes[:20])
	
	// Base58 encode für libp2p Peer ID
	peerIDStr := "12D3Koo" + hex.EncodeToString(peerIDBytes[2:12]) // Vereinfachte Konvertierung
	
	// Erstelle Multiaddr
	addrStr := fmt.Sprintf("/ip4/%s/tcp/%s/p2p/%s", ip, port, peerIDStr)
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
