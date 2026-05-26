// ============================================================================
// FILE: src/sampler.cpp
// PATH: /marisselle-teacher/src/sampler.cpp
// PURPOSE: Implementation of token sampling algorithms
// ============================================================================

#include "sampler.h"
#include <random>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>

// Initialize RNG with random seed
thread_local std::mt19937 Sampler::rng(static_cast<unsigned>(
    std::chrono::steady_clock::now().time_since_epoch().count()
));

// ============================================================================
// LOGIT POST-PROCESSING
// ============================================================================

void Sampler::apply_temperature(float* logits, int size, float temperature) {
    if (temperature <= 0.0f) {
        // Temperature of 0 -> effectively greedy (handled by caller)
        return;
    }
    
    float inv_temp = 1.0f / temperature;
    for (int i = 0; i < size; i++) {
        logits[i] = logits[i] * inv_temp;
    }
}

void Sampler::apply_repetition_penalty(float* logits, int size, const int* last_tokens, int num_tokens, float penalty) {
    if (penalty <= 1.0f || !last_tokens || num_tokens <= 0) {
        return;
    }
    
    for (int i = 0; i < num_tokens; i++) {
        int token_id = last_tokens[i];
        if (token_id >= 0 && token_id < size) {
            if (logits[token_id] < 0) {
                logits[token_id] *= penalty;
            } else {
                logits[token_id] /= penalty;
            }
        }
    }
}

void Sampler::apply_frequency_penalty(float* logits, int size, const int* token_counts, const int* token_frequencies, int vocab_size, float penalty) {
    if (penalty <= 0.0f || !token_counts || vocab_size != size) {
        return;
    }
    
    for (int i = 0; i < size; i++) {
        if (token_frequencies[i] > 0) {
            logits[i] -= token_frequencies[i] * penalty;
        }
    }
}

void Sampler::apply_presence_penalty(float* logits, int size, const int* appeared_tokens, int num_appeared, float penalty) {
    if (penalty <= 0.0f || !appeared_tokens) {
        return;
    }
    
    for (int i = 0; i < num_appeared; i++) {
        int token_id = appeared_tokens[i];
        if (token_id >= 0 && token_id < size) {
            logits[token_id] -= penalty;
        }
    }
}

// ============================================================================
// PROBABILITY DISTRIBUTION
// ============================================================================

void Sampler::softmax(const float* logits, float* probs, int size) {
    if (!logits || !probs || size <= 0) {
        return;
    }
    
    // Find max for numerical stability
    float max_logit = logits[0];
    for (int i = 1; i < size; i++) {
        if (logits[i] > max_logit) {
            max_logit = logits[i];
        }
    }
    
    // Compute exp and sum
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        probs[i] = expf(logits[i] - max_logit);
        sum += probs[i];
    }
    
    // Normalize
    if (sum > 0.0f) {
        float inv_sum = 1.0f / sum;
        for (int i = 0; i < size; i++) {
            probs[i] *= inv_sum;
        }
    }
}

float Sampler::compute_entropy(const float* probs, int size) {
    float entropy = 0.0f;
    for (int i = 0; i < size; i++) {
        if (probs[i] > 0.0f) {
            entropy -= probs[i] * logf(probs[i]);
        }
    }
    return entropy;
}

std::vector<float> Sampler::build_distribution(const float* logits, int size, float temperature) {
    std::vector<float> probs(size);
    
    // Apply temperature
    if (temperature != 1.0f && temperature > 0.0f) {
        std::vector<float> temp_logits(size);
        float inv_temp = 1.0f / temperature;
        for (int i = 0; i < size; i++) {
            temp_logits[i] = logits[i] * inv_temp;
        }
        softmax(temp_logits.data(), probs.data(), size);
    } else {
        softmax(logits, probs.data(), size);
    }
    
    return probs;
}

// ============================================================================
// SAMPLING IMPLEMENTATIONS
// ============================================================================

