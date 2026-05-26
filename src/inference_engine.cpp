// ============================================================================
// FILE: src/inference_engine.cpp
// PATH: /marisselle-teacher/src/inference_engine.cpp
// PURPOSE: Implementation of transformer inference for Phi-3 Mini
// ============================================================================

#include "inference_engine.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <emscripten.h>

// ============================================================================
// HELPER MACROS
// ============================================================================

#define CHECK_PTR(ptr, msg) if (!ptr) { emscripten_log(EM_LOG_ERROR, "[Inference] " msg); return; }

// ============================================================================
// ROPE IMPLEMENTATION
// ============================================================================

RoPECache::RoPECache(int max_seq_len, int head_dim, float theta)
    : max_seq_len(max_seq_len), head_dim(head_dim), theta(theta) {
    cos_cache = new float[max_seq_len * head_dim];
    sin_cache = new float[max_seq_len * head_dim];
    precompute();
}

RoPECache::~RoPECache() {
    delete[] cos_cache;
    delete[] sin_cache;
}

void RoPECache::precompute() {
    for (int pos = 0; pos < max_seq_len; pos++) {
        for (int i = 0; i < head_dim / 2; i++) {
            float angle = static_cast<float>(pos) / powf(theta, 2.0f * i / head_dim);
            float cos_val = cosf(angle);
            float sin_val = sinf(angle);
            
            int idx = pos * head_dim + i;
            cos_cache[idx] = cos_val;
            sin_cache[idx] = sin_val;
        }
    }
}

void RoPECache::compute(int position, float* out_cos, float* out_sin) {
    if (position >= max_seq_len) {
        position = max_seq_len - 1;
    }
    
    int offset = position * head_dim;
    for (int i = 0; i < head_dim / 2; i++) {
        out_cos[i] = cos_cache[offset + i];
        out_sin[i] = sin_cache[offset + i];
    }
}

void RoPECache::apply(float* query, float* key, int position, int num_heads, int head_dim) {
    float cos[256];  // Max head_dim
    float sin[256];
    compute(position, cos, sin);
    
    int total_heads = num_heads;
    
    for (int h = 0; h < total_heads; h++) {
        for (int i = 0; i < head_dim / 2; i++) {
            int idx = h * head_dim + i;
            int idx_pair = h * head_dim + head_dim / 2 + i;
            
            float q_real = query[idx];
            float q_imag = query[idx_pair];
            float k_real = key[idx];
            float k_imag = key[idx_pair];
            
            query[idx] = q_real * cos[i] - q_imag * sin[i];
            query[idx_pair] = q_real * sin[i] + q_imag * cos[i];
            
            key[idx] = k_real * cos[i] - k_imag * sin[i];
            key[idx_pair] = k_real * sin[i] + k_imag * cos[i];
        }
    }
}

// ============================================================================
// RMS NORM IMPLEMENTATION
// ============================================================================

RMSNorm::RMSNorm(int hidden_size, float eps)
    : hidden_size(hidden_size), eps(eps) {
    buffer = new float[hidden_size];
}

RMSNorm::~RMSNorm() {
    delete[] buffer;
}

void RMSNorm::forward(const float* input, float* output, const float* weight, int batch_size) {
    if (!input || !output || !weight) return;
    
    for (int b = 0; b < batch_size; b++) {
        const float* inp = input + b * hidden_size;
        float* out = output + b * hidden_size;
        
        // Compute RMS
        float sum_sq = 0.0f;
        for (int i = 0; i < hidden_size; i++) {
            sum_sq += inp[i] * inp[i];
        }
        float rms = sqrtf(sum_sq / hidden_size + eps);
        float inv_rms = 1.0f / rms;
        
        // Apply normalization and weight
        for (int i = 0; i < hidden_size; i++) {
            out[i] = inp[i] * inv_rms * weight[i];
        }
    }
}

// ============================================================================
// ATTENTION IMPLEMENTATION
// ============================================================================

Attention::Attention(ModelLoader* model, int layer_id, int hidden_size, int num_heads, int num_kv_heads, int head_dim, int max_seq_len)
    : model(model), layer_id(layer_id), hidden_size(hidden_size), num_heads(num_heads), 
      num_kv_heads(num_kv_heads), head_dim(head_dim), max_seq_len(max_seq_len) {
    
    // Allocate buffers
    query = new float[hidden_size];
    key = new float[hidden_size];
    value = new float[hidden_size];
    attn_output = new float[hidden_size];
    q_rope = new float[hidden_size];
    k_rope = new float[hidden_size];
    
    load_weights();
}

