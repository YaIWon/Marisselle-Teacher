// ============================================================================
// FILE: marisselle-teacher/ollama-worker.js
// WEB WORKER FOR OLLAMA WASM - Isolated inference thread
// UPDATED: Wrappers match WASM exports (_ollama_* functions)
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
                ollama_console_log: (ptr, len) => {
                    const bytes = new Uint8Array(wasmMemory.buffer, ptr, len);
                    const text = new TextDecoder().decode(bytes);
                    ctx.postMessage({ type: 'log', data: text });
                },
                ollama_get_timestamp: () => performance.now(),
                thread_count: config.threads || 4
            }
        });
        
        const rawModule = module.instance.exports;
        
        // Create wrapper functions that match the worker's expected names
        // Maps worker calls (alloc) to WASM exports (_ollama_alloc)
        wasmModule = {
            alloc: (size) => rawModule._ollama_alloc(size),
            free: (ptr) => rawModule._ollama_free(ptr),
            heap: {
                set: (buffer, ptr) => {
                    const heap = new Uint8Array(wasmMemory.buffer);
                    heap.set(buffer, ptr);
                }
            },
            load_model: (ptr, len) => rawModule._ollama_load_model(ptr, len),
            is_model_loaded: () => rawModule._ollama_is_model_loaded(),
            get_model_info: () => {
                const ptr = rawModule._ollama_get_model_info();
                if (!ptr) return "{}";
                return rawModule.UTF8ToString ? rawModule.UTF8ToString(ptr) : "{}";
            },
            alloc_kv_cache: (len) => rawModule._ollama_alloc_kv_cache(len),
            free_kv_cache: (ptr) => rawModule._ollama_free_kv_cache(ptr),
            clear_kv_cache: (ptr) => rawModule._ollama_clear_kv_cache(ptr),
            eval_token: (token, kv) => rawModule._ollama_eval_token(token, kv),
            forward: (token, kv) => rawModule._ollama_forward(token, kv),
            get_logits: () => rawModule._ollama_get_logits(),
            get_logits_size: () => rawModule._ollama_get_logits_size(),
            apply_temperature: (logits, size, temp) => rawModule._ollama_apply_temperature(logits, size, temp),
            apply_repetition_penalty: (logits, size, tokens, num, penalty) => rawModule._ollama_apply_repetition_penalty(logits, size, tokens, num, penalty),
            sample_top_p: (logits, size, top_p) => rawModule._ollama_sample_top_p(logits, size, top_p),
            sample_top_k: (logits, size, k) => rawModule._ollama_sample_top_k(logits, size, k),
            sample_greedy: (logits, size) => rawModule._ollama_sample_greedy(logits, size),
            tokenize: (text, outSize) => rawModule._ollama_tokenize(text, outSize),
            detokenize: (tokens, num) => rawModule._ollama_detokenize(tokens, num),
            free_tokens: (ptr) => rawModule._ollama_free_tokens(ptr),
            free_string: (ptr) => rawModule._ollama_free_string(ptr),
            get_timestamp: () => rawModule._ollama_get_timestamp(),
            get_thread_count: () => rawModule._ollama_get_thread_count(),
            UTF8ToString: (ptr) => {
                if (rawModule.UTF8ToString) return rawModule.UTF8ToString(ptr);
                // Fallback manual decode
                const heap = new Uint8Array(wasmMemory.buffer);
                let len = 0;
                while (heap[ptr + len] !== 0) len++;
                return new TextDecoder().decode(heap.slice(ptr, ptr + len));
            }
        };
        
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
        const result = wasmModule.load_model(modelPtr, modelBuffer.length);
        wasmModule.free(modelPtr);
        
        if (result === 0) {
            isReady = true;
            ctx.postMessage({ type: 'model_loaded', data: { 
                size: modelBuffer.length,
                ready: true,
                info: wasmModule.get_model_info()
            }});
        } else {
            ctx.postMessage({ type: 'error', data: `Model load failed with code ${result}` });
            return;
        }
        
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
        let inputTokens = tokens;
        if (!inputTokens && prompt) {
            const outSizePtr = wasmModule.alloc(4);
            const tokenPtr = wasmModule.tokenize(prompt, outSizePtr);
            const outSize = new Int32Array(wasmMemory.buffer, outSizePtr, 1)[0];
            inputTokens = new Int32Array(wasmMemory.buffer, tokenPtr, outSize);
            inputTokens = Array.from(inputTokens);
            wasmModule.free(outSizePtr);
            wasmModule.free_tokens(tokenPtr);
        }
        
        if (!inputTokens || inputTokens.length === 0) {
            throw new Error('Tokenization failed');
        }
        
        // Prepare KV cache
        const kvCachePtr = wasmModule.alloc_kv_cache(MAX_SEQUENCE_LEN);
        
        // Run inference
        const result = await runInference(
            inputTokens, 
            temperature || 0.7, 
            topP || 0.9, 
            repeatPenalty || 1.1, 
            maxTokens || 2048,
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
    let lastTokensBuffer = null;
    
    // Prefill - eval all input tokens except last
    for (let i = 0; i < tokens.length - 1; i++) {
        wasmModule.eval_token(tokens[i], kvCachePtr);
    }
    
    let currentToken = tokens[tokens.length - 1];
    let generatedCount = 0;
    
    for (let step = 0; step < maxTokens && !isDone && generatedCount < maxTokens; step++) {
        // Forward pass
        const logitsPtr = wasmModule.forward(currentToken, kvCachePtr);
        const logitsSize = wasmModule.get_logits_size();
        
        // Copy logits to JS for sampling
        const logits = new Float32Array(wasmMemory.buffer, logitsPtr, logitsSize);
        
        // Apply temperature
        if (temperature > 0 && temperature !== 1.0) {
            wasmModule.apply_temperature(logitsPtr, logitsSize, temperature);
        }
        
        // Apply repetition penalty
        if (repeatPenalty > 1.0 && generatedTokens.length > 0) {
            const penaltyBuffer = wasmModule.alloc(generatedTokens.length * 4);
            const penaltyView = new Int32Array(wasmMemory.buffer, penaltyBuffer, generatedTokens.length);
            for (let i = 0; i < generatedTokens.length; i++) {
                penaltyView[i] = generatedTokens[i];
            }
            wasmModule.apply_repetition_penalty(logitsPtr, logitsSize, penaltyBuffer, generatedTokens.length, repeatPenalty);
            wasmModule.free(penaltyBuffer);
        }
        
        // Sample next token
        let nextToken;
        if (topP > 0 && topP < 1.0) {
            nextToken = wasmModule.sample_top_p(logitsPtr, logitsSize, topP);
        } else if (temperature > 0) {
            nextToken = wasmModule.sample_top_k(logitsPtr, logitsSize, 50);
        } else {
            nextToken = wasmModule.sample_greedy(logitsPtr, logitsSize);
        }
        
        // Check for EOS (token id 2 is EOS in Phi-3)
        if (nextToken === 2 || nextToken === 0) {
            isDone = true;
            break;
        }
        
        generatedTokens.push(nextToken);
        currentToken = nextToken;
        generatedCount++;
        
        // Yield occasionally to prevent blocking
        if (step % 10 === 0) {
            await yieldToMain();
        }
    }
    
    // Detokenize result
    if (generatedTokens.length > 0) {
        const tokenBuffer = wasmModule.alloc(generatedTokens.length * 4);
        const tokenView = new Int32Array(wasmMemory.buffer, tokenBuffer, generatedTokens.length);
        for (let i = 0; i < generatedTokens.length; i++) {
            tokenView[i] = generatedTokens[i];
        }
        const textPtr = wasmModule.detokenize(tokenBuffer, generatedTokens.length);
        let text = "";
        if (textPtr) {
            text = wasmModule.UTF8ToString(textPtr);
            wasmModule.free_string(textPtr);
        }
        wasmModule.free(tokenBuffer);
        
        return { text: text, tokens: generatedTokens.length };
    }
    
    return { text: "", tokens: 0 };
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
        const resultPromise = new Promise((resolve) => {
            const handler = (event) => {
                if (event.data.type === 'generation_complete' && event.data.data.id === `${batchId}_${Date.now()}`) {
                    ctx.removeEventListener('message', handler);
                    resolve(event.data.data);
                }
            };
            ctx.addEventListener('message', handler);
        });
        handleGenerate({ ...prompt, id: `${batchId}_${Date.now()}` });
        results.push(resultPromise);
    }
    
    const completed = await Promise.all(results);
    
    ctx.postMessage({
        type: 'batch_complete',
        data: { batchId, results: completed }
    });
}

function sendStatus() {
    ctx.postMessage({
        type: 'status',
        data: {
            ready: isReady,
            queueLength: inferenceQueue.length,
            memoryUsed: wasmMemory ? wasmMemory.buffer.byteLength : 0,
            modelLoaded: wasmModule ? wasmModule.is_model_loaded() : false
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
