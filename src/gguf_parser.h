// ============================================================================
// FILE: src/gguf_parser.h
// PATH: /marisselle-teacher/src/gguf_parser.h
// PURPOSE: Parse GGUF model format (Phi-3 Mini)
// ============================================================================

#ifndef GGUF_PARSER_H
#define GGUF_PARSER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

// GGUF magic numbers
constexpr uint32_t GGUF_MAGIC = 0x46554747;  // "GGUF"
constexpr uint32_t GGUF_VERSION = 3;

// Tensor types
enum class GGUFTensorType : uint32_t {
    F32 = 0,
    F16 = 1,
    Q4_0 = 2,
    Q4_1 = 3,
    Q5_0 = 6,
    Q5_1 = 7,
    Q8_0 = 8,
    Q8_1 = 9,
    Q2_K = 10,
    Q3_K = 11,
    Q4_K = 12,
    Q5_K = 13,
    Q6_K = 14,
    Q8_K = 15,
    IQ1_S = 16,
    IQ2_S = 17,
    IQ3_S = 18,
    IQ4_S = 19
};

// Metadata value types
enum class GGUFTValueType : uint32_t {
    UINT8 = 0,
    INT8 = 1,
    UINT16 = 2,
    INT16 = 3,
    UINT32 = 4,
    INT32 = 5,
    FLOAT32 = 6,
    BOOL = 7,
    STRING = 8,
    ARRAY = 9,
};

struct GGUFTensorInfo {
    std::string name;
    GGUFTensorType type;
    std::vector<uint64_t> dimensions;
    uint64_t offset;
};

class GGUFLoader {
public:
    GGUFLoader();
    ~GGUFLoader();
    
    // Load GGUF from memory buffer
    bool load(const uint8_t* buffer, size_t size);
    
    // Get tensor data
    const uint8_t* get_tensor_data(const std::string& name);
    const uint8_t* get_tensor_data_at_offset(uint64_t offset);
    
    // Get metadata
    template<typename T>
    T get_metadata(const std::string& key, T default_value) const;
    
    std::string get_string_metadata(const std::string& key) const;
    int64_t get_int_metadata(const std::string& key, int64_t default_value) const;
    float get_float_metadata(const std::string& key, float default_value) const;
    
    // Accessors
    uint64_t get_tensor_count() const { return tensor_infos.size(); }
    const std::vector<GGUFTensorInfo>& get_tensor_infos() const { return tensor_infos; }
    const uint8_t* get_data_buffer() const { return data_buffer; }
    size_t get_data_size() const { return data_size; }
    
    // Model parameters
    int get_vocab_size() const { return vocab_size; }
    int get_hidden_size() const { return hidden_size; }
    int get_intermediate_size() const { return intermediate_size; }
    int get_num_layers() const { return num_layers; }
    int get_num_heads() const { return num_heads; }
    int get_num_kv_heads() const { return num_kv_heads; }
    int get_max_seq_len() const { return max_seq_len; }
    
private:
    bool parse_header(const uint8_t* buffer, size_t size);
    bool parse_metadata(const uint8_t* buffer, size_t size);
    bool parse_tensor_infos(const uint8_t* buffer, size_t size);
    bool parse_tensor_data(const uint8_t* buffer, size_t size);
    
    std::unordered_map<std::string, uint64_t> string_metadata;
    std::unordered_map<std::string, int64_t> int_metadata;
    std::unordered_map<std::string, float> float_metadata;
    
    std::vector<GGUFTensorInfo> tensor_infos;
    const uint8_t* data_buffer = nullptr;
    size_t data_size = 0;
    
    // Cached model parameters
    int vocab_size = 32064;
    int hidden_size = 3072;
    int intermediate_size = 8192;
    int num_layers = 32;
    int num_heads = 32;
    int num_kv_heads = 8;
    int max_seq_len = 4096;
};

#endif // GGUF_PARSER_H
