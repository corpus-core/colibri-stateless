// gossip.go - libp2p/DHT-based preconfirmation capture for OP Stack chains
package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/golang/snappy"
	libp2p "github.com/libp2p/go-libp2p"
	"github.com/libp2p/go-libp2p/core/peer"
	"github.com/libp2p/go-libp2p/p2p/discovery/routing"
	"github.com/libp2p/go-libp2p/p2p/discovery/util"
	dht "github.com/libp2p/go-libp2p-kad-dht"
	pubsub "github.com/libp2p/go-libp2p-pubsub"
	pb "github.com/libp2p/go-libp2p-pubsub/pb"
	ma "github.com/multiformats/go-multiaddr"
	"github.com/multiformats/go-multihash"
	ethcrypto "github.com/ethereum/go-ethereum/crypto"
)

// msgIDFn creates the OP Stack message ID function for pubsub
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

// convertEnodeToMultiaddr converts an enode:// URL to a libp2p Multiaddr
func convertEnodeToMultiaddr(enodeURL string) (ma.Multiaddr, error) {
	if !strings.HasPrefix(enodeURL, "enode://") {
		return nil, fmt.Errorf("invalid enode URL: %s", enodeURL)
	}

	// Remove "enode://" prefix
	nodeInfo := strings.TrimPrefix(enodeURL, "enode://")

	// Separate node_id from ip:port
	parts := strings.Split(nodeInfo, "@")
	if len(parts) != 2 {
		return nil, fmt.Errorf("invalid enode format: %s", enodeURL)
	}

	nodeID := parts[0]
	ipPort := parts[1]

	// Separate IP and port
	lastColon := strings.LastIndex(ipPort, ":")
	if lastColon == -1 {
		return nil, fmt.Errorf("invalid ip:port format: %s", ipPort)
	}

	ip := ipPort[:lastColon]
	port := ipPort[lastColon+1:]

	// Convert Ethereum Node ID to libp2p Peer ID
	// Ethereum node IDs are 64 hex characters (32 bytes)
	if len(nodeID) != 128 { // 64 bytes * 2 hex chars
		return nil, fmt.Errorf("invalid node ID length: %d", len(nodeID))
	}

	nodeIDBytes, err := hex.DecodeString(nodeID)
	if err != nil {
		return nil, fmt.Errorf("invalid hex node ID: %v", err)
	}

	// Convert Ethereum secp256k1 Public Key to libp2p Peer ID
	// Ethereum enode ID is the Keccak-256 hash of the uncompressed public key (without 0x04 prefix)
	// For libp2p we need the full public key

	// Since we only have the hash, we can't reconstruct the original public key
	// Instead, we create an identity-based Peer ID from the hash

	// Create an identity multihash from the node ID
	mh, err := multihash.EncodeName(nodeIDBytes, "identity")
	if err != nil {
		return nil, fmt.Errorf("failed to create multihash: %v", err)
	}

	// Convert to Peer ID
	peerID, err := peer.IDFromBytes(mh)
	if err != nil {
		return nil, fmt.Errorf("failed to create peer ID: %v", err)
	}

	// Create Multiaddr
	addrStr := fmt.Sprintf("/ip4/%s/tcp/%s/p2p/%s", ip, port, peerID.String())
	return ma.NewMultiaddr(addrStr)
}

// parseBootnode parses a bootnode address (enode, ENR or Multiaddr)
func parseBootnode(addr string) (ma.Multiaddr, error) {
	if strings.HasPrefix(addr, "enode://") {
		return convertEnodeToMultiaddr(addr)
	}
	if strings.HasPrefix(addr, "enr:") {
		return nil, fmt.Errorf("ENR format not yet supported, please use enode or multiaddr format")
	}

	return ma.NewMultiaddr(addr)
}

