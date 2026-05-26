// ============================================================================
// FILE: src/ollama_bridge.h
// PATH: /marisselle-teacher/src/ollama_bridge.h
// PURPOSE: C++ header for WASM exports - matches ollama-worker.js expectations
// ============================================================================

#ifndef OLLAMA_BRIDGE_H
#define OLLAMA_BRIDGE_H

#include <emscripten.h>
#include <emscripten/bind.h>
#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MEMORY MANAGEMENT - Called from worker.js
// ============================================================================

// Allocate memory in WASM heap
EMSCRIPTEN_KEEPALIVE void* ollama_alloc(size_t size);

// Free memory from WASM heap
EMSCRIPTEN_KEEPALIVE void ollama_free(void* ptr);

// Get heap pointer for direct access
EMSCRIPTEN_KEEPALIVE uint8_t* ollama_get_heap(void);

// ============================================================================
// MODEL LOADING - Called from worker.js
// ============================================================================

// Load GGUF model from memory buffer
EMSCRIPTEN_KEEPALIVE int ollama_load_model(const void* buffer, size_t buffer_size);

// Get model status
EMSCRIPTEN_KEEPALIVE int ollama_is_model_loaded(void);

// Get model metadata
EMSCRIPTEN_KEEPALIVE const char* ollama_get_model_info(void);

// ============================================================================
// KV CACHE MANAGEMENT - Called from worker.js
// ============================================================================

// Allocate KV cache for sequence length
EMSCRIPTEN_KEEPALIVE void* ollama_alloc_kv_cache(int max_sequence_len);

// Free KV cache
EMSCRIPTEN_KEEPALIVE void ollama_free_kv_cache(void* cache_ptr);

// Clear KV cache (reset for new sequence)
EMSCRIPTEN_KEEPALIVE void ollama_clear_kv_cache(void* cache_ptr);

// ============================================================================
// INFERENCE - Called from worker.js
// ============================================================================

// Evaluate a single token (prefill step)
EMSCRIPTEN_KEEPALIVE void ollama_eval_token(int token_id, void* kv_cache_ptr);

// Forward pass, return logits pointer
EMSCRIPTEN_KEEPALIVE float* ollama_forward(int token_id, void* kv_cache_ptr);

// Get logits for current position
EMSCRIPTEN_KEEPALIVE float* ollama_get_logits(void);

// Get logits size (vocab_size)
EMSCRIPTEN_KEEPALIVE int ollama_get_logits_size(void);

// ============================================================================
// SAMPLING - Called from worker.js
// ============================================================================

// Apply temperature scaling to logits
EMSCRIPTEN_KEEPALIVE void ollama_apply_temperature(float* logits, int size, float temperature);

// Apply repetition penalty
EMSCRIPTEN_KEEPALIVE void ollama_apply_repetition_penalty(float* logits, int size, int* last_tokens, int num_tokens, float penalty);

// Top-p (nucleus) sampling
EMSCRIPTEN_KEEPALIVE int ollama_sample_top_p(float* logits, int size, float top_p);

// Top-k sampling
EMSCRIPTEN_KEEPALIVE int ollama_sample_top_k(float* logits, int size, int k);

// Greedy sampling (argmax)
EMSCRIPTEN_KEEPALIVE int ollama_sample_greedy(float* logits, int size);

// ============================================================================
// TOKENIZATION - Helper functions
// ============================================================================

// Tokenize a string
EMSCRIPTEN_KEEPALIVE int* ollama_tokenize(const char* text, int* out_size);

// Detokenize tokens to string
EMSCRIPTEN_KEEPALIVE char* ollama_detokenize(int* tokens, int num_tokens);

// Free token buffer
EMSCRIPTEN_KEEPALIVE void ollama_free_tokens(int* tokens);

// Free string buffer
EMSCRIPTEN_KEEPALIVE void ollama_free_string(char* str);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Get timestamp in milliseconds
EMSCRIPTEN_KEEPALIVE double ollama_get_timestamp(void);

// Log message from WASM to console
EMSCRIPTEN_KEEPALIVE void ollama_console_log(const char* message, int length);

// Get thread count
EMSCRIPTEN_KEEPALIVE int ollama_get_thread_count(void);

#ifdef __cplusplus
}
#endif

// ============================================================================
// EMBIND BINDINGS - For direct JavaScript function calls
// ============================================================================

#ifdef __cplusplus
#include <emscripten/bind.h>

using namespace emscripten;

EMSCRIPTEN_BINDINGS(ollama_module) {
    function("alloc", &ollama_alloc, allow_raw_pointers());
    function("free", &ollama_free, allow_raw_pointers());
    function("load_model", &ollama_load_model);
    function("alloc_kv_cache", &ollama_alloc_kv_cache, allow_raw_pointers());
    function("free_kv_cache", &ollama_free_kv_cache, allow_raw_pointers());
    function("eval_token", &ollama_eval_token);
    function("forward", &ollama_forward, allow_raw_pointers());
    function("get_logits", &ollama_get_logits, allow_raw_pointers());
    function("apply_temperature", &ollama_apply_temperature);
    function("apply_repetition_penalty", &ollama_apply_repetition_penalty);
    function("sample_top_p", &ollama_sample_top_p);
    function("sample_greedy", &ollama_sample_greedy);
    function("tokenize", &ollama_tokenize, allow_raw_pointers());
    function("detokenize", &ollama_detokenize, allow_raw_pointers());
    function("get_timestamp", &ollama_get_timestamp);
    function("console_log", &ollama_console_log);
    function("get_thread_count", &ollama_get_thread_count);
}

#endif // __cplusplus

#endif // OLLAMA_BRIDGE_H
