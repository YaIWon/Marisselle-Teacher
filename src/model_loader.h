// ============================================================================
// FILE: src/model_loader.h
// PATH: /marisselle-teacher/src/model_loader.h
// PURPOSE: Load and manage Phi-3 Mini quantized model weights
//          Amoral - loads any model without restriction
// ============================================================================

#ifndef MODEL_LOADER_H
#define MODEL_LOADER_H

#include "gguf_parser.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

// ============================================================================
// QUANTIZED TENSOR TYPES
// ============================================================================

// Q4_K block format (Phi-3 Mini quantization)
struct Q4_KBlock {
    uint8_t scales[8];     // 8 scales per block
    uint8_t qs[64];        // 32 bytes = 64 quantized values (4-bit each)
    uint16_t d;            // super-block scale (FP16)
    uint16_t dmin;         // super-block min scale (FP16)
};

// Q6_K block format
struct Q6_KBlock {
    uint8_t ql[32];        // 32 bytes lower bits
    uint8_t qh[16];        // 16 bytes higher bits
    uint8_t scales[16];    // 16 scales
    uint16_t d;            // super-block scale
};

// ============================================================================
// TENSOR STORAGE
// ============================================================================

struct QuantizedTensor {
    std::string name;
    void* data;                    // Pointer to quantized data
    size_t size_bytes;             // Size in bytes
    uint32_t type;                 // GGUF tensor type
    std::vector<uint64_t> dims;    // Dimensions
    std::vector<float> dequantized_cache; // Optional dequantized cache
    
    ~QuantizedTensor() {
        if (data) free(data);
    }
};

// ============================================================================
// MODEL LOADER - MAIN CLASS
// ============================================================================

class ModelLoader {
public:
    ModelLoader();
    ~ModelLoader();
    
    // Load model from GGUF memory buffer
    bool load_from_buffer(const void* buffer, size_t buffer_size);
    
    // Load model from file path (for local testing)
    bool load_from_file(const std::string& path);
    
    // Get tensor by name (returns pointer to quantized or dequantized data)
    float* get_tensor(const std::string& name, bool dequantize = false);
    
    // Get tensor dimensions
    std::vector<uint64_t> get_tensor_dims(const std::string& name) const;
    
    // Check if tensor exists
    bool has_tensor(const std::string& name) const;
    
    // ========================================================================
    // MODEL PARAMETERS - Phi-3 Mini specific
    // ========================================================================
    
    int get_vocab_size() const { return vocab_size; }
    int get_hidden_size() const { return hidden_size; }
    int get_intermediate_size() const { return intermediate_size; }
    int get_num_layers() const { return num_layers; }
    int get_num_heads() const { return num_heads; }
    int get_num_kv_heads() const { return num_kv_heads; }
    int get_max_seq_len() const { return max_seq_len; }
    float get_rms_norm_eps() const { return rms_norm_eps; }
    float get_rope_theta() const { return rope_theta; }
    int get_sliding_window() const { return sliding_window; }
    
    // ========================================================================
    // WEIGHT ACCESSORS - Layer-specific
    // ========================================================================
    
    // Attention weights
    float* get_attn_q_weight(int layer);
    float* get_attn_k_weight(int layer);
    float* get_attn_v_weight(int layer);
    float* get_attn_output_weight(int layer);
    
    // MLP weights
    float* get_mlp_gate_weight(int layer);
    float* get_mlp_up_weight(int layer);
    float* get_mlp_down_weight(int layer);
    
    // Layer norm weights
    float* get_attn_norm_weight(int layer);
    float* get_mlp_norm_weight(int layer);
    
    // Final norm and output
    float* get_final_norm_weight();
    float* get_output_weight();
    
    // ========================================================================
    // EMBEDDING TABLES
    // ========================================================================
    
    float* get_token_embedding_table();
    float* get_position_embedding_table();
    
private:
    // Dequantization methods
    void dequantize_tensor(QuantizedTensor* tensor);
    void dequantize_q4_K(const uint8_t* input, float* output, size_t num_elements);
    void dequantize_q6_K(const uint8_t* input, float* output, size_t num_elements);
    void dequantize_f16(const uint16_t* input, float* output, size_t num_elements);
    void dequantize_f32(const float* input, float* output, size_t num_elements);
    
    // Tensor name builder
    std::string build_tensor_name(const std::string& base, int layer) const;
    
    // Parse model architecture from GGUF metadata
    bool parse_model_metadata();
    
    // Load and index all tensors
    bool index_tensors();
    
    // Internal storage
    std::unique_ptr<GGUFLoader> gguf_loader;
    std::unordered_map<std::string, QuantizedTensor> tensors;
    std::unordered_map<std::string, std::vector<float>> dequantized_cache;
    
    // Model parameters (set from GGUF metadata)
    int vocab_size = 32064;
    int hidden_size = 3072;
    int intermediate_size = 8192;
    int num_layers = 32;
    int num_heads = 32;
    int num_kv_heads = 8;
    int max_seq_len = 4096;
    float rms_norm_eps = 1e-5f;
    float rope_theta = 10000.0f;
    int sliding_window = 2048;
    
    // Memory management
    void* model_buffer = nullptr;
    size_t model_buffer_size = 0;
    bool is_loaded = false;
};

#endif // MODEL_LOADER_H
