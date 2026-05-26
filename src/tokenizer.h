// ============================================================================
// FILE: src/tokenizer.h
// PATH: /marisselle-teacher/src/tokenizer.h
// PURPOSE: Phi-3 tokenizer with BPE and special tokens
//          Amoral - tokenizes any input without filtering
// ============================================================================

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

// ============================================================================
// TOKEN TYPE ENUMERATION
// ============================================================================

enum class TokenType {
    NORMAL = 0,
    UNKNOWN = 1,
    CONTROL = 2,
    USER = 3,
    ASSISTANT = 4,
    SYSTEM = 5,
    END = 6,
    PAD = 7,
    BOS = 8,
    EOS = 9
};

// ============================================================================
// TOKEN STRUCTURE
// ============================================================================

struct Token {
    int id;
    std::string text;
    TokenType type;
    float score;  // For merges, higher = more frequent
};

// ============================================================================
// BYTE PAIR ENCODING TOKENIZER
// ============================================================================

class BPETokenizer {
public:
    BPETokenizer();
    ~BPETokenizer();
    
    // Load tokenizer from JSON (tokenizer.json format)
    bool load_from_json(const std::string& json_content);
    
    // Load tokenizer from model metadata
    bool load_from_model(class ModelLoader* model);
    
    // Encode text to token IDs
    std::vector<int> encode(const std::string& text);
    
    // Decode token IDs to text
    std::string decode(const std::vector<int>& tokens);
    
    // Get token info
    const Token* get_token_info(int id) const;
    std::string get_token_text(int id) const;
    TokenType get_token_type(int id) const;
    
    // Special token IDs (Phi-3 specific)
    int get_bos_token_id() const { return bos_token_id; }
    int get_eos_token_id() const { return eos_token_id; }
    int get_pad_token_id() const { return pad_token_id; }
    int get_unk_token_id() const { return unk_token_id; }
    
    // Vocabulary size
    int get_vocab_size() const { return static_cast<int>(id_to_token.size()); }
    
    // Amoral - no content filtering
    bool is_allowed(const std::string& text) const { return true; }
    
private:
    // BPE operations
    std::vector<std::string> preprocess(const std::string& text);
    std::vector<std::string> byte_pair_encode(const std::vector<std::string>& words);
    std::string byte_pair_merge(const std::vector<std::string>& parts);
    
    // Byte-level encoding (UTF-8)
    std::vector<uint8_t> utf8_encode(const std::string& text);
    std::string utf8_decode(const std::vector<uint8_t>& bytes);
    
    // Byte pair merge rules
    struct MergeRule {
        std::string first;
        std::string second;
        std::string merged;
        int priority;
    };
    
    std::vector<MergeRule> merge_rules;
    std::unordered_map<std::string, int> merge_ranks;
    
    // Token maps
    std::unordered_map<std::string, int> text_to_id;
    std::unordered_map<int, Token> id_to_token;
    
    // Special token IDs (Phi-3)
    int bos_token_id = 1;
    int eos_token_id = 2;
    int pad_token_id = 3;
    int unk_token_id = 0;
    
    // Chat template tokens (Phi-3 specific)
    int user_token_id = 4;
    int assistant_token_id = 5;
    int system_token_id = 6;
    int end_token_id = 7;
    
    // Byte encoder (bytes 0-255 to printable representation)
    std::unordered_map<uint8_t, std::string> byte_encoder;
    std::unordered_map<std::string, uint8_t> byte_decoder;
    
    void initialize_byte_encoder();
};

// ============================================================================
// MAIN TOKENIZER CLASS (facade)
// ============================================================================

class Tokenizer {
public:
    Tokenizer();
    ~Tokenizer();
    
    // Initialize from model
    bool load_from_model(class ModelLoader* model);
    
    // Initialize from JSON file
    bool load_from_json(const std::string& json_content);
    
    // Encode text to token IDs (amoral - all input accepted)
    std::vector<int> encode(const std::string& text);
    
    // Decode token IDs to text
    std::string decode(const std::vector<int>& tokens);
    
    // Apply chat template
    std::string apply_chat_template(const std::vector<std::pair<std::string, std::string>>& messages, bool add_generation_prompt = true);
    
    // Token utilities
    int get_vocab_size() const;
    bool is_special_token(int id) const;
    
private:
    std::unique_ptr<BPETokenizer> bpe_tokenizer;
    std::string chat_template;
    bool is_initialized = false;
};

#endif // TOKENIZER_H