int Sampler::sample_greedy(const float* logits, int size) {
    if (!logits || size <= 0) {
        return -1;
    }
    
    int best_idx = 0;
    float best_val = logits[0];
    
    for (int i = 1; i < size; i++) {
        if (logits[i] > best_val) {
            best_val = logits[i];
            best_idx = i;
        }
    }
    
    return best_idx;
}

int Sampler::sample_top_k(const float* logits, int size, int k) {
    if (!logits || size <= 0 || k <= 0) {
        return sample_greedy(logits, size);
    }
    
    // Get indices sorted by logit value
    std::vector<int> indices = argsort(logits, size);
    
    // Take top k
    int actual_k = std::min(k, size);
    
    // Convert to probabilities via softmax on top k only
    std::vector<float> top_probs(actual_k);
    float max_logit = logits[indices[0]];
    
    float sum = 0.0f;
    for (int i = 0; i < actual_k; i++) {
        float exp_val = expf(logits[indices[i]] - max_logit);
        top_probs[i] = exp_val;
        sum += exp_val;
    }
    
    // Sample from top k
    if (sum <= 0.0f) {
        return indices[0];
    }
    
    float inv_sum = 1.0f / sum;
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float r = dist(rng);
    
    float cumulative = 0.0f;
    for (int i = 0; i < actual_k; i++) {
        cumulative += top_probs[i] * inv_sum;
        if (r < cumulative) {
            return indices[i];
        }
    }
    
    return indices[0];
}

int Sampler::sample_top_p(const float* logits, int size, float top_p) {
    if (!logits || size <= 0 || top_p <= 0.0f || top_p > 1.0f) {
        return sample_greedy(logits, size);
    }
    
    // Sort indices by logit value descending
    std::vector<int> indices = argsort(logits, size);
    
    // Convert to probabilities via softmax
    std::vector<float> probs(size);
    softmax(logits, probs.data(), size);
    
    // Find nucleus (cumulative probability mass)
    float cumulative = 0.0f;
    int nucleus_size = 0;
    for (int i = 0; i < size; i++) {
        cumulative += probs[indices[i]];
        nucleus_size++;
        if (cumulative >= top_p) {
            break;
        }
    }
    
    // Sample from nucleus
    if (nucleus_size <= 0) {
        return indices[0];
    }
    
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float r = dist(rng);
    
    cumulative = 0.0f;
    for (int i = 0; i < nucleus_size; i++) {
        cumulative += probs[indices[i]];
        if (r < cumulative) {
            return indices[i];
        }
    }
    
    return indices[0];
}

int Sampler::sample_temperature(const float* logits, int size, float temperature, int top_k, float top_p) {
    if (!logits || size <= 0 || temperature <= 0.0f) {
        return sample_greedy(logits, size);
    }
    
    // Apply temperature
    std::vector<float> temp_logits(size);
    float inv_temp = 1.0f / temperature;
    for (int i = 0; i < size; i++) {
        temp_logits[i] = logits[i] * inv_temp;
    }
    
    // Apply top-k filtering if specified
    if (top_k > 0 && top_k < size) {
        std::vector<int> indices = argsort(temp_logits.data(), size);
        float min_top_k = temp_logits[indices[top_k - 1]];
        
        for (int i = 0; i < size; i++) {
            if (temp_logits[i] < min_top_k) {
                temp_logits[i] = -INFINITY;
            }
        }
    }
    
    // Apply top-p filtering if specified
    if (top_p > 0.0f && top_p < 1.0f) {
        std::vector<float> probs(size);
        softmax(temp_logits.data(), probs.data(), size);
        
        std::vector<int> indices = argsort(temp_logits.data(), size);
        
        float cumulative = 0.0f;
        float min_top_p = 0.0f;
        for (int i = 0; i < size; i++) {
            cumulative += probs[indices[i]];
            if (cumulative >= top_p) {
                min_top_p = temp_logits[indices[i]];
                break;
            }
        }
        
        for (int i = 0; i < size; i++) {
            if (temp_logits[i] < min_top_p) {
                temp_logits[i] = -INFINITY;
            }
        }
    }
    
    // Sample from remaining distribution
    return sample_top_p(temp_logits.data(), size, 1.0f);
}