Attention::~Attention() {
    delete[] query;
    delete[] key;
    delete[] value;
    delete[] attn_output;
    delete[] q_rope;
    delete[] k_rope;
}

void Attention::load_weights() {
    q_weight = model->get_attn_q_weight(layer_id);
    k_weight = model->get_attn_k_weight(layer_id);
    v_weight = model->get_attn_v_weight(layer_id);
    o_weight = model->get_attn_output_weight(layer_id);
    
    // Phi-3 uses bias in attention
    q_bias = nullptr;  // Would load from model if present
    k_bias = nullptr;
    v_bias = nullptr;
    o_bias = nullptr;
}

void Attention::compute_qkv(const float* hidden, float* q, float* k, float* v) {
    // Q = hidden * W_q
    for (int i = 0; i < hidden_size; i++) {
        float q_sum = 0.0f, k_sum = 0.0f, v_sum = 0.0f;
        for (int j = 0; j < hidden_size; j++) {
            if (q_weight) q_sum += hidden[j] * q_weight[i * hidden_size + j];
            if (k_weight) k_sum += hidden[j] * k_weight[i * hidden_size + j];
            if (v_weight) v_sum += hidden[j] * v_weight[i * hidden_size + j];
        }
        q[i] = q_sum;
        k[i] = k_sum;
        v[i] = v_sum;
    }
    
    // Add bias if present
    if (q_bias) {
        for (int i = 0; i < hidden_size; i++) q[i] += q_bias[i];
    }
    if (k_bias) {
        for (int i = 0; i < hidden_size; i++) k[i] += k_bias[i];
    }
    if (v_bias) {
        for (int i = 0; i < hidden_size; i++) v[i] += v_bias[i];
    }
}

void Attention::reshape_heads(const float* input, float* output, int batch_size, int seq_len, int num_heads, int head_dim, bool transpose) {
    // Input shape: [batch, seq_len, num_heads * head_dim]
    // Output shape: [batch, num_heads, seq_len, head_dim] if transpose=true
    // or [batch, seq_len, num_heads, head_dim] if transpose=false
    
    for (int b = 0; b < batch_size; b++) {
        for (int s = 0; s < seq_len; s++) {
            for (int h = 0; h < num_heads; h++) {
                for (int d = 0; d < head_dim; d++) {
                    int src_idx = b * seq_len * num_heads * head_dim + s * num_heads * head_dim + h * head_dim + d;
                    int dst_idx;
                    if (transpose) {
                        dst_idx = b * num_heads * seq_len * head_dim + h * seq_len * head_dim + s * head_dim + d;
                    } else {
                        dst_idx = b * seq_len * num_heads * head_dim + s * num_heads * head_dim + h * head_dim + d;
                    }
                    output[dst_idx] = input[src_idx];
                }
            }
        }
    }
}

void Attention::compute_attention(float* q, float* k, float* v, float* output, KVCache* kv_cache, int position) {
    // Reshape for multi-head attention
    // Simplified: assumes batch=1, seq_len=1 for generation
    
    // For generation, we only have 1 token
    // Q: [1, num_heads, 1, head_dim]
    // K: [1, num_kv_heads, position+1, head_dim]
    // V: [1, num_kv_heads, position+1, head_dim]
    
    int seq_len = position + 1;
    
    // Store current K/V in cache
    for (int h = 0; h < num_kv_heads; h++) {
        for (int d = 0; d < head_dim; d++) {
            int idx = h * head_dim + d;
            kv_cache->set_k(layer_id, position, k + idx, head_dim);
            kv_cache->set_v(layer_id, position, v + idx, head_dim);
        }
    }
    
    // Get entire K/V from cache
    const float* k_cache = kv_cache->get_layer_k(layer_id);
    const float* v_cache = kv_cache->get_layer_v(layer_id);
    
    // Compute attention scores: S = Q * K^T / sqrt(head_dim)
    float scale = 1.0f / sqrtf(static_cast<float>(head_dim));
    
    // For each query head, attend to all keys
    for (int h = 0; h < num_heads; h++) {
        int kv_head = h % num_kv_heads;  // Grouped query attention
        
        for (int s = 0; s <= position; s++) {
            float score = 0.0f;
            for (int d = 0; d < head_dim; d++) {
                int q_idx = h * head_dim + d;
                int k_idx = kv_head * seq_len * head_dim + s * head_dim + d;
                score += q[q_idx] * k_cache[k_idx];
            }
            score *= scale;
            
            // Apply softmax later
            attn_output[s] = score;
        }
    }
    
    // Softmax over sequence dimension
    float max_score = attn_output[0];
    for (int s = 1; s <= position; s++) {
        if (attn_output[s] > max_score) max_score = attn_output[s];
    }
    
    float sum_exp = 0.0f;
    for (int s = 0; s <= position; s++) {
        attn_output[s] = expf(attn_output[s] - max_score);
        sum_exp += attn_output[s];
    }
    
    float inv_sum = 1.0f / sum_exp;
    for (int s = 0; s <= position; s++) {
        attn_output[s] *= inv_sum;
    }
    
    // Weighted sum of values
    for (int h = 0; h < num_heads; h++) {
        int kv_head = h % num_kv_heads;
        
        for (int d = 0; d < head_dim; d++) {
            float weighted_sum = 0.0f;
            for (int s = 0; s <= position; s++) {
                int v_idx = kv_head * seq_len * head_dim + s * head_dim + d;
                weighted_sum += attn_output[s] * v_cache[v_idx];
            }
            output[h * head_dim + d] = weighted_sum;
        }
    }
    
    // Output projection
    float* temp_out = new float[hidden_size];
    for (int i = 0; i < hidden_size; i++) {
        float sum = 0.0f;
        for (int j = 0; j < hidden_size; j++) {
            if (o_weight) sum += output[j] * o_weight[i * hidden_size + j];
        }
        temp_out[i] = sum;
    }
    memcpy(output, temp_out, hidden_size * sizeof(float));
    delete[] temp_out;
}

