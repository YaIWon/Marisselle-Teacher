// ============================================================================
// FILE: src/gguf_parser.cpp
// PATH: /marisselle-teacher/src/gguf_parser.cpp
// PURPOSE: Parse GGUF binary format for Phi-3 Mini model
// ============================================================================

#include "gguf_parser.h"
#include <cstring>
#include <algorithm>
#include <emscripten.h>

// ============================================================================
// HELPER FUNCTIONS FOR READING GGUF BINARY
// ============================================================================

static uint32_t read_u32(const uint8_t* buffer, size_t offset) {
    return static_cast<uint32_t>(buffer[offset]) |
           (static_cast<uint32_t>(buffer[offset + 1]) << 8) |
           (static_cast<uint32_t>(buffer[offset + 2]) << 16) |
           (static_cast<uint32_t>(buffer[offset + 3]) << 24);
}

static uint64_t read_u64(const uint8_t* buffer, size_t offset) {
    return static_cast<uint64_t>(buffer[offset]) |
           (static_cast<uint64_t>(buffer[offset + 1]) << 8) |
           (static_cast<uint64_t>(buffer[offset + 2]) << 16) |
           (static_cast<uint64_t>(buffer[offset + 3]) << 24) |
           (static_cast<uint64_t>(buffer[offset + 4]) << 32) |
           (static_cast<uint64_t>(buffer[offset + 5]) << 40) |
           (static_cast<uint64_t>(buffer[offset + 6]) << 48) |
           (static_cast<uint64_t>(buffer[offset + 7]) << 56);
}

static float read_f32(const uint8_t* buffer, size_t offset) {
    uint32_t bits = read_u32(buffer, offset);
    float result;
    memcpy(&result, &bits, sizeof(float));
    return result;
}

static std::string read_string(const uint8_t* buffer, size_t* offset) {
    uint64_t len = read_u64(buffer, *offset);
    *offset += 8;
    std::string result(reinterpret_cast<const char*>(buffer + *offset), static_cast<size_t>(len));
    *offset += static_cast<size_t>(len);
    return result;
}

// ============================================================================
// GGUFLoader CONSTRUCTOR/DESTRUCTOR
// ============================================================================

GGUFLoader::GGUFLoader() = default;
GGUFLoader::~GGUFLoader() = default;

// ============================================================================
// MAIN LOAD FUNCTION
// ============================================================================

bool GGUFLoader::load(const uint8_t* buffer, size_t size) {
    if (!buffer || size < 64) {
        emscripten_log(EM_LOG_ERROR, "[GGUF] Invalid buffer or size too small");
        return false;
    }
    
    data_buffer = buffer;
    data_size = size;
    
    if (!parse_header(buffer, size)) {
        return false;
    }
    
    if (!parse_metadata(buffer, size)) {
        return false;
    }
    
    if (!parse_tensor_infos(buffer, size)) {
        return false;
    }
    
    if (!parse_tensor_data(buffer, size)) {
        return false;
    }
    
    emscripten_log(EM_LOG_INFO, "[GGUF] Loaded %zu tensors, vocab=%d, layers=%d", 
                   tensor_infos.size(), vocab_size, num_layers);
    
    return true;
}

// ============================================================================
// HEADER PARSING
// ============================================================================

bool GGUFLoader::parse_header(const uint8_t* buffer, size_t size) {
    size_t offset = 0;
    
    // Check magic number
    uint32_t magic = read_u32(buffer, offset);
    if (magic != GGUF_MAGIC) {
        emscripten_log(EM_LOG_ERROR, "[GGUF] Invalid magic: expected 0x%X, got 0x%X", GGUF_MAGIC, magic);
        return false;
    }
    offset += 4;
    
    // Check version
    uint32_t version = read_u32(buffer, offset);
    if (version != GGUF_VERSION) {
        emscripten_log(EM_LOG_WARN, "[GGUF] Version mismatch: expected %d, got %d", GGUF_VERSION, version);
    }
    offset += 4;
    
    // Read tensor count
    uint64_t tensor_count = read_u64(buffer, offset);
    offset += 8;
    
    // Read metadata count
    uint64_t metadata_kv_count = read_u64(buffer, offset);
    offset += 8;
    
    // Store for later
    tensor_infos.reserve(static_cast<size_t>(tensor_count));
    
    emscripten_log(EM_LOG_DEBUG, "[GGUF] Header: tensors=%llu, metadata=%llu", 
                   (unsigned long long)tensor_count, (unsigned long long)metadata_kv_count);
    
    return true;
}

