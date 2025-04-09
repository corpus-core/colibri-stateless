#include "bytes.h"  // For buffer_alloc etc.
#include "plugin.h" // Include the definition of storage_plugin_t and buffer_t
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Global Variables to Cache JNI Objects ---
// It's crucial to cache these for performance, especially the JVM.
// Initialization MUST happen via JNI_OnLoad or a dedicated init function.
static JavaVM*   g_vm                    = NULL;
static jclass    g_storageBridgeClass    = NULL;
static jobject   g_storageBridgeInstance = NULL; // The singleton instance
static jmethodID g_storageGetMethod      = NULL;
static jmethodID g_storageSetMethod      = NULL;
static jmethodID g_storageDeleteMethod   = NULL;

// Helper function to get JNIEnv* for the current thread
// Returns NULL if thread not attached or error occurs
static JNIEnv* getJniEnv() {
  JNIEnv* env = NULL;
  if (g_vm == NULL) {
    fprintf(stderr, "JNI Bridge Error: JavaVM not initialized.\n");
    return NULL;
  }
  // Check if the current thread is attached to the VM
  jint getEnvStat = (*g_vm)->GetEnv(g_vm, (void**) &env, JNI_VERSION_1_6);

  if (getEnvStat == JNI_EDETACHED) {
    // fprintf(stderr, "JNI Bridge Info: Thread not attached, attempting to attach.\n");
    //  We shouldn't automatically attach daemon threads. If this function
    //  can be called from a non-Java thread, explicit attachment might be needed,
    //  but for callbacks originating from Java/Kotlin, it should already be attached.
    //  If calls can come from pure native threads, use AttachCurrentThread.
    //  For now, assume calls originate from attached threads.
    //  jint attachStat = (*g_vm)->AttachCurrentThread(g_vm, &env, NULL);
    //  if (attachStat != JNI_OK) {
    //      fprintf(stderr, "JNI Bridge Error: Failed to attach current thread.\n");
    //      return NULL;
    //  }
    //  fprintf(stderr, "JNI Bridge Info: Thread attached successfully.\n");
    fprintf(stderr, "JNI Bridge Error: Callback attempted from detached thread.\n");
    return NULL; // Don't attach automatically here, might hide issues
  }
  else if (getEnvStat == JNI_EVERSION) {
    fprintf(stderr, "JNI Bridge Error: JNI version not supported.\n");
    return NULL;
  }
  else if (getEnvStat != JNI_OK) {
    fprintf(stderr, "JNI Bridge Error: GetEnv failed with unknown error %d.\n", getEnvStat);
    return NULL;
  }
  // getEnvStat == JNI_OK means the thread is already attached.
  return env;
}

// --- JNI Bridge Functions (Called by C core via function pointers) ---

// Bridge for storage_plugin_t->get
static bool bridge_storage_get(char* key, buffer_t* buffer) {
  JNIEnv* env = getJniEnv();
  if (!env || !g_storageBridgeInstance || !g_storageGetMethod) {
    fprintf(stderr, "JNI get bridge error: JNI Env or bridge components not ready.\n");
    return false; // Indicate failure
  }

  jstring jKey = (*env)->NewStringUTF(env, key);
  if (!jKey) {
    fprintf(stderr, "JNI get bridge error: Failed to create Java string for key '%s'.\n", key);
    return false;
  }

  // Call the Kotlin StorageBridge.implementation.get(key) method
  jbyteArray jResultBytes = (jbyteArray) (*env)->CallObjectMethod(env, g_storageBridgeInstance, g_storageGetMethod, jKey);

  (*env)->DeleteLocalRef(env, jKey); // Clean up local reference

  if ((*env)->ExceptionCheck(env)) {
    fprintf(stderr, "JNI get bridge error: Exception occurred during Kotlin 'get' call for key '%s'.\n", key);
    (*env)->ExceptionDescribe(env);
    (*env)->ExceptionClear(env);
    return false;
  }

  if (jResultBytes == NULL) {
    // Kotlin function returned null (key not found)
    return false;
  }

  // Copy data from Java byte[] to C buffer_t
  jbyte* elements = (*env)->GetByteArrayElements(env, jResultBytes, NULL);
  if (!elements) {
    fprintf(stderr, "JNI get bridge error: Failed to get byte array elements for key '%s'.\n", key);
    (*env)->DeleteLocalRef(env, jResultBytes);
    return false;
  }

  // Create a temporary bytes_t pointing to the JNI elements
  bytes_t data_to_append;
  data_to_append.data = (uint8_t*) elements;
  data_to_append.len  = (*env)->GetArrayLength(env, jResultBytes);

  // Append the data to the provided buffer
  // buffer_append handles resizing the buffer if necessary
  uint32_t result = buffer_append(buffer, data_to_append);

  // Release the JNI byte array elements (use JNI_ABORT since we only read)
  (*env)->ReleaseByteArrayElements(env, jResultBytes, elements, JNI_ABORT);
  (*env)->DeleteLocalRef(env, jResultBytes); // Clean up local reference

  return true; // Success
}

