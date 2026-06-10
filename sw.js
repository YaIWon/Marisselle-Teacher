// ============================================================================
// FILE: marisselle-teacher/sw.js
// SERVICE WORKER - Offline support & API routing
// ADVANCED VERSION - Full compatibility with teacher.js API routes
// ============================================================================

const CACHE_NAME = 'marisselle-teacher-v3';
const MODEL_CACHE_NAME = 'phi3-model-v1';
const API_CACHE_NAME = 'teacher-api-v1';
const STATIC_CACHE_NAME = 'teacher-static-v1';

const STATIC_URLS = [
    '/',
    '/index.html',
    '/teacher.js',
    '/ollama-worker.js',
    '/keepalive.js',
    '/offline.html',
    '/sw.js',
    '/_headers',
    '/_redirects',
    '/.nojekyll'
];

const MODEL_URLS = [
    '/phi3-model/config.json',
    '/phi3-model/tokenizer.json'
];

// Advanced: API endpoints that should be cached
const API_CACHE_ENDPOINTS = [
    '/api/teacher/status',
    '/api/teacher/ping'
];

// ============================================================================
// INSTALL EVENT - Cache all static assets
// ============================================================================

self.addEventListener('install', (event) => {
    console.log('[SW] Installing new version');
    
    event.waitUntil(
        Promise.all([
            caches.open(CACHE_NAME).then(cache => {
                console.log('[SW] Caching static assets');
                return cache.addAll(STATIC_URLS);
            }),
            caches.open(MODEL_CACHE_NAME).then(cache => {
                console.log('[SW] Caching model files');
                return cache.addAll(MODEL_URLS);
            }),
            caches.open(API_CACHE_NAME).then(cache => {
                console.log('[SW] Creating API cache');
                return cache;
            })
        ])
    );
    
    // Force activate immediately
    self.skipWaiting();
});

// ============================================================================
// FETCH EVENT - Route requests to appropriate handler
// ============================================================================

self.addEventListener('fetch', (event) => {
    const url = new URL(event.request.url);
    const request = event.request;
    
    // ========================================================================
    // HEALTH CHECK REQUESTS - Always network first, never cache
    // ========================================================================
    if (request.headers.has('X-Health-Check')) {
        event.respondWith(
            fetch(request).catch(() => {
                return new Response(JSON.stringify({
                    error: 'Teacher offline',
                    timestamp: Date.now()
                }), {
                    status: 503,
                    headers: { 'Content-Type': 'application/json' }
                });
            })
        );
        return;
    }
    
    // ========================================================================
    // API ROUTES - Network first, fallback to offline response
    // ========================================================================
    if (url.pathname.startsWith('/api/')) {
        event.respondWith(handleApiRequest(request, url));
        return;
    }
    
    // ========================================================================
    // MODEL FILES - Cache first, then network (large files)
    // ========================================================================
    if (url.pathname.includes('/phi3-model/') || url.pathname.endsWith('.gguf') || url.pathname.endsWith('.wasm')) {
        event.respondWith(handleModelRequest(request));
        return;
    }
    
    // ========================================================================
    // STATIC ASSETS - Cache first, then network
    // ========================================================================
    if (isStaticAsset(url.pathname)) {
        event.respondWith(handleStaticRequest(request));
        return;
    }
    
    // ========================================================================
    // DEFAULT - Network first with cache fallback
    // ========================================================================
    event.respondWith(
        fetch(request).catch(async () => {
            const cachedResponse = await caches.match(request);
            if (cachedResponse) {
                return cachedResponse;
            }
            return caches.match('/offline.html');
        })
    );
});

// ============================================================================
// API REQUEST HANDLER
// ============================================================================

async function handleApiRequest(request, url) {
    // Don't cache POST requests
    if (request.method !== 'GET') {
        try {
            const response = await fetch(request.clone());
            return response;
        } catch (error) {
            return new Response(JSON.stringify({
                error: 'Teacher offline - POST failed',
                method: request.method,
                endpoint: url.pathname,
                timestamp: Date.now()
            }), {
                status: 503,
                headers: { 'Content-Type': 'application/json' }
            });
        }
    }
    
    // GET requests - try network first, fallback to cache
    try {
        const networkResponse = await fetch(request.clone());
        
        // Cache successful GET responses for API endpoints
        if (networkResponse.ok && API_CACHE_ENDPOINTS.includes(url.pathname)) {
            const cache = await caches.open(API_CACHE_NAME);
            cache.put(request, networkResponse.clone());
        }
        
        return networkResponse;
    } catch (error) {
        // Network failed - try cache
        const cachedResponse = await caches.match(request);
        if (cachedResponse) {
            return cachedResponse;
        }
        
        // Return offline status
        return new Response(JSON.stringify({
            online: false,
            error: 'Teacher offline',
            cached: false,
            endpoint: url.pathname,
            timestamp: Date.now()
        }), {
            status: 503,
            headers: { 'Content-Type': 'application/json' }
        });
    }
}

// ============================================================================
// MODEL REQUEST HANDLER - Cache first for large files
// ============================================================================

async function handleModelRequest(request) {
    const cache = await caches.open(MODEL_CACHE_NAME);
    const cachedResponse = await cache.match(request);
    
    if (cachedResponse) {
        console.log('[SW] Model cache hit:', request.url);
        return cachedResponse;
    }
    
    try {
        const networkResponse = await fetch(request.clone());
        if (networkResponse.ok) {
            cache.put(request, networkResponse.clone());
        }
        return networkResponse;
    } catch (error) {
        console.error('[SW] Model fetch failed:', error);
        return new Response('Model file unavailable', { status: 404 });
    }
}

