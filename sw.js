// ============================================================================
// FILE: marisselle-teacher/sw.js
// SERVICE WORKER - Offline support & API routing
// ============================================================================
const CACHE_NAME = 'marisselle-teacher-v2';
const MODEL_CACHE_NAME = 'phi3-model-v1';

const STATIC_URLS = [
    '/',
    '/index.html',
    '/teacher.js',
    '/ollama-worker.js',
    '/keepalive.js',
    '/offline.html'
];

const MODEL_URLS = [
    '/phi3-model/config.json',
    '/phi3-model/tokenizer.json'
];

self.addEventListener('install', (event) => {
    event.waitUntil(
        Promise.all([
            caches.open(CACHE_NAME).then(cache => cache.addAll(STATIC_URLS)),
            caches.open(MODEL_CACHE_NAME).then(cache => cache.addAll(MODEL_URLS))
        ])
    );
    self.skipWaiting();
});

self.addEventListener('fetch', (event) => {
    const url = new URL(event.request.url);
    
    // API routes - network first
    if (url.pathname.startsWith('/api/')) {
        event.respondWith(
            fetch(event.request).catch(() => {
                return new Response(JSON.stringify({
                    error: 'Teacher offline',
                    cached: false,
                    timestamp: Date.now()
                }), {
                    headers: { 'Content-Type': 'application/json' }
                });
            })
        );
        return;
    }
    
    // Model files - cache first, then network
    if (url.pathname.includes('/phi3-model/')) {
        event.respondWith(
            caches.open(MODEL_CACHE_NAME).then(cache => {
                return cache.match(event.request).then(cached => {
                    if (cached) return cached;
                    return fetch(event.request).then(response => {
                        cache.put(event.request, response.clone());
                        return response;
                    });
                });
            })
        );
        return;
    }
    
    // Static assets - cache first
    event.respondWith(
        caches.match(event.request).then(cached => {
            return cached || fetch(event.request);
        })
    );
});

self.addEventListener('activate', (event) => {
    event.waitUntil(
        caches.keys().then(keys => {
            return Promise.all(
                keys.filter(key => key !== CACHE_NAME && key !== MODEL_CACHE_NAME)
                    .map(key => caches.delete(key))
            );
        })
    );
    self.clients.claim();
});

// Background sync for offline requests
self.addEventListener('sync', (event) => {
    if (event.tag === 'teacher-sync') {
        event.waitUntil(syncQueuedRequests());
    }
});

async function syncQueuedRequests() {
    const cache = await caches.open('request-queue');
    const requests = await cache.keys();
    
    for (const request of requests) {
        try {
            const response = await fetch(request);
            if (response.ok) {
                await cache.delete(request);
            }
        } catch (e) {
            console.error('Sync failed:', e);
        }
    }
}