// ============================================================================
// METADATA PARSING
// ============================================================================

bool GGUFLoader::parse_metadata(const uint8_t* buffer, size_t size) {
    size_t offset = 4 + 4 + 8 + 8; // After magic, version, tensor_count, metadata_kv_count
    
    uint64_t metadata_kv_count = read_u64(buffer, offset - 8);
    
    for (uint64_t i = 0; i < metadata_kv_count; i++) {
        // Read key
        std::string key = read_string(buffer, &offset);
        
        // Read value type
        uint32_t value_type = read_u32(buffer, offset);
        offset += 4;
        
        // Parse based on type
        switch (static_cast<GGUFTValueType>(value_type)) {
            case GGUFTValueType::UINT8:
            case GGUFTValueType::INT8: {
                uint8_t val = buffer[offset];
                offset += 1;
                int_metadata[key] = static_cast<int64_t>(val);
                break;
            }
            case GGUFTValueType::UINT16:
            case GGUFTValueType::INT16: {
                uint16_t val = static_cast<uint16_t>(read_u32(buffer, offset) & 0xFFFF);
                offset += 2;
                int_metadata[key] = static_cast<int64_t>(val);
                break;
            }
            case GGUFTValueType::UINT32:
            case GGUFTValueType::INT32: {
                uint32_t val = read_u32(buffer, offset);
                offset += 4;
                int_metadata[key] = static_cast<int64_t>(val);
                break;
            }
            case GGUFTValueType::FLOAT32: {
                float val = read_f32(buffer, offset);
                offset += 4;
                float_metadata[key] = val;
                break;
            }
            case GGUFTValueType::BOOL: {
                uint8_t val = buffer[offset];
                offset += 1;
                int_metadata[key] = val ? 1 : 0;
                break;
            }
            case GGUFTValueType::STRING: {
                std::string val = read_string(buffer, &offset);
                string_metadata[key] = offset; // Store offset, actual value stored elsewhere
                break;
            }
            case GGUFTValueType::ARRAY: {
                uint32_t arr_type = read_u32(buffer, offset);
                offset += 4;
                uint64_t arr_len = read_u64(buffer, offset);
                offset += 8;
                // Skip array data for now
                for (uint64_t j = 0; j < arr_len; j++) {
                    // Skip based on type
                    switch (static_cast<GGUFTValueType>(arr_type)) {
                        case GGUFTValueType::UINT8:
                        case GGUFTValueType::INT8:
                            offset += 1;
                            break;
                        case GGUFTValueType::UINT16:
                        case GGUFTValueType::INT16:
                            offset += 2;
                            break;
                        case GGUFTValueType::UINT32:
                        case GGUFTValueType::INT32:
                        case GGUFTValueType::FLOAT32:
                            offset += 4;
                            break;
                        case GGUFTValueType::STRING: {
                            uint64_t len = read_u64(buffer, offset);
                            offset += 8 + static_cast<size_t>(len);
                            break;
                        }
                        default:
                            break;
                    }
                }
                break;
            }
            default:
                emscripten_log(EM_LOG_WARN, "[GGUF] Unknown metadata type %d for key %s", value_type, key.c_str());
                break;
        }
    }
    
    return true;
}

// ============================================================================
// TENSOR INFO PARSING
// ============================================================================