// gossipPreconfCapture performs libp2p/DHT-based preconf capture
func gossipPreconfCapture(ctx context.Context, chainID uint64, hf int, outDir string, boots multiAddrs) error {
	log.Printf("🔗 Starting libp2p-based preconf capture")

	// Create libp2p host with DHT support
	h, err := libp2p.New(
		libp2p.EnableRelay(),
		libp2p.EnableNATService(),
		libp2p.EnableHolePunching(),
	)
	if err != nil {
		return fmt.Errorf("libp2p host: %v", err)
	}

	log.Printf("libp2p host ID: %s", h.ID().String())
	log.Printf("libp2p addresses: %v", h.Addrs())

	// Initialize DHT for peer discovery with explicit bootstrap nodes
	kdht, err := dht.New(ctx, h, dht.Mode(dht.ModeAuto))
	if err != nil {
		return fmt.Errorf("DHT initialization: %v", err)
	}

	// Connect to known libp2p bootstrap nodes
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

	// Create discovery service
	disc := routing.NewRoutingDiscovery(kdht)

	// Wait briefly for DHT bootstrap
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
	if err != nil {
		return fmt.Errorf("gossipsub: %v", err)
	}

	// Legacy bootnode connections (as fallback)
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
	if err != nil {
		return fmt.Errorf("join topic: %v", err)
	}
	sub, err := topic.Subscribe()
	if err != nil {
		return fmt.Errorf("subscribe: %v", err)
	}

	log.Printf("Subscribed to topic: %s", topicStr)

	// DHT-based peer discovery for the specific topic
	go func() {
		log.Printf("Starting DHT-based peer discovery for topic: %s", topicStr)

		// Advertise our participation in the topic
		util.Advertise(ctx, disc, topicStr)
		log.Printf("Advertising participation in topic: %s", topicStr)

		// Continuous peer discovery
		ticker := time.NewTicker(30 * time.Second)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				// Search for peers for this topic
				log.Printf("Searching for peers interested in topic: %s", topicStr)
				peerCh, err := disc.FindPeers(ctx, topicStr)
				if err != nil {
					log.Printf("DHT peer discovery failed: %v", err)
					continue
				}

				// Connect to found peers
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

				// Show current peer statistics
				connectedPeers := h.Network().Peers()
				log.Printf("Currently connected to %d peers total", len(connectedPeers))

				// Show topic-specific peers
				topicPeers := topic.ListPeers()
				log.Printf("Topic %s has %d subscribed peers", topicStr, len(topicPeers))
			}
		}
	}()

	// Process incoming messages
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
		if len(dec) < 65 {
			continue
		}
		sigBytes := dec[:65]
		offset := 65

		var parentBBR []byte
		if hf >= 2 { // v3/v4 have parentBeaconBlockRoot
			if len(dec) < offset+32 {
				continue
			}
			parentBBR = dec[offset : offset+32]
			offset += 32
		}
		payload := dec[offset:]
		if len(payload) == 0 {
			continue
		}

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
		if sigBytes[64] > 1 {
			continue // y_parity guard
		}
		sig := make([]byte, 65)
		copy(sig, sigBytes)
		// go-ethereum expects V in {27,28}
		sig[64] = 27 + sig[64]
		pubkey, err := ethcrypto.SigToPub(msgHash, sig)
		if err != nil {
			continue
		}
		addr := ethcrypto.PubkeyToAddress(*pubkey)

		// Write files
		ts := time.Now().Unix()
		base := fmt.Sprintf("%d_%x", ts, rawHash[:8])
		rawPath := filepath.Join(outDir, base+".raw")  // original pubsub bytes (snappy-compressed)
		metaPath := filepath.Join(outDir, base+".json") // metadata
		decPath := filepath.Join(outDir, base+".bin")   // decompressed concat (sig || [parentBBR] || payload)

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
			SignatureHex:   "0x" + hex.EncodeToString(sigBytes),
			ParentBBRHex:   hexOrEmpty(parentBBR),
			PayloadKeccak:  "0x" + hex.EncodeToString(payloadHash),
			RawSHA256:      "0x" + hex.EncodeToString(rawHash[:]),
			DecompressedSz: len(dec),
		}
		b, _ := json.MarshalIndent(&meta, "", "  ")
		_ = os.WriteFile(metaPath, b, 0o644)
	}

	return nil
}
