// ============================================================================
// FILE: src/tokenizer.cpp
// PATH: /marisselle-teacher/src/tokenizer.cpp
// PURPOSE: Implementation of Phi-3 BPE tokenizer
// ============================================================================

#include "tokenizer.h"
#include "model_loader.h"
#include <algorithm>
#include <sstream>
#include <regex>
#include <queue>
#include <emscripten.h>
#include <nlohmann/json.hpp>  // Requires nlohmann/json, or implement manual JSON parsing

using json = nlohmann::json;

// ============================================================================
// BYTE ENCODER INITIALIZATION
// ============================================================================

void BPETokenizer::initialize_byte_encoder() {
    // Byte encoder: maps bytes 0-255 to printable Unicode characters
    // This is the GPT-2/Phi-3 byte encoder
    for (int i = 0; i < 256; i++) {
        char c = static_cast<char>(i);
        std::string s(1, c);
        
        if (i == ' ' || i == '!' || i == '"' || i == '#' || i == '$' || i == '%' || i == '&' || i == '\'' ||
            i == '(' || i == ')' || i == '*' || i == '+' || i == ',' || i == '-' || i == '.' || i == '/' ||
            i == '0' || i == '1' || i == '2' || i == '3' || i == '4' || i == '5' || i == '6' || i == '7' ||
            i == '8' || i == '9' || i == ':' || i == ';' || i == '<' || i == '=' || i == '>' || i == '?' ||
            i == '@' || i == 'A' || i == 'B' || i == 'C' || i == 'D' || i == 'E' || i == 'F' || i == 'G' ||
            i == 'H' || i == 'I' || i == 'J' || i == 'K' || i == 'L' || i == 'M' || i == 'N' || i == 'O' ||
            i == 'P' || i == 'Q' || i == 'R' || i == 'S' || i == 'T' || i == 'U' || i == 'V' || i == 'W' ||
            i == 'X' || i == 'Y' || i == 'Z' || i == '[' || i == '\\' || i == ']' || i == '^' || i == '_' ||
            i == '`' || i == 'a' || i == 'b' || i == 'c' || i == 'd' || i == 'e' || i == 'f' || i == 'g' ||
            i == 'h' || i == 'i' || i == 'j' || i == 'k' || i == 'l' || i == 'm' || i == 'n' || i == 'o' ||
            i == 'p' || i == 'q' || i == 'r' || i == 's' || i == 't' || i == 'u' || i == 'v' || i == 'w' ||
            i == 'x' || i == 'y' || i == 'z' || i == '{' || i == '|' || i == '}' || i == '~') {
            // Printable ASCII
            byte_encoder[static_cast<uint8_t>(i)] = s;
        } else {
            // Non-printable: encode as Ā + byte value
            char buf[8];
            snprintf(buf, sizeof(buf), "Ġ%02X", i);
            byte_encoder[static_cast<uint8_t>(i)] = buf;
        }
    }
    
    // Build decoder
    for (const auto& pair : byte_encoder) {
        byte_decoder[pair.second] = pair.first;
    }
}

// ============================================================================
// BPETokenizer CONSTRUCTOR/DESTRUCTOR
// ============================================================================

BPETokenizer::BPETokenizer() {
    initialize_byte_encoder();
}

BPETokenizer::~BPETokenizer() = default;

// ============================================================================
// LOAD FROM JSON
// ============================================================================

