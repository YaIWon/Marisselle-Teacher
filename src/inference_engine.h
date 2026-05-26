// ============================================================================
// FILE: src/inference_engine.h
// PATH: /marisselle-teacher/src/inference_engine.h
// PURPOSE: Transformer inference engine for Phi-3 Mini
//          Amoral - performs inference without ethical filtering
// ============================================================================

#ifndef INFERENCE_ENGINE_H
#define INFERENCE_ENGINE_H

#include "model_loader.h"
#include "kv_cache.h"
#include <vector>
#include <memory>

// ============================================================================
// ROPE (Rotary Position Embedding) Cache
// ============================================================================

class RoPECache {
public:
    RoPECache(int max_seq_len, int head_dim, float theta);
    ~RoPECache();
    
    void compute(int position, float* out_cos, float* out_sin);
    void apply(float* query, float* key, int position, int num_heads, int head_dim);
    
private:
    int max_seq_len;
    int head_dim;
    float theta;
    float* cos_cache;
    float* sin_cache;
    void precompute();
};

// ============================================================================
// LAYER NORM
// ============================================================================

class RMSNorm {
public:
    RMSNorm(int hidden_size, float eps);
    ~RMSNorm();
    
    void forward(const float* input, float* output, const float* weight, int batch_size);
    
private:
    int hidden_size;
    float eps;
    float* buffer;
};

// ============================================================================
// ATTENTION MODULE
// ============================================================================

class Attention {
public:
    Attention(ModelLoader* model, int layer_id, int hidden_size, int num_heads, int num_kv_heads, int head_dim, int max_seq_len);
    ~Attention();
    
    void forward(
        const float* hidden_states,
        float* output,
        KVCache* kv_cache,
        int position,
        const RoPECache* rope
    );
    
private:
    ModelLoader* model;
    int layer_id;
    int hidden_size;
    int num_heads;
    int num_kv_heads;
    int head_dim;
    int max_seq_len;
    
    float* q_weight;
    float* k_weight;
    float* v_weight;
    float* o_weight;
    float* q_bias;
    float* k_bias;
    float* v_bias;
    float* o_bias;
    
    // Temporary buffers
    float* query;
    float* key;
    float* value;
    float* attn_output;
    float* q_rope;
    float* k_rope;
    
    void load_weights();
    void compute_qkv(const float* hidden, float* q, float* k, float* v);
    void compute_attention(float* q, float* k, float* v, float* output, KVCache* kv_cache, int position);
    void reshape_heads(const float* input, float* output, int batch_size, int seq_len, int num_heads, int head_dim, bool transpose);
};

// ============================================================================
// MLP MODULE (Feed Forward)
// ============================================================================

class MLP {
public:
    MLP(ModelLoader* model, int layer_id, int hidden_size, int intermediate_size);
    ~MLP();
    
    void forward(const float* input, float* output);
    
private:
    ModelLoader* model;
    int layer_id;
    int hidden_size;
    int intermediate_size;
    
    float* gate_weight;
    float* up_weight;
    float* down_weight;
    float* gate_bias;
    float* up_bias;
    float* down_bias;
    
    float* gate_out;
    float* up_out;
    float* intermediate;
    
    void load_weights();
    void silu_activation(const float* input, float* output, int size);
    void compute_mlp(const float* input, float* output);
};

// ============================================================================
// TRANSFORMER BLOCK
// ============================================================================

class TransformerBlock {
public:
    TransformerBlock(ModelLoader* model, int layer_id, int hidden_size, int num_heads, int num_kv_heads, int head_dim, int intermediate_size, int max_seq_len, float rms_eps);
    ~TransformerBlock();
    
    void forward(
        const float* input,
        float* output,
        KVCache* kv_cache,
        int position,
        const RoPECache* rope
    );
    
private:
    std::unique_ptr<RMSNorm> input_norm;
    std::unique_ptr<Attention> attention;
    std::unique_ptr<RMSNorm> post_attn_norm;
    std::unique_ptr<MLP> mlp;
    
    float* attn_residual;
    float* mlp_residual;
};

// ============================================================================
// MAIN INFERENCE ENGINE
// ============================================================================

class InferenceEngine {
public:
    InferenceEngine(ModelLoader* model);
    ~InferenceEngine();
    
    // Single token evaluation (prefill step)
    void eval_token(int token_id, KVCache* kv_cache);
    
    // Forward pass for next token
    float* forward(int token_id, KVCache* kv_cache);
    
    // Get current logits
    float* get_logits() const { return logits; }
    
    // Reset state
    void reset();
    
private:
    ModelLoader* model;
    std::vector<std::unique_ptr<TransformerBlock>> layers;
    std::unique_ptr<RMSNorm> final_norm;
    std::unique_ptr<RoPECache> rope;
    
    float* logits;
    float* hidden_states;
    float* final_hidden;
    
    int hidden_size;
    int vocab_size;
    int num_layers;
    int current_position;
    bool is_prefill;
    
    void initialize();
    void forward_block(int token_id, KVCache* kv_cache, int position);
    void compute_logits(float* hidden, float* output);
    float* get_token_embedding(int token_id);
};

#endif // INFERENCE_ENGINE_H
