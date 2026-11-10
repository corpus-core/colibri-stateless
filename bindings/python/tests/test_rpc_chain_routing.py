"""RPC list selection by request chain id."""

import pytest

from colibri.client import Colibri
from colibri.types import DataRequest, HTTPError


def _req(chain_id, method="eth_blockNumber"):
    return DataRequest(
        req_ptr=1,
        url="",
        method="POST",
        payload={"method": method, "params": []},
        chain_id=chain_id,
    )


def test_configured_chain_keeps_its_rpc_list(monkeypatch):
    client = Colibri(chain_id=1, eth_rpcs=["https://primary"])
    monkeypatch.setattr(client, "_l1_chain_id", lambda: 1)

    assert client._eth_rpc_servers(_req(1)) == ["https://primary"]


def test_l2_uses_explicit_l1_list(monkeypatch):
    client = Colibri(
        chain_id=8453,
        eth_rpcs=["https://l2"],
        l1_eth_rpcs=["https://l1"],
        oblivious_nodes=["https://tee"],
    )
    monkeypatch.setattr(client, "_l1_chain_id", lambda: 1)

    assert client._eth_rpc_servers(_req(8453)) == ["https://l2"]
    assert client._eth_rpc_servers(_req(8453, "eth_getProof")) == ["https://tee"]
    # Oblivious nodes belong to the configured chain only.
    assert client._eth_rpc_servers(_req(1, "eth_getProof")) == ["https://l1"]


def test_missing_l1_list_uses_mainnet_defaults(monkeypatch):
    client = Colibri(chain_id=8453, eth_rpcs=["https://l2"])
    monkeypatch.setattr(client, "_l1_chain_id", lambda: 1)

    servers = client._eth_rpc_servers(_req(1))
    assert servers
    assert "https://l2" not in servers


def test_other_chain_and_zero_are_errors(monkeypatch):
    client = Colibri(chain_id=8453, eth_rpcs=["https://l2"])
    monkeypatch.setattr(client, "_l1_chain_id", lambda: 1)

    with pytest.raises(HTTPError, match="chain_id 10"):
        client._eth_rpc_servers(_req(10))
    with pytest.raises(HTTPError, match="was not set"):
        client._eth_rpc_servers(_req(0))


def test_mainnet_keeps_primary_when_l1_list_is_also_set(monkeypatch):
    """Configured chain 1 and L1 id 1 must not pick the explicit L1 list."""
    client = Colibri(
        chain_id=1,
        eth_rpcs=["https://primary"],
        l1_eth_rpcs=["https://l1"],
        oblivious_nodes=["https://tee"],
    )
    monkeypatch.setattr(client, "_l1_chain_id", lambda: 1)

    assert client._eth_rpc_servers(_req(1)) == ["https://primary"]
    assert client._eth_rpc_servers(_req(1, "eth_getProof")) == ["https://tee"]


def test_empty_l1_list_uses_generated_defaults(monkeypatch):
    client = Colibri(chain_id=8453, eth_rpcs=["https://l2"], l1_eth_rpcs=[])
    monkeypatch.setattr(client, "_l1_chain_id", lambda: 1)

    servers = client._eth_rpc_servers(_req(1))
    assert servers
    assert "https://l2" not in servers
    assert client._eth_rpc_servers(_req(1, "eth_getProof")) == servers


def test_from_dict_chain_id_selects_the_list(monkeypatch):
    client = Colibri(
        chain_id=8453,
        eth_rpcs=["https://l2"],
        l1_eth_rpcs=["https://l1"],
    )
    monkeypatch.setattr(client, "_l1_chain_id", lambda: 1)

    def parsed(chain_id):
        body = {
            "req_ptr": 1,
            "type": "eth_rpc",
            "method": "POST",
            "payload": {"method": "eth_blockNumber", "params": []},
            "chain_id": chain_id,
        }
        return DataRequest.from_dict(body)

    assert client._eth_rpc_servers(parsed(8453)) == ["https://l2"]
    assert client._eth_rpc_servers(parsed(1)) == ["https://l1"]
    with pytest.raises(HTTPError, match="chain_id 10"):
        client._eth_rpc_servers(parsed(10))
    with pytest.raises(HTTPError, match="was not set"):
        client._eth_rpc_servers(parsed(0))
