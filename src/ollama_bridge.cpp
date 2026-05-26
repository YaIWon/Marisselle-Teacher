// ============================================================================
// FILE: src/ollama_bridge.cpp
// PATH: /marisselle-teacher/src/ollama_bridge.cpp
// PURPOSE: Implementation of WASM exports for Marisselle Teacher
// ============================================================================

#include "ollama_bridge.h"
#include "model_loader.h"
#include "inference_engine.h"
#include "kv_cache.h"
#include "tokenizer.h"
#include "sampler.h"
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>

// ============================================================================
// GLOBAL STATE
// ============================================================================

static ModelLoader* g_model_loader = nullptr;
static InferenceEngine* g_inference_engine = nullptr;
static Tokenizer* g_tokenizer = nullptr;
static std::mutex g_mutex;
static bool g_model_loaded = false;
static float* g_current_logits = nullptr;
static int g_logits_size = 0;

// ============================================================================
// MEMORY MANAGEMENT
// ============================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE void* ollama_alloc(size_t size) {
    void* ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

EMSCRIPTEN_KEEPALIVE void ollama_free(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}

EMSCRIPTEN_KEEPALIVE uint8_t* ollama_get_heap(void) {
    // Return base pointer of heap for direct access
    static uint8_t* heap_base = nullptr;
    if (!heap_base) {
        heap_base = static_cast<uint8_t*>(malloc(1));
        free(heap_base);
    }
    return heap_base;
}

// ============================================================================
// MODEL LOADING
// ============================================================================

EMSCRIPTEN_KEEPALIVE int ollama_load_model(const void* buffer, size_t buffer_size) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    try {
        if (g_model_loader) {
            delete g_model_loader;
            g_model_loader = nullptr;
        }
        
        if (g_inference_engine) {
            delete g_inference_engine;
            g_inference_engine = nullptr;
        }
        
        g_model_loader = new ModelLoader();
        bool load_success = g_model_loader->load_from_buffer(buffer, buffer_size);
        
        if (!load_success) {
            return -1;
        }
        
        g_inference_engine = new InferenceEngine(g_model_loader);
        g_tokenizer = new Tokenizer();
        g_tokenizer->load_from_model(g_model_loader);
        
        g_model_loaded = true;
        g_logits_size = g_model_loader->get_vocab_size();
        
        return 0;
    } catch (...) {
        return -1;
    }
}

