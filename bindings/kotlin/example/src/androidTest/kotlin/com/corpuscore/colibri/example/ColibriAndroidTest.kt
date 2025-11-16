package com.corpuscore.colibri.example

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.corpuscore.colibri.Colibri
import com.corpuscore.colibri.MethodType
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.math.BigInteger

/**
 * Android-specific test for Colibri to compare behavior with JAR tests
 */
@RunWith(AndroidJUnit4::class)
class ColibriAndroidTest {

    @Test
    fun testBasicJNIFunctionality() {
        // Test basic JNI calls that should work
        val colibri = Colibri()
        
        runBlocking {
            val methodType = colibri.getMethodSupport("eth_blockNumber")
            assertEquals("Method should be PROOFABLE", MethodType.PROOFABLE, methodType)
        }
    }

    @Test
    fun testMockRequestHandler() {
        // Test with mock data to see if the problem is in the JNI calls or in HTTP handling
        val mockHandler: (Map<String, Any?>) -> ByteArray? = { requestDetails ->
            // Simple mock response
            """{"result":"0x123456"}""".toByteArray()
        }
        
        val colibri = Colibri(requestHandler = mockHandler)
        
        runBlocking {
            try {
                // This should trigger the mock handler and avoid real HTTP requests
                val result = colibri.rpc("eth_blockNumber", arrayOf())
                assertNotNull("Result should not be null", result)
                println("Mock test result: $result")
            } catch (e: Exception) {
                println("Mock test failed: ${e.message}")
                throw e
            }
        }
    }
    
    @Test  
    fun testWithBypassedErrorHandling() {
        // Test the current BYPASS version to verify it doesn't crash
        val colibri = Colibri()
        
        runBlocking {
            try {
                // This should use the BYPASS version and not crash
                val methodType = colibri.getMethodSupport("eth_blockNumber")
                assertEquals("Should identify proofable method", MethodType.PROOFABLE, methodType)
                
                // If we get here, basic JNI works on Android
                assertTrue("Basic JNI functionality works", true)
            } catch (e: Exception) {
                fail("Basic JNI should not fail: ${e.message}")
            }
        }
    }

    @Test
    fun testContextInfo() {
        // Print Android context information for debugging
        val appContext = InstrumentationRegistry.getInstrumentation().targetContext
        assertEquals("com.corpuscore.colibri.example", appContext.packageName)
        
        println("Android Test Context:")
        println("  Package: ${appContext.packageName}")
        println("  SDK: ${android.os.Build.VERSION.SDK_INT}")
        println("  ABI: ${android.os.Build.SUPPORTED_ABIS.joinToString()}")
    }
}