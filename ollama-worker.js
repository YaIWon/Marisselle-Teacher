// ============================================================================
// FILE: marisselle-teacher/ollama-worker.js
// WEB WORKER FOR OLLAMA WASM - Isolated inference thread
// ============================================================================

// Worker self reference
const ctx = self;

// Model state
let wasmModule = null;
let wasmMemory = null;
let modelBuffer = null;
let isReady = false;
let inferenceQueue = [];

// Configuration
const MAX_BATCH_SIZE = 32;
const MAX_SEQUENCE_LEN = 2048;

// ============================================================================
// INITIALIZATION
// ============================================================================

ctx.addEventListener('message', async (event) => {
    const { type, data } = event.data;
    
    switch (type) {
        case 'init':
            await initializeWASM(data);
            break;
        case 'generate':
            await handleGenerate(data);
            break;
        case 'batch_generate':
            await handleBatchGenerate(data);
            break;
        case 'get_status':
            sendStatus();
            break;
        case 'load_model':
            await loadModel(data);
            break;
    }
});

async function initializeWASM(config) {
    try {
        // Create shared memory
        wasmMemory = new WebAssembly.Memory({
            initial: 256,
            maximum: 4096,
            shared: true
        });
        
        // Load WASM module
        const wasmBytes = await fetch(config.wasmPath).then(r => r.arrayBuffer());
        const module = await WebAssembly.instantiate(wasmBytes, {
            env: {
                memory: wasmMemory,
                console_log: (ptr, len) => {
                    const bytes = new Uint8Array(wasmMemory.buffer, ptr, len);
                    const text = new TextDecoder().decode(bytes);
                    ctx.postMessage({ type: 'log', data: text });
                },
                get_timestamp: () => performance.now(),
                thread_count: config.threads || 4
            }
        });
        
        wasmModule = module.instance.exports;
        
        ctx.postMessage({ type: 'initialized', data: { success: true } });
        
        // Load model if path provided
        if (config.modelPath) {
            await loadModel({ modelPath: config.modelPath });
        }
        
    } catch (error) {
        ctx.postMessage({ type: 'error', data: error.message });
    }
}

async function loadModel(config) {
    try {
        ctx.postMessage({ type: 'status', data: { stage: 'loading_model', progress: 0 } });
        
        const response = await fetch(config.modelPath);
        const totalSize = parseInt(response.headers.get('content-length') || '0');
        const chunks = [];
        let loadedSize = 0;
        
        const reader = response.body.getReader();
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            
            chunks.push(value);
            loadedSize += value.length;
            const progress = (loadedSize / totalSize) * 100;
            
            ctx.postMessage({ type: 'status', data: { stage: 'loading_model', progress } });
        }
        
        modelBuffer = new Uint8Array(await new Blob(chunks).arrayBuffer());
        
        // Load into WASM
        const modelPtr = wasmModule.alloc(modelBuffer.length);
        wasmModule.heap.set(modelBuffer, modelPtr);
        wasmModule.load_model(modelPtr, modelBuffer.length);
        wasmModule.free(modelPtr);
        
        isReady = true;
        
        ctx.postMessage({ type: 'model_loaded', data: { 
            size: modelBuffer.length,
            ready: true 
        }});
        
        // Process queued inferences
        processQueue();
        
    } catch (error) {
        ctx.postMessage({ type: 'error', data: `Model load failed: ${error.message}` });
    }
}

// ============================================================================
// INFERENCE ENGINE
// ============================================================================

async function handleGenerate(data) {
    const { id, prompt, tokens, temperature, topP, repeatPenalty, maxTokens } = data;
    
    if (!isReady) {
        inferenceQueue.push({ id, prompt, tokens, temperature, topP, repeatPenalty, maxTokens });
        return;
    }
    
    const startTime = performance.now();
    
    try {
        // Tokenize input
        const inputTokens = tokens || tokenize(prompt);
        
        // Prepare KV cache
        const kvCachePtr = wasmModule.alloc_kv_cache(MAX_SEQUENCE_LEN);
        
        // Run inference
        const result = await runInference(
            inputTokens, 
            temperature, 
            topP, 
            repeatPenalty, 
            maxTokens,
            kvCachePtr
        );
        
        // Free cache
        wasmModule.free_kv_cache(kvCachePtr);
        
        const inferenceTime = performance.now() - startTime;
        
        ctx.postMessage({
            type: 'generation_complete',
            data: {
                id,
                text: result.text,
                tokens: result.tokens,
                inferenceTime,
                tokensPerSecond: (result.tokens / inferenceTime) * 1000
            }
        });
        
    } catch (error) {
        ctx.postMessage({
            type: 'generation_error',
            data: { id, error: error.message }
        });
    }
}