// Bridge for storage_plugin_t->set
static void bridge_storage_set(char* key, bytes_t value) {
  JNIEnv* env = getJniEnv();
  if (!env || !g_storageBridgeInstance || !g_storageSetMethod) {
    fprintf(stderr, "JNI set bridge error: JNI Env or bridge components not ready.\n");
    return;
  }

  jstring jKey = (*env)->NewStringUTF(env, key);
  if (!jKey) {
    fprintf(stderr, "JNI set bridge error: Failed to create Java string for key '%s'.\n", key);
    return;
  }

  jbyteArray jValue = (*env)->NewByteArray(env, (jsize) value.len);
  if (!jValue) {
    fprintf(stderr, "JNI set bridge error: Failed to create Java byte array for key '%s'.\n", key);
    (*env)->DeleteLocalRef(env, jKey);
    return;
  }
  (*env)->SetByteArrayRegion(env, jValue, 0, (jsize) value.len, (const jbyte*) value.data);

  // Call the Kotlin StorageBridge.implementation.set(key, value) method
  (*env)->CallVoidMethod(env, g_storageBridgeInstance, g_storageSetMethod, jKey, jValue);

  // Clean up local references
  (*env)->DeleteLocalRef(env, jKey);
  (*env)->DeleteLocalRef(env, jValue);

  if ((*env)->ExceptionCheck(env)) {
    fprintf(stderr, "JNI set bridge error: Exception occurred during Kotlin 'set' call for key '%s'.\n", key);
    (*env)->ExceptionDescribe(env);
    (*env)->ExceptionClear(env);
  }
}

// Bridge for storage_plugin_t->del
static void bridge_storage_del(char* key) {
  JNIEnv* env = getJniEnv();
  if (!env || !g_storageBridgeInstance || !g_storageDeleteMethod) {
    fprintf(stderr, "JNI delete bridge error: JNI Env or bridge components not ready.\n");
    return;
  }

  jstring jKey = (*env)->NewStringUTF(env, key);
  if (!jKey) {
    fprintf(stderr, "JNI delete bridge error: Failed to create Java string for key '%s'.\n", key);
    return;
  }

  // Call the Kotlin StorageBridge.implementation.delete(key) method
  (*env)->CallVoidMethod(env, g_storageBridgeInstance, g_storageDeleteMethod, jKey);

  (*env)->DeleteLocalRef(env, jKey); // Clean up local reference

  if ((*env)->ExceptionCheck(env)) {
    fprintf(stderr, "JNI delete bridge error: Exception occurred during Kotlin 'delete' call for key '%s'.\n", key);
    (*env)->ExceptionDescribe(env);
    (*env)->ExceptionClear(env);
  }
}

// --- Initialization Function (Called from Java/Kotlin via JNI) ---