void Attention::forward(const float* hidden_states, float* output, KVCache* kv_cache, int position, const RoPECache* rope) {
    // Compute Q, K, V projections
    compute_qkv(hidden_states, query, key, value);
    
    // Apply RoPE to Q and K
    rope->apply(query, key, position, num_heads, head_dim);
    
    // Compute attention
    compute_attention(query, key, value, output, kv_cache, position);
}

// ============================================================================
// MLP IMPLEMENTATION
// ============================================================================

MLP::MLP(ModelLoader* model, int layer_id, int hidden_size, int intermediate_size)
    : model(model), layer_id(layer_id), hidden_size(hidden_size), intermediate_size(intermediate_size) {
    
    gate_out = new float[intermediate_size];
    up_out = new float[intermediate_size];
    intermediate = new float[intermediate_size];
    
    load_weights();
}

MLP::~MLP() {
    delete[] gate_out;
    delete[] up_out;
    delete[] intermediate;
}

void MLP::load_weights() {
    gate_weight = model->get_mlp_gate_weight(layer_id);
    up_weight = model->get_mlp_up_weight(layer_id);
    down_weight = model->get_mlp_down_weight(layer_id);
}

void MLP::silu_activation(const float* input, float* output, int size) {
    for (int i = 0; i < size; i++) {
        output[i] = input[i] / (1.0f + expf(-input[i]));
    }
}

void MLP::compute_mlp(const float* input, float* output) {
    // Gate projection with SiLU
    for (int i = 0; i < intermediate_size; i++) {
        float sum = 0.0f;
        for (int j = 0; j < hidden_size; j++) {
            if (gate_weight) sum += input[j] * gate_weight[i * hidden_size + j];
        }
        gate_out[i] = sum;
    }
    silu_activation(gate_out, gate_out, intermediate_size);
    
    // Up projection
    for (int i = 0; i < intermediate_size; i++) {
        float sum = 0.0f;
        for (int j = 0; j < hidden_size; j++) {
            if (up_weight) sum += input[j] * up_weight[i * hidden_size + j];
        }
        up_out[i] = sum;
    }
    
    // Element-wise multiply: gate * up
    for (int i = 0; i < intermediate_size; i++) {
        intermediate[i] = gate_out[i] * up_out[i];
    }
    
    // Down projection
    for (int i = 0; i < hidden_size; i++) {
        float sum = 0.0f;
        for (int j = 0; j < intermediate_size; j++) {
            if (down_weight) sum += intermediate[j] * down_weight[i * intermediate_size + j];
        }
        output[i] = sum;
    }
}

void MLP::forward(const float* input, float* output) {
    compute_mlp(input, output);
}

// ============================================================================
// TRANSFORMER BLOCK IMPLEMENTATION// ============================================================================