async function runInference(inputTokens, temperature, topP, repeatPenalty, maxTokens, kvCachePtr) {
    let tokens = [...inputTokens];
    let generatedTokens = [];
    let isDone = false;
    
    // Prefill
    for (let i = 0; i < tokens.length - 1; i++) {
        wasmModule.eval_token(tokens[i], kvCachePtr);
    }
    
    let lastToken = tokens[tokens.length - 1];
    
    for (let step = 0; step < maxTokens && !isDone; step++) {
        // Forward pass
        const logits = wasmModule.forward(lastToken, kvCachePtr);
        
        // Apply temperature
        if (temperature > 0) {
            applyTemperature(logits, temperature);
        }
        
        // Apply top-p sampling
        const nextToken = topPSample(logits, topP);
        
        // Check for stop token
        if (nextToken === 2 || nextToken === 0) { // EOS or pad
            isDone = true;
            break;
        }
        
        generatedTokens.push(nextToken);
        lastToken = nextToken;
        
        // Apply repetition penalty
        if (repeatPenalty > 1.0) {
            applyRepetitionPenalty(logits, generatedTokens, repeatPenalty);
        }
        
        // Yield periodically
        if (step % 10 === 0) {
            await yieldToMain();
        }
    }
    
    const text = detokenize(generatedTokens);
    
    return {
        text: text,
        tokens: generatedTokens.length
    };
}

function applyTemperature(logits, temperature) {
    const invTemp = 1.0 / temperature;
    for (let i = 0; i < logits.length; i++) {
        logits[i] = logits[i] * invTemp;
    }
}

function topPSample(logits, topP) {
    // Create array of (index, probability) pairs
    const probs = softmax(logits);
    const indexed = probs.map((p, i) => ({ idx: i, prob: p }));
    
    // Sort by probability descending
    indexed.sort((a, b) => b.prob - a.prob);
    
    // Compute cumulative sum
    let cumulative = 0;
    const selected = [];
    for (const item of indexed) {
        cumulative += item.prob;
        selected.push(item);
        if (cumulative >= topP) break;
    }
    
    // Sample from selected
    const sum = selected.reduce((s, item) => s + item.prob, 0);
    let target = Math.random() * sum;
    for (const item of selected) {
        target -= item.prob;
        if (target <= 0) return item.idx;
    }
    
    return selected[0].idx;
}

function applyRepetitionPenalty(logits, tokens, penalty) {
    const seen = new Set(tokens);
    for (const token of seen) {
        if (logits[token] < 0) {
            logits[token] *= penalty;
        } else {
            logits[token] /= penalty;
        }
    }
}

function softmax(arr) {
    const max = Math.max(...arr);
    const exp = arr.map(x => Math.exp(x - max));
    const sum = exp.reduce((a, b) => a + b, 0);
    return exp.map(x => x / sum);
}

function detokenize(tokens) {
    // Simplified detokenization (production uses actual tokenizer)
    return tokens.map(t => String.fromCharCode(65 + (t % 26))).join('');
}

function tokenize(text) {
    // Simplified tokenization (production uses actual tokenizer)
    return text.split('').map(c => c.charCodeAt(0) % 1000);
}

async function yieldToMain() {
    return new Promise(resolve => setTimeout(resolve, 0));
}

function processQueue() {
    while (inferenceQueue.length > 0 && isReady) {
        const request = inferenceQueue.shift();
        handleGenerate(request);
    }
}

async function handleBatchGenerate(data) {
    const { prompts, batchId } = data;
    
    const results = [];
    for (const prompt of prompts) {
        const result = await handleGenerate({ ...prompt, id: `${batchId}_${Date.now()}` });
        results.push(result);
    }
    
    ctx.postMessage({
        type: 'batch_complete',
        data: { batchId, results }
    });
}

function sendStatus() {
    ctx.postMessage({
        type: 'status',
        data: {
            ready: isReady,
            queueLength: inferenceQueue.length,
            memoryUsed: wasmMemory ? wasmMemory.buffer.byteLength : 0
        }
    });
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

ctx.addEventListener('error', (event) => {
    ctx.postMessage({ type: 'error', data: event.message });
});

// Signal ready
ctx.postMessage({ type: 'worker_ready' });
