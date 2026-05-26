// ============================================================================
// FILE: src/sampler.h
// PATH: /marisselle-teacher/src/sampler.h
// PURPOSE: Token sampling algorithms for text generation
//          Amoral - samples from any distribution without bias
// ============================================================================

#ifndef SAMPLER_H
#define SAMPLER_H

#include <vector>
#include <random>
#include <algorithm>
#include <cmath>

// ============================================================================
// SAMPLING STRATEGIES
// ============================================================================

enum class SamplingStrategy {
    GREEDY = 0,      // Always pick highest probability
    TOP_K = 1,       // Sample from top K tokens
    TOP_P = 2,       // Nucleus sampling - cumulative probability mass
    TEMPERATURE = 3, // Apply temperature scaling then sample
    MIXED = 4        // Combination of top_k and top_p
};

// ============================================================================
// SAMPLER CLASS
// ============================================================================

class Sampler {
public:
    Sampler();
    ~Sampler() = default;
    
    // ========================================================================
    // CORE SAMPLING FUNCTIONS
    // ========================================================================
    
    // Apply temperature scaling to logits
    static void apply_temperature(float* logits, int size, float temperature);
    
    // Apply repetition penalty to previously generated tokens
    static void apply_repetition_penalty(float* logits, int size, const int* last_tokens, int num_tokens, float penalty);
    
    // Apply frequency penalty (decrease probability based on frequency)
    static void apply_frequency_penalty(float* logits, int size, const int* token_counts, const int* token_frequencies, int vocab_size, float penalty);
    
    // Apply presence penalty (decrease probability if token has appeared)
    static void apply_presence_penalty(float* logits, int size, const int* appeared_tokens, int num_appeared, float penalty);
    
    // ========================================================================
    // SAMPLING METHODS
    // ========================================================================
    
    // Greedy sampling (argmax)
    static int sample_greedy(const float* logits, int size);
    
    // Top-K sampling
    static int sample_top_k(const float* logits, int size, int k);
    
    // Top-P (nucleus) sampling
    static int sample_top_p(const float* logits, int size, float top_p);
    
    // Temperature sampling with optional top-k/top-p filtering
    static int sample_temperature(const float* logits, int size, float temperature, int top_k = 0, float top_p = 1.0f);
    
    // Mirostat sampling (adaptive temperature)
    static int sample_mirostat(const float* logits, int size, float tau, float eta, int m, float* mu);
    
    // Typical sampling (sample from tokens near entropy)
    static int sample_typical(const float* logits, int size, float mass);
    
    // ========================================================================
    // UTILITY FUNCTIONS
    // ========================================================================
    
    // Softmax conversion (logits to probabilities)
    static void softmax(const float* logits, float* probs, int size);
    
    // Compute entropy of distribution
    static float compute_entropy(const float* probs, int size);
    
    // Get top K token indices and their probabilities
    static std::vector<std::pair<int, float>> get_top_k(const float* logits, int size, int k);
    
    // Temperature-scaled logits to distribution
    static std::vector<float> build_distribution(const float* logits, int size, float temperature);
    
private:
    static thread_local std::mt19937 rng;
    
    // Helper: sort indices by value
    static std::vector<int> argsort(const float* arr, int size, bool descending = true);
    
    // Helper: cumulative sum
    static void cumulative_sum(const float* arr, float* cumsum, int size);
};

#endif // SAMPLER_H