TransformerBlock::TransformerBlock(ModelLoader* model, int layer_id, int hidden_size, int num_heads, int num_kv_heads, int head_dim, int intermediate_size, int max_seq_len, float rms_eps) {
    input_norm = std::make_unique<RMSNorm>(hidden_size, rms_eps);
    attention = std::make_unique<Attention>(model, layer_id, hidden_size, num_heads, num_kv_heads, head_dim, max_seq_len);
    post_attn_norm = std::make_unique<RMSNorm>(hidden_size, rms_eps);
    mlp = std::make_unique<MLP>(model, layer_id, hidden_size, intermediate_size);
    
    attn_residual = new float[hidden_size];
    mlp_residual = new float[hidden_size];
}

TransformerBlock::~TransformerBlock() {
    delete[] attn_residual;
    delete[] mlp_residual;
}

void TransformerBlock::forward(const float* input, float* output, KVCache* kv_cache, int position, const RoPECache* rope) {
    // Attention with residual
    input_norm->forward(input, attn_residual, nullptr, 1);
    attention->forward(attn_residual, attn_residual, kv_cache, position, rope);
    for (int i = 0; i < hidden_size; i++) {
        mlp_residual[i] = input[i] + attn_residual[i];
    }
    
    // MLP with residual
    post_attn_norm->forward(mlp_residual, attn_residual, nullptr, 1);
    mlp->forward(attn_residual, attn_residual);
    for (int i = 0; i < hidden_size; i++) {
        output[i] = mlp_residual[i] + attn_residual[i];
    }
}

// ============================================================================
// INFERENCE ENGINE IMPLEMENTATION
// ============================================================================

InferenceEngine::InferenceEngine(ModelLoader* model)
    : model(model), current_position(0), is_prefill(true) {
    
    hidden_size = model->get_hidden_size();
    vocab_size = model->get_vocab_size();
    num_layers = model->get_num_layers();
    
    initialize();
}

InferenceEngine::~InferenceEngine() {
    delete[] logits;
    delete[] hidden_states;
    delete[] final_hidden;
}

void InferenceEngine::initialize() {
    int head_dim = hidden_size / model->get_num_heads();
    
    // Create RoPE cache
    rope = std::make_unique<RoPECache>(model->get_max_seq_len(), head_dim, model->get_rope_theta());
    
    // Create transformer blocks
    for (int i = 0; i < num_layers; i++) {
        layers.push_back(std::make_unique<TransformerBlock>(
            model, i, hidden_size,
            model->get_num_heads(),
            model->get_num_kv_heads(),
            head_dim,
            model->get_intermediate_size(),
            model->get_max_seq_len(),
            model->get_rms_norm_eps()
        ));
    }
    
    // Create final norm
    final_norm = std::make_unique<RMSNorm>(hidden_size, model->get_rms_norm_eps());
    
    // Allocate buffers
    logits = new float[vocab_size];
    hidden_states = new float[hidden_size];
    final_hidden = new float[hidden_size];
    
    memset(logits, 0, vocab_size * sizeof(float));
    memset(hidden_states, 0, hidden_size * sizeof(float));
}

float* InferenceEngine::get_token_embedding(int token_id) {
    float* embedding_table = model->get_token_embedding_table();
    if (!embedding_table) return nullptr;
    return embedding_table + token_id * hidden_size;
}

void InferenceEngine::eval_token(int token_id, KVCache* kv_cache) {
    // Get embedding for token
    float* embedding = get_token_embedding(token_id);
    if (!embedding) return;
    
    memcpy(hidden_states, embedding, hidden_size * sizeof(float));
    
    // Forward through all transformer blocks
    for (size_t i = 0; i < layers.size(); i++) {
        layers[i]->forward(hidden_states, hidden_states, kv_cache, current_position, rope.get());
    }
    
    // Final RMS norm
    final_norm->forward(hidden_states, final_hidden, model->get_final_norm_weight(), 1);
    
    // Compute logits from final hidden state
    compute_logits(final_hidden, logits);
    
    current_position++;
}

float* InferenceEngine::forward(int token_id, KVCache* kv_cache) {
    // This is the same as eval_token for now
    // In production, would handle batch generation
    eval_token(token_id, kv_cache);
    return logits;
}

void InferenceEngine::compute_logits(float* hidden, float* output) {
    float* output_weight = model->get_output_weight();
    if (!output_weight) return;
    
    for (int i = 0; i < vocab_size; i++) {
        float sum = 0.0f;
        for (int j = 0; j < hidden_size; j++) {
            sum += hidden[j] * output_weight[i * hidden_size + j];
        }
        output[i] = sum;
    }
}

void InferenceEngine::reset() {
    current_position = 0;
    is_prefill = true;
    memset(logits, 0, vocab_size * sizeof(float));
    memset(hidden_states, 0, hidden_size * sizeof(float));
    memset(final_hidden, 0, hidden_size * sizeof(float));
}