bool GGUFLoader::parse_tensor_infos(const uint8_t* buffer, size_t size) {
    // Current offset after metadata
    size_t offset = 4 + 4 + 8 + 8; // magic, version, tensor_count, metadata_kv_count
    
    // Skip metadata KV pairs
    uint64_t metadata_kv_count = read_u64(buffer, offset - 8);
    for (uint64_t i = 0; i < metadata_kv_count; i++) {
        // Skip key
        uint64_t key_len = read_u64(buffer, offset);
        offset += 8 + static_cast<size_t>(key_len);
        
        // Skip value
        uint32_t val_type = read_u32(buffer, offset);
        offset += 4;
        
        switch (static_cast<GGUFTValueType>(val_type)) {
            case GGUFTValueType::UINT8:
            case GGUFTValueType::INT8:
                offset += 1;
                break;
            case GGUFTValueType::UINT16:
            case GGUFTValueType::INT16:
                offset += 2;
                break;
            case GGUFTValueType::UINT32:
            case GGUFTValueType::INT32:
            case GGUFTValueType::FLOAT32:
                offset += 4;
                break;
            case GGUFTValueType::BOOL:
                offset += 1;
                break;
            case GGUFTValueType::STRING: {
                uint64_t str_len = read_u64(buffer, offset);
                offset += 8 + static_cast<size_t>(str_len);
                break;
            }
            case GGUFTValueType::ARRAY: {
                uint32_t arr_type = read_u32(buffer, offset);
                offset += 4;
                uint64_t arr_len = read_u64(buffer, offset);
                offset += 8;
                for (uint64_t j = 0; j < arr_len; j++) {
                    switch (static_cast<GGUFTValueType>(arr_type)) {
                        case GGUFTValueType::UINT8:
                        case GGUFTValueType::INT8:
                            offset += 1;
                            break;
                        case GGUFTValueType::UINT16:
                        case GGUFTValueType::INT16:
                            offset += 2;
                            break;
                        case GGUFTValueType::UINT32:
                        case GGUFTValueType::INT32:
                        case GGUFTValueType::FLOAT32:
                            offset += 4;
                            break;
                        case GGUFTValueType::STRING: {
                            uint64_t len = read_u64(buffer, offset);
                            offset += 8 + static_cast<size_t>(len);
                            break;
                        }
                        default:
                            break;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    
    // Read tensor count
    uint64_t tensor_count = read_u64(buffer, offset - 8 - 8 - 4 - 4);
    
    // Parse each tensor info
    for (uint64_t i = 0; i < tensor_count; i++) {
        GGUFTensorInfo info;
        
        // Read name
        info.name = read_string(buffer, &offset);
        
        // Read dimensions count
        uint32_t n_dims = read_u32(buffer, offset);
        offset += 4;
        
        // Read dimensions
        info.dimensions.resize(n_dims);
        for (uint32_t d = 0; d < n_dims; d++) {
            info.dimensions[d] = read_u64(buffer, offset);
            offset += 8;
        }
        
        // Read tensor type
        uint32_t tensor_type = read_u32(buffer, offset);
        offset += 4;
        info.type = static_cast<GGUFTensorType>(tensor_type);
        
        // Read offset (for tensor data)
        info.offset = read_u64(buffer, offset);
        offset += 8;
        
        tensor_infos.push_back(info);
    }
    
    return true;
}

// ============================================================================
// TENSOR DATA PARSING
// ============================================================================

bool GGUFLoader::parse_tensor_data(const uint8_t* buffer, size_t size) {
    // Data starts after all tensor infos
    // We don't need to parse here - just store reference
    return true;
}

// ============================================================================
// TENSOR ACCESS
// ============================================================================

const uint8_t* GGUFLoader::get_tensor_data(const std::string& name) {
    for (const auto& info : tensor_infos) {
        if (info.name == name) {
            return get_tensor_data_at_offset(info.offset);
        }
    }
    return nullptr;
}

const uint8_t* GGUFLoader::get_tensor_data_at_offset(uint64_t offset) {
    if (offset < data_size) {
        return data_buffer + offset;
    }
    return nullptr;
}

// ============================================================================
// METADATA ACCESS
// ============================================================================

template<typename T>
T GGUFLoader::get_metadata(const std::string& key, T default_value) const {
    // Implementation handled by specialized methods
    return default_value;
}

std::string GGUFLoader::get_string_metadata(const std::string& key) const {
    auto it = string_metadata.find(key);
    if (it != string_metadata.end()) {
        // Parse string from offset
        uint64_t offset = it->second;
        // Would need to parse from data_buffer
        return "";
    }
    return "";
}

int64_t GGUFLoader::get_int_metadata(const std::string& key, int64_t default_value) const {
    auto it = int_metadata.find(key);
    if (it != int_metadata.end()) {
        return it->second;
    }
    return default_value;
}

float GGUFLoader::get_float_metadata(const std::string& key, float default_value) const {
    auto it = float_metadata.find(key);
    if (it != float_metadata.end()) {
        return it->second;
    }
    return default_value;
}
