/*
 * Copyright (c) 2025 corpus.core
 * SPDX-License-Identifier: MIT
 */

package com.corpuscore.colibri

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.math.BigInteger

/**
 * Pure-JVM tests for generated [DefaultChains] lookups.
 * Does not load JNI / the native library.
 */
class DefaultChainsTest {

    private val knownIds = listOf(
        BigInteger.ONE,
        BigInteger.valueOf(11155111L),
        BigInteger.valueOf(100L),
        BigInteger.valueOf(10200L),
        BigInteger("7091047534"),
        BigInteger.valueOf(7091047534L),
    )

    @Test
    fun knownChainsHaveEndpoints() {
        for (id in knownIds) {
            assertTrue(DefaultChains.defaultProvers(id).isNotEmpty())
            assertTrue(DefaultChains.defaultEthRpcs(id).isNotEmpty())
            assertTrue(DefaultChains.defaultBeaconApis(id).isNotEmpty())
            assertTrue(DefaultChains.defaultCheckpointz(id).isNotEmpty())
        }
    }

    @Test
    fun cloudflareProverIsFirstAndDirectLbSecond() {
        val mainnet = DefaultChains.defaultProvers(BigInteger.ONE)
        assertEquals("https://mainnet.colibri-proof.tech", mainnet[0])
        assertEquals("https://mainnet1.colibri-proof.tech", mainnet[1])

        val sepolia = DefaultChains.defaultProvers(BigInteger.valueOf(11155111L))
        assertEquals("https://sepolia.colibri-proof.tech", sepolia[0])
        assertEquals("https://sepolia1.colibri-proof.tech", sepolia[1])

        val gnosis = DefaultChains.defaultProvers(BigInteger.valueOf(100L))
        assertEquals("https://gnosis.colibri-proof.tech", gnosis[0])
        assertEquals("https://gnosis1.colibri-proof.tech", gnosis[1])
    }

    @Test
    fun platabergetDefaults() {
        val id = BigInteger("7091047534")
        assertArrayEquals(
            arrayOf("https://plataberget.colibri-proof.tech"),
            DefaultChains.defaultProvers(id),
        )
        assertTrue(DefaultChains.defaultEthRpcs(id).single().contains("/execution"))
        assertTrue(DefaultChains.defaultBeaconApis(id).single().contains("/consensus"))
    }

    @Test
    fun unknownChainHasProverFallbackOnly() {
        val unknown = BigInteger.valueOf(999999L)
        assertTrue(DefaultChains.defaultProvers(unknown).isNotEmpty())
        assertTrue(DefaultChains.defaultEthRpcs(unknown).isEmpty())
        assertTrue(DefaultChains.defaultBeaconApis(unknown).isEmpty())
        assertTrue(DefaultChains.defaultCheckpointz(unknown).isEmpty())
    }
}