EMSCRIPTEN_KEEPALIVE int ollama_is_model_loaded(void) {
    return g_model_loaded ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* ollama_get_model_info(void) {
    static char info[512];
    if (g_model_loaded && g_model_loader) {
        snprintf(info, sizeof(info), 
            "{\"vocab_size\":%d,\"hidden_size\":%d,\"num_layers\":%d,\"max_seq_len\":%d}",
            g_model_loader->get_vocab_size(),
            g_model_loader->get_hidden_size(),
            g_model_loader->get_num_layers(),
            g_model_loader->get_max_seq_len()
        );
        return info;
    }
    return "{\"error\":\"model not loaded\"}";
}

// ============================================================================
// KV CACHE MANAGEMENT
// ============================================================================

EMSCRIPTEN_KEEPALIVE void* ollama_alloc_kv_cache(int max_sequence_len) {
    if (!g_model_loaded || !g_inference_engine) {
        return nullptr;
    }
    
    KVCache* cache = new KVCache(
        g_model_loader->get_num_layers(),
        g_model_loader->get_num_kv_heads(),
        g_model_loader->get_hidden_size() / g_model_loader->get_num_heads(),
        max_sequence_len
    );
    
    return static_cast<void*>(cache);
}

EMSCRIPTEN_KEEPALIVE void ollama_free_kv_cache(void* cache_ptr) {
    if (cache_ptr) {
        KVCache* cache = static_cast<KVCache*>(cache_ptr);
        delete cache;
    }
}

EMSCRIPTEN_KEEPALIVE void ollama_clear_kv_cache(void* cache_ptr) {
    if (cache_ptr) {
        KVCache* cache = static_cast<KVCache*>(cache_ptr);
        cache->clear();
    }
}

// ============================================================================
// INFERENCE
// ============================================================================

EMSCRIPTEN_KEEPALIVE void ollama_eval_token(int token_id, void* kv_cache_ptr) {
    if (!g_model_loaded || !g_inference_engine) {
        return;
    }
    
    KVCache* cache = static_cast<KVCache*>(kv_cache_ptr);
    g_inference_engine->eval_token(token_id, cache);
}

EMSCRIPTEN_KEEPALIVE float* ollama_forward(int token_id, void* kv_cache_ptr) {
    if (!g_model_loaded || !g_inference_engine) {
        return nullptr;
    }
    
    KVCache* cache = static_cast<KVCache*>(kv_cache_ptr);
    g_current_logits = g_inference_engine->forward(token_id, cache);
    
    return g_current_logits;
}

EMSCRIPTEN_KEEPALIVE float* ollama_get_logits(void) {
    return g_current_logits;
}

EMSCRIPTEN_KEEPALIVE int ollama_get_logits_size(void) {
    return g_logits_size;
}

// ============================================================================
// SAMPLING
// ============================================================================

EMSCRIPTEN_KEEPALIVE void ollama_apply_temperature(float* logits, int size, float temperature) {
    if (!logits || size <= 0 || temperature <= 0.0f) {
        return;
    }
    
    Sampler::apply_temperature(logits, size, temperature);
}

EMSCRIPTEN_KEEPALIVE void ollama_apply_repetition_penalty(float* logits, int size, int* last_tokens, int num_tokens, float penalty) {
    if (!logits || !last_tokens || num_tokens <= 0 || penalty <= 1.0f) {
        return;
    }
    
    Sampler::apply_repetition_penalty(logits, size, last_tokens, num_tokens, penalty);
}

EMSCRIPTEN_KEEPALIVE int ollama_sample_top_p(float* logits, int size, float top_p) {
    if (!logits || size <= 0) {
        return -1;
    }
    
    return Sampler::sample_top_p(logits, size, top_p);
}

EMSCRIPTEN_KEEPALIVE int ollama_sample_top_k(float* logits, int size, int k) {
    if (!logits || size <= 0) {
        return -1;
    }
    
    return Sampler::sample_top_k(logits, size, k);
}

EMSCRIPTEN_KEEPALIVE int ollama_sample_greedy(float* logits, int size) {
    if (!logits || size <= 0) {
        return -1;
    }
    
    return Sampler::sample_greedy(logits, size);
}

// ============================================================================
// TOKENIZATION
// ============================================================================

EMSCRIPTEN_KEEPALIVE int* ollama_tokenize(const char* text, int* out_size) {
    if (!g_tokenizer || !text || !out_size) {
        return nullptr;
    }
    
    std::vector<int> tokens = g_tokenizer->encode(text);
    *out_size = tokens.size();
    
    int* result = static_cast<int*>(malloc(tokens.size() * sizeof(int)));
    if (result) {
        memcpy(result, tokens.data(), tokens.size() * sizeof(int));
    }
    
    return result;
}

EMSCRIPTEN_KEEPALIVE char* ollama_detokenize(int* tokens, int num_tokens) {
    if (!g_tokenizer || !tokens || num_tokens <= 0) {
        return nullptr;
    }
    
    std::vector<int> token_vec(tokens, tokens + num_tokens);
    std::string text = g_tokenizer->decode(token_vec);
    
    char* result = static_cast<char*>(malloc(text.length() + 1));
    if (result) {
        strcpy(result, text.c_str());
    }
    
    return result;
}

EMSCRIPTEN_KEEPALIVE void ollama_free_tokens(int* tokens) {
    if (tokens) {
        free(tokens);
    }
}

EMSCRIPTEN_KEEPALIVE void ollama_free_string(char* str) {
    if (str) {
        free(str);
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

EMSCRIPTEN_KEEPALIVE double ollama_get_timestamp(void) {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double, std::milli>(duration).count();
}

EMSCRIPTEN_KEEPALIVE void ollama_console_log(const char* message, int length) {
    if (message && length > 0) {
        // Log to browser console via emscripten
        emscripten_log(EM_LOG_CONSOLE, "%.*s", length, message);
    }
}

EMSCRIPTEN_KEEPALIVE int ollama_get_thread_count(void) {
    return std::thread::hardware_concurrency();
}

} // extern "C"