// This function MUST be called *after* the JVM is initialized and
// *before* the C core tries to use the storage plugin.
// It finds the StorageBridge class and caches method IDs.
// We'll expose this via a simple native method in c4.java/c4JNI.
JNIEXPORT void JNICALL Java_com_corpuscore_colibri_c4JNI_nativeInitializeBridge(JNIEnv* env, jclass clazz) {
  // Cache StorageBridge class (use FindClass)
  jclass localBridgeClass = (*env)->FindClass(env, "com/corpuscore/colibri/StorageBridge");
  if (!localBridgeClass) {
    fprintf(stderr, "JNI Bridge Init Error: Cannot find StorageBridge class.\n");
    // Cannot proceed without the class
    return;
  }
  // Create a global reference to the class
  g_storageBridgeClass = (jclass) (*env)->NewGlobalRef(env, localBridgeClass);
  (*env)->DeleteLocalRef(env, localBridgeClass); // Delete local ref
  if (!g_storageBridgeClass) {
    fprintf(stderr, "JNI Bridge Init Error: Cannot create global ref for StorageBridge class.\n");
    return;
  }

  // Get the static 'implementation' field ID from StorageBridge class
  jfieldID implField = (*env)->GetStaticFieldID(env, g_storageBridgeClass, "implementation", "Lcom/corpuscore/colibri/ColibriStorage;");
  if (!implField) {
    fprintf(stderr, "JNI Bridge Init Error: Cannot find 'implementation' field in StorageBridge.\n");
    // Clean up global ref if initialization fails partially
    (*env)->DeleteGlobalRef(env, g_storageBridgeClass);
    g_storageBridgeClass = NULL;
    return;
  }

  // Get the singleton instance from the static field
  jobject localBridgeInstance = (*env)->GetStaticObjectField(env, g_storageBridgeClass, implField);
  if (!localBridgeInstance) {
    // This is okay initially if registerStorage hasn't been called yet.
    // The bridge functions (get/set/del) check if g_storageBridgeInstance is null.
    fprintf(stderr, "JNI Bridge Init Info: StorageBridge.implementation is initially null.\n");
    g_storageBridgeInstance = NULL; // Ensure it's explicitly null
  }
  else {
    // Create a global reference to the instance for use in callbacks
    g_storageBridgeInstance = (*env)->NewGlobalRef(env, localBridgeInstance);
    (*env)->DeleteLocalRef(env, localBridgeInstance); // Delete local ref
    if (!g_storageBridgeInstance) {
      fprintf(stderr, "JNI Bridge Init Error: Cannot create global ref for StorageBridge instance.\n");
      (*env)->DeleteGlobalRef(env, g_storageBridgeClass);
      g_storageBridgeClass = NULL;
      return;
    }
    fprintf(stderr, "JNI Bridge Init Info: StorageBridge instance cached.\n");
  }

  // Get Method IDs for ColibriStorage interface methods (use the interface class!)
  jclass storageInterfaceClass = (*env)->FindClass(env, "com/corpuscore/colibri/ColibriStorage");
  if (!storageInterfaceClass) {
    fprintf(stderr, "JNI Bridge Init Error: Cannot find ColibriStorage interface.\n");
    // Clean up global refs
    if (g_storageBridgeInstance) (*env)->DeleteGlobalRef(env, g_storageBridgeInstance);
    if (g_storageBridgeClass) (*env)->DeleteGlobalRef(env, g_storageBridgeClass);
    g_storageBridgeInstance = NULL;
    g_storageBridgeClass    = NULL;
    return;
  }

  g_storageGetMethod    = (*env)->GetMethodID(env, storageInterfaceClass, "get", "(Ljava/lang/String;)[B");
  g_storageSetMethod    = (*env)->GetMethodID(env, storageInterfaceClass, "set", "(Ljava/lang/String;[B)V");
  g_storageDeleteMethod = (*env)->GetMethodID(env, storageInterfaceClass, "delete", "(Ljava/lang/String;)V"); // Use "delete"

  // We don't need the interface class reference anymore
  (*env)->DeleteLocalRef(env, storageInterfaceClass);

  if (!g_storageGetMethod || !g_storageSetMethod || !g_storageDeleteMethod) {
    fprintf(stderr, "JNI Bridge Init Error: Failed to find one or more methods in ColibriStorage interface.\n");
    // Clean up global refs
    if (g_storageBridgeInstance) (*env)->DeleteGlobalRef(env, g_storageBridgeInstance);
    if (g_storageBridgeClass) (*env)->DeleteGlobalRef(env, g_storageBridgeClass);
    g_storageBridgeInstance = NULL;
    g_storageBridgeClass    = NULL;
    g_storageGetMethod      = NULL;
    g_storageSetMethod      = NULL;
    g_storageDeleteMethod   = NULL;
    return;
  }

  // --- Configure C Core ---
  // Create the plugin struct with pointers to our bridge functions
  storage_plugin_t plugin;
  plugin.get             = bridge_storage_get;
  plugin.set             = bridge_storage_set;
  plugin.del             = bridge_storage_del;
  plugin.max_sync_states = 10; // Example value, configure as needed

  // Set the configuration in the C core
  c4_set_storage_config(&plugin);

  fprintf(stderr, "JNI Bridge Initialized Successfully.\n");
}

// --- JNI_OnLoad --- (Optional but recommended)
// Cache the JavaVM pointer when the library is loaded.
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  g_vm = vm;
  JNIEnv* env;
  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK) {
    fprintf(stderr, "JNI_OnLoad Error: Failed to get JNI Env.\n");
    return JNI_EVERSION; // Version error or other issue
  }
  // Perform initializations that require JNIEnv but don't depend on specific classes yet, if any.
  // Call the main initialization here OR rely on explicit call from Kotlin.
  // Calling explicitly from Kotlin might be safer to ensure class loading order.
  // Java_com_corpuscore_colibri_c4JNI_nativeInitializeBridge(env, NULL); // Don't call automatically?

  fprintf(stderr, "JNI_OnLoad completed.\n");
  return JNI_VERSION_1_6; // Use the JNI version you're targeting
}

// --- JNI_OnUnload --- (Optional)
// Clean up global references when the library is unloaded.
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
  JNIEnv* env = NULL;
  // It's possible GetEnv fails during unload, proceed cautiously
  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) == JNI_OK && env != NULL) {
    if (g_storageBridgeInstance) {
      (*env)->DeleteGlobalRef(env, g_storageBridgeInstance);
    }
    if (g_storageBridgeClass) {
      (*env)->DeleteGlobalRef(env, g_storageBridgeClass);
    }
  }
  else {
    fprintf(stderr, "JNI_OnUnload Warning: Could not get JNIEnv to clean up global refs.\n");
  }
  g_storageBridgeInstance = NULL;
  g_storageBridgeClass    = NULL;
  g_storageGetMethod      = NULL;
  g_storageSetMethod      = NULL;
  g_storageDeleteMethod   = NULL;
  g_vm                    = NULL;
  fprintf(stderr, "JNI_OnUnload completed.\n");
}