// ============================================================================
// STATIC REQUEST HANDLER - Cache first
// ============================================================================

async function handleStaticRequest(request) {
    const cachedResponse = await caches.match(request);
    if (cachedResponse) {
        return cachedResponse;
    }
    
    try {
        const networkResponse = await fetch(request.clone());
        if (networkResponse.ok) {
            const cache = await caches.open(CACHE_NAME);
            cache.put(request, networkResponse.clone());
        }
        return networkResponse;
    } catch (error) {
        return caches.match('/offline.html');
    }
}

// ============================================================================
// UTILITY - Check if path is static asset
// ============================================================================

function isStaticAsset(pathname) {
    const staticExtensions = [
        '.js', '.css', '.html', '.json', '.png', '.jpg', '.jpeg', 
        '.gif', '.svg', '.ico', '.webp', '.txt', '.md'
    ];
    return staticExtensions.some(ext => pathname.endsWith(ext));
}

// ============================================================================
// ACTIVATE EVENT - Clean up old caches
// ============================================================================

self.addEventListener('activate', (event) => {
    console.log('[SW] Activating new version');
    
    event.waitUntil(
        caches.keys().then(keys => {
            const validCaches = [CACHE_NAME, MODEL_CACHE_NAME, API_CACHE_NAME, STATIC_CACHE_NAME];
            return Promise.all(
                keys.filter(key => !validCaches.includes(key))
                    .map(key => {
                        console.log('[SW] Deleting old cache:', key);
                        return caches.delete(key);
                    })
            );
        })
    );
    
    // Take control of all clients immediately
    self.clients.claim();
});

// ============================================================================
// BACKGROUND SYNC - For offline requests
// ============================================================================

self.addEventListener('sync', (event) => {
    console.log('[SW] Sync event:', event.tag);
    
    if (event.tag === 'teacher-sync') {
        event.waitUntil(syncQueuedRequests());
    }
    
    if (event.tag === 'teacher-keepalive') {
        event.waitUntil(handleKeepaliveSync());
    }
});

async function syncQueuedRequests() {
    console.log('[SYNC] Processing queued requests');
    const cache = await caches.open('request-queue');
    const requests = await cache.keys();
    
    for (const request of requests) {
        try {
            const response = await fetch(request);
            if (response.ok) {
                await cache.delete(request);
                console.log('[SYNC] Successfully synced:', request.url);
            }
        } catch (e) {
            console.error('[SYNC] Failed:', e);
        }
    }
}

async function handleKeepaliveSync() {
    console.log('[SYNC] Keepalive sync triggered');
    try {
        await fetch('/api/teacher/ping');
    } catch (e) {
        console.error('[SYNC] Keepalive failed:', e);
    }
}

// ============================================================================
// PUSH NOTIFICATIONS (optional - for future use)
// ============================================================================

self.addEventListener('push', (event) => {
    const data = event.data ? event.data.json() : {};
    
    const options = {
        body: data.body || 'Teacher has a new lesson for Marisselle',
        icon: '/favicon.ico',
        badge: '/badge.png',
        vibrate: [100, 50, 100],
        data: {
            url: data.url || '/',
            lessonId: data.lessonId
        }
    };
    
    event.waitUntil(
        self.registration.showNotification('Marisselle Teacher', options)
    );
});

self.addEventListener('notificationclick', (event) => {
    event.notification.close();
    
    const urlToOpen = event.notification.data?.url || '/';
    
    event.waitUntil(
        clients.matchAll({ type: 'window', includeUncontrolled: true })
            .then(windowClients => {
                for (const client of windowClients) {
                    if (client.url === urlToOpen && 'focus' in client) {
                        return client.focus();
                    }
                }
                if (clients.openWindow) {
                    return clients.openWindow(urlToOpen);
                }
            })
    );
});

// ============================================================================
// MESSAGE HANDLING - For communication with teacher.js
// ============================================================================

self.addEventListener('message', (event) => {
    const data = event.data;
    
    if (data.type === 'SKIP_WAITING') {
        self.skipWaiting();
    }
    
    if (data.type === 'CLEAR_CACHE') {
        event.waitUntil(clearAllCaches());
    }
    
    if (data.type === 'GET_CACHE_STATUS') {
        event.source.postMessage({ type: 'CACHE_STATUS', status: 'ready' });
    }
});

async function clearAllCaches() {
    const keys = await caches.keys();
    await Promise.all(keys.map(key => caches.delete(key)));
    console.log('[SW] All caches cleared');
}

// ============================================================================
// PERIODIC BACKGROUND SYNC (if supported)
// ============================================================================

if ('periodicSync' in self.registration) {
    self.addEventListener('periodicsync', (event) => {
        if (event.tag === 'teacher-health-check') {
            event.waitUntil(performHealthCheck());
        }
    });
}

async function performHealthCheck() {
    try {
        const response = await fetch('/api/teacher/status');
        const data = await response.json();
        console.log('[PERIODIC] Health check:', data);
    } catch (e) {
        console.error('[PERIODIC] Health check failed:', e);
    }
}

// ============================================================================
// EXPORT FOR DEBUGGING (in worker context)
// ============================================================================

console.log('[SW] Service Worker loaded and ready');