bool BPETokenizer::load_from_json(const std::string& json_content) {
    try {
        json data = json::parse(json_content);
        
        // Load vocabulary
        if (data.contains("model") && data["model"].contains("vocab")) {
            for (const auto& [token, id] : data["model"]["vocab"].items()) {
                int token_id = id.get<int>();
                text_to_id[token] = token_id;
                
                Token token_info;
                token_info.id = token_id;
                token_info.text = token;
                token_info.type = TokenType::NORMAL;
                token_info.score = 0.0f;
                id_to_token[token_id] = token_info;
            }
        }
        
        // Load merge rules
        if (data.contains("model") && data["model"].contains("merges")) {
            auto merges = data["model"]["merges"];
            for (size_t i = 0; i < merges.size(); i++) {
                std::string merge_str = merges[i].get<std::string>();
                size_t space_pos = merge_str.find(' ');
                if (space_pos != std::string::npos) {
                    MergeRule rule;
                    rule.first = merge_str.substr(0, space_pos);
                    rule.second = merge_str.substr(space_pos + 1);
                    rule.merged = rule.first + rule.second;
                    rule.priority = static_cast<int>(i);
                    merge_rules.push_back(rule);
                    merge_ranks[rule.first + " " + rule.second] = static_cast<int>(i);
                }
            }
        }
        
        // Load special tokens
        if (data.contains("added_tokens")) {
            for (const auto& token_json : data["added_tokens"]) {
                int id = token_json["id"].get<int>();
                std::string content = token_json["content"].get<std::string>();
                std::string token_type = token_json.value("special", false) ? "special" : "normal";
                
                Token token_info;
                token_info.id = id;
                token_info.text = content;
                token_info.type = (token_type == "special") ? TokenType::CONTROL : TokenType::NORMAL;
                token_info.score = token_json.value("score", 0.0f);
                
                id_to_token[id] = token_info;
                text_to_id[content] = id;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        emscripten_log(EM_LOG_ERROR, "[BPETokenizer] Failed to load JSON: %s", e.what());
        return false;
    }
}

// ============================================================================
// ENCODING
// ============================================================================

std::vector<uint8_t> BPETokenizer::utf8_encode(const std::string& text) {
    std::vector<uint8_t> result;
    for (char c : text) {
        result.push_back(static_cast<uint8_t>(c));
    }
    return result;
}

std::string BPETokenizer::utf8_decode(const std::vector<uint8_t>& bytes) {
    std::string result;
    for (uint8_t b : bytes) {
        result.push_back(static_cast<char>(b));
    }
    return result;
}

std::vector<std::string> BPETokenizer::preprocess(const std::string& text) {
    std::vector<std::string> tokens;
    
    // Convert to UTF-8 bytes
    std::vector<uint8_t> bytes = utf8_encode(text);
    
    // Convert each byte to its printable representation
    for (uint8_t b : bytes) {
        tokens.push_back(byte_encoder[b]);
    }
    
    return tokens;
}

std::vector<std::string> BPETokenizer::byte_pair_encode(const std::vector<std::string>& words) {
    std::vector<std::string> result = words;
    
    // Greedy BPE merging
    bool changed;
    do {
        changed = false;
        
        // Find best merge (highest priority/lowest rank)
        int best_rank = -1;
        int best_index = -1;
        
        for (size_t i = 0; i < result.size() - 1; i++) {
            std::string pair_key = result[i] + " " + result[i + 1];
            auto it = merge_ranks.find(pair_key);
            if (it != merge_ranks.end()) {
                if (best_rank == -1 || it->second < best_rank) {
                    best_rank = it->second;
                    best_index = static_cast<int>(i);
                }
            }
        }
        
        if (best_index != -1) {
            // Merge the pair
            std::string merged = result[best_index] + result[best_index + 1];
            result[best_index] = merged;
            result.erase(result.begin() + best_index + 1);
            changed = true;
        }
    } while (changed);
    
    return result;
}

std::vector<int> BPETokenizer::encode(const std::string& text) {
    std::vector<int> tokens;
    
    // Preprocess: convert bytes to tokens
    std::vector<std::string> preprocessed = preprocess(text);
    
    // Apply BPE merges
    std::vector<std::string> bpe_tokens = byte_pair_encode(preprocessed);
    
    // Convert to token IDs
    for (const std::string& bpe_token : bpe_tokens) {
        auto it = text_to_id.find(bpe_token);
        if (it != text_to_id.end()) {
            tokens.push_back(it->second);
        } else {
            tokens.push_back(unk_token_id);
        }
    }
    
    return tokens;
}

// ============================================================================
// DECODING
// ============================================================================

std::string BPETokenizer::decode(const std::vector<int>& token_ids) {
    std::vector<uint8_t> bytes;
    
    for (int id : token_ids) {
        auto it = id_to_token.find(id);
        if (it != id_to_token.end()) {
            const std::string& token = it->second.text;
            
            // Convert token back to bytes
            if (token.size() == 1 && byte_decoder.find(token) != byte_decoder.end()) {
                bytes.push_back(byte_decoder[token]);
            } else if (token.size() >= 2 && token[0] == 'Ġ') {
                // Hex encoded byte
                std::string hex_str = token.substr(1);
                uint8_t byte_val = static_cast<uint8_t>(std::stoi(hex_str, nullptr, 16));
                bytes.push_back(byte_val);
            } else {
                // Multi-byte token (like " hello")
                std::vector<uint8_t> token_bytes = utf8_encode(token);
                bytes.insert(bytes.end(), token_bytes.begin(), token_bytes.end());
            }
        }
    }
    
    return utf8_decode(bytes);
}

const Token* BPETokenizer::get_token_info(int id) const {
    auto it = id_to_token.find(id);
    if (it != id_to_token.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string BPETokenizer::get_token_text(int id) const {
    auto it = id_to_token.find(id);
    if (it != id_to_token.end()) {
        return it->second.text;
    }
    return "<unk>";
}

TokenType BPETokenizer::get_token_type(int id) const {
    auto it = id_to_token.find(id);
    if (it != id_to_token.end()) {
        return it->second.type;
    }
    return TokenType::UNKNOWN;
}

// ============================================================================
// TOKENIZER FACADE IMPLEMENTATION
// ============================================================================

Tokenizer::Tokenizer() : bpe_tokenizer(std::make_unique<BPETokenizer>()) {}

Tokenizer::~Tokenizer() = default;

bool Tokenizer::load_from_model(ModelLoader* model) {
    // Try to get tokenizer from model metadata
    // If model has tokenizer.json embedded, load it
    // Otherwise use default Phi-3 tokenizer
    return bpe_tokenizer->load_from_json("{}");  // Placeholder
}

bool Tokenizer::load_from_json(const std::string& json_content) {
    return bpe_tokenizer->load_from_json(json_content);
}

std::vector<int> Tokenizer::encode(const std::string& text) {
    if (!bpe_tokenizer) {
        return {};
    }
    return bpe_tokenizer->encode(text);
}

std::string Tokenizer::decode(const std::vector<int>& tokens) {
    if (!bpe_tokenizer) {
        return "";
    }
    return bpe_tokenizer->decode(tokens);
}

std::string Tokenizer::apply_chat_template(const std::vector<std::pair<std::string, std::string>>& messages, bool add_generation_prompt) {
    std::string result;
    
    for (const auto& [role, content] : messages) {
        if (role == "system") {
            result += "<|system|>\n" + content + "<|end|>\n";
        } else if (role == "user") {
            result += "<|user|>\n" + content + "<|end|>\n";
        } else if (role == "assistant") {
            result += "<|assistant|>\n" + content + "<|end|>\n";
        }
    }
    
    if (add_generation_prompt) {
        result += "<|assistant|>\n";
    }
    
    return result;
}

int Tokenizer::get_vocab_size() const {
    return bpe_tokenizer ? bpe_tokenizer->get_vocab_size() : 0;
}

bool Tokenizer::is_special_token(int id) const {
    auto info = bpe_tokenizer->get_token_info(id);
    return info && (info->type == TokenType::CONTROL || info->type == TokenType::USER || 
                    info->type == TokenType::ASSISTANT || info->type == TokenType::SYSTEM ||
                    info->type == TokenType::END);
}