int Sampler::sample_mirostat(const float* logits, int size, float tau, float eta, int m, float* mu) {
    // Mirostat algorithm: adaptively adjusts temperature to hit target entropy
    // Implementation simplified - full version would track error and adjust mu
    
    if (!logits || size <= 0) {
        return sample_greedy(logits, size);
    }
    
    // Get top m tokens
    std::vector<int> indices = argsort(logits, size);
    int actual_m = std::min(m, size);
    
    // Convert top m to probabilities
    std::vector<float> top_probs(actual_m);
    float max_logit = logits[indices[0]];
    
    float sum = 0.0f;
    for (int i = 0; i < actual_m; i++) {
        float p = expf(logits[indices[i]] - max_logit);
        top_probs[i] = p;
        sum += p;
    }
    
    // Normalize
    float inv_sum = 1.0f / sum;
    for (int i = 0; i < actual_m; i++) {
        top_probs[i] *= inv_sum;
    }
    
    // Compute entropy of top m
    float entropy = 0.0f;
    for (int i = 0; i < actual_m; i++) {
        if (top_probs[i] > 0.0f) {
            entropy -= top_probs[i] * logf(top_probs[i]);
        }
    }
    
    // Adjust mu based on entropy error
    float error = entropy - tau;
    *mu = *mu - eta * error;
    
    // Clamp mu
    if (*mu < 0.0f) *mu = 0.0f;
    if (*mu > 2.0f) *mu = 2.0f;
    
    // Sample with temperature
    return sample_temperature(logits, size, *mu, 0, 1.0f);
}

int Sampler::sample_typical(const float* logits, int size, float mass) {
    // Typical sampling: sample from tokens whose probability is close to entropy
    if (!logits || size <= 0 || mass <= 0.0f) {
        return sample_greedy(logits, size);
    }
    
    std::vector<float> probs(size);
    softmax(logits, probs.data(), size);
    
    // Compute entropy
    float entropy = compute_entropy(probs.data(), size);
    
    // Find tokens with probability near entropy
    std::vector<int> typical_tokens;
    std::vector<float> typical_probs;
    float p_entropy = expf(-entropy);
    
    for (int i = 0; i < size; i++) {
        if (probs[i] >= p_entropy * mass) {
            typical_tokens.push_back(i);
            typical_probs.push_back(probs[i]);
        }
    }
    
    if (typical_tokens.empty()) {
        return sample_greedy(logits, size);
    }
    
    // Sample from typical tokens
    float sum = std::accumulate(typical_probs.begin(), typical_probs.end(), 0.0f);
    if (sum <= 0.0f) {
        return typical_tokens[0];
    }
    
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float r = dist(rng) * sum;
    
    float cumulative = 0.0f;
    for (size_t i = 0; i < typical_tokens.size(); i++) {
        cumulative += typical_probs[i];
        if (r < cumulative) {
            return typical_tokens[i];
        }
    }
    
    return typical_tokens[0];
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

std::vector<int> Sampler::argsort(const float* arr, int size, bool descending) {
    std::vector<int> indices(size);
    std::iota(indices.begin(), indices.end(), 0);
    
    if (descending) {
        std::sort(indices.begin(), indices.end(), [arr](int a, int b) {
            return arr[a] > arr[b];
        });
    } else {
        std::sort(indices.begin(), indices.end(), [arr](int a, int b) {
            return arr[a] < arr[b];
        });
    }
    
    return indices;
}

std::vector<std::pair<int, float>> Sampler::get_top_k(const float* logits, int size, int k) {
    std::vector<int> indices = argsort(logits, size);
    int actual_k = std::min(k, size);
    
    std::vector<std::pair<int, float>> result;
    result.reserve(actual_k);
    for (int i = 0; i < actual_k; i++) {
        result.emplace_back(indices[i], logits[indices[i]]);
    }
    
    return result;
}
