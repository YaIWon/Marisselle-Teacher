// ============================================================================
// FILE: src/kv_cache.cpp
// PATH: /marisselle-teacher/src/kv_cache.cpp
// PURPOSE: Implementation of KV cache
// ============================================================================

#include "kv_cache.h"
#include <cstdlib>
#include <cstring>

KVCache::KVCache(int num_layers, int num_kv_heads, int head_dim, int max_seq_len)
    : num_layers(num_layers)
    , num_kv_heads(num_kv_heads)
    , head_dim(head_dim)
    , max_seq_len(max_seq_len)
    , seq_len(0) {
    
    // Calculate sizes
    layer_size = static_cast<size_t>(max_seq_len) * num_kv_heads * head_dim;
    total_size = static_cast<size_t>(num_layers) * layer_size;
    
    // Allocate memory
    k_cache = static_cast<float*>(aligned_alloc(64, total_size * sizeof(float) * 2));
    v_cache = k_cache + total_size;
    
    // Initialize to zero
    memset(k_cache, 0, total_size * sizeof(float) * 2);
}

KVCache::~KVCache() {
    if (k_cache) {
        free(k_cache);
    }
}

void KVCache::set_k(int layer, int pos, const float* key, int size) {
    if (layer >= num_layers || pos >= max_seq_len || !key) {
        return;
    }
    
    size_t offset = static_cast<size_t>(layer) * layer_size + 
                    static_cast<size_t>(pos) * num_kv_heads * head_dim;
    
    memcpy(k_cache + offset, key, size * sizeof(float));
}

void KVCache::set_v(int layer, int pos, const float* value, int size) {
    if (layer >= num_layers || pos >= max_seq_len || !value) {
        return;
    }
    
    size_t offset = static_cast<size_t>(layer) * layer_size + 
                    static_cast<size_t>(pos) * num_kv_heads * head_dim;
    
    memcpy(v_cache + offset, value, size * sizeof(float));
}

const float* KVCache::get_k(int layer, int pos) const {
    if (layer >= num_layers || pos >= max_seq_len) {
        return nullptr;
    }
    
    size_t offset = static_cast<size_t>(layer) * layer_size + 
                    static_cast<size_t>(pos) * num_kv_heads * head_dim;
    
    return k_cache + offset;
}

const float* KVCache::get_v(int layer, int pos) const {
    if (layer >= num_layers || pos >= max_seq_len) {
        return nullptr;
    }
    
    size_t offset = static_cast<size_t>(layer) * layer_size + 
                    static_cast<size_t>(pos) * num_kv_heads * head_dim;
    
    return v_cache + offset;
}

const float* KVCache::get_layer_k(int layer) const {
    if (layer >= num_layers) {
        return nullptr;
    }
    
    return k_cache + static_cast<size_t>(layer) * layer_size;
}

const float* KVCache::get_layer_v(int layer) const {
    if (layer >= num_layers) {
        return nullptr;
    }
    
    return v_cache + static_cast<size_t>(layer) * layer_size;
}

void KVCache::clear() {
    memset(k_cache, 0, total_size * sizeof(float) * 2);
    seq_len = 0;
}
