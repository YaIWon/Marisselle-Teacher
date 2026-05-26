# ============================================================================
# FILE: CMakeLists.txt
# PATH: /marisselle-teacher/CMakeLists.txt
# PURPOSE: Build Ollama WASM module for Marisselle Teacher
#          Exports functions expected by ollama-worker.js
#          NOW INCLUDES ALL SOURCE FILES
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(ollama_teacher VERSION 1.0.0 LANGUAGES CXX C)

# ============================================================================
# COMPILER AND LINKER FLAGS
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emscripten-specific configuration
if(EMSCRIPTEN)
    # Compiler flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s WASM=1 -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_MEMORY=2GB")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s EXPORTED_FUNCTIONS=['_malloc','_free','_ollama_alloc','_ollama_free','_ollama_get_heap','_ollama_load_model','_ollama_is_model_loaded','_ollama_get_model_info','_ollama_alloc_kv_cache','_ollama_free_kv_cache','_ollama_clear_kv_cache','_ollama_eval_token','_ollama_forward','_ollama_get_logits','_ollama_get_logits_size','_ollama_apply_temperature','_ollama_apply_repetition_penalty','_ollama_sample_top_p','_ollama_sample_top_k','_ollama_sample_greedy','_ollama_tokenize','_ollama_detokenize','_ollama_free_tokens','_ollama_free_string','_ollama_get_timestamp','_ollama_console_log','_ollama_get_thread_count']")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','setValue','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8','HEAP32','HEAPF32']")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s MODULARIZE=1 -s EXPORT_NAME='createOllamaModule'")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ENVIRONMENT='web,worker'")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -msimd128 -msse2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s ASSERTIONS=2 -s DEMANGLE_SUPPORT=1")
    
    # Linker flags
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s WASM=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s ALLOW_MEMORY_GROWTH=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s TOTAL_MEMORY=2GB")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s USE_PTHREADS=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s PTHREAD_POOL_SIZE=4")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s INITIAL_MEMORY=256MB")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s MAXIMUM_MEMORY=4GB")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s STACK_SIZE=5MB")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s FORCE_FILESYSTEM=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s FILESYSTEM=1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --bind")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s DISABLE_EXCEPTION_CATCHING=0")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s ERROR_ON_UNDEFINED_SYMBOLS=0")
endif()

# ============================================================================
# SOURCE FILES - ALL IMPLEMENTATIONS
# ============================================================================

set(SOURCES
    src/ollama_bridge.cpp
    src/gguf_parser.cpp
    src/kv_cache.cpp
    src/model_loader.cpp
    src/tokenizer.cpp
    src/sampler.cpp
    src/inference_engine.cpp
)

set(HEADERS
    src/ollama_bridge.h
    src/gguf_parser.h
    src/kv_cache.h
    src/model_loader.h
    src/tokenizer.h
    src/sampler.h
    src/inference_engine.h
)

# ============================================================================
# EXECUTABLE
# ============================================================================

add_executable(ollama.wasm ${SOURCES} ${HEADERS})

# ============================================================================
# TARGET PROPERTIES
# ============================================================================

set_target_properties(ollama.wasm PROPERTIES
    OUTPUT_NAME "ollama"
    SUFFIX ".wasm"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================

target_include_directories(ollama.wasm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ============================================================================
# COMPILE DEFINITIONS
# ============================================================================

target_compile_definitions(ollama.wasm PRIVATE
    USE_PTHREADS=1
    ALLOW_MEMORY_GROWTH=1
    TOTAL_MEMORY=2147483648
    MODEL_MAX_SEQ_LEN=4096
    MODEL_VOCAB_SIZE=32064
    MODEL_HIDDEN_SIZE=3072
    MODEL_INTERMEDIATE_SIZE=8192
    MODEL_NUM_LAYERS=32
    MODEL_NUM_HEADS=32
    MODEL_NUM_KV_HEADS=8
)

# ============================================================================
# CUSTOM COMMANDS
# ============================================================================

# Generate JS wrapper and copy to root
add_custom_command(TARGET ollama.wasm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/ollama.wasm
        ${CMAKE_SOURCE_DIR}/ollama.wasm
    COMMENT "Copying ollama.wasm to root directory"
)
