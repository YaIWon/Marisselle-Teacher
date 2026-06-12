// ============================================================================
// FILE: marisselle-teacher/keepalive.js
// BACKGROUND KEEPALIVE - Prevents Pages from sleeping
// UPDATED: Works with physical status.json endpoint
// ============================================================================

// Register background sync (only if service worker exists)
if ('serviceWorker' in navigator && 'SyncManager' in window) {
    navigator.serviceWorker.ready.then(reg => {
        try {
            reg.sync.register('teacher-keepalive');
            console.log('[KEEPALIVE] Background sync registered');
        } catch (e) {
            console.log('[KEEPALIVE] Background sync not supported:', e.message);
        }
    }).catch(e => {
        console.log('[KEEPALIVE] Service worker not ready:', e.message);
    });
}

// Periodic health checks
const HEALTH_CONFIG = {
    interval: 240000,  // 4 minutes
    endpoints: {
        status: '/api/teacher/status',
        ping: '/api/teacher/ping',
        health: '/health.json'
    },
    maxRetries: 3,
    retryDelay: 2000  // 2 seconds between retries
};

let healthStatus = {
    lastCheck: null,
    uptime: 0,
    requestsServed: 0,
    errors: 0,
    lastError: null,
    statusEndpointWorking: false,
    pingEndpointWorking: false
};

async function checkHealth() {
    console.log('[HEALTH] Starting health check cycle...');
    
    // Check status endpoint (now a physical JSON file)
    const statusWorking = await checkEndpoint(HEALTH_CONFIG.endpoints.status, 'STATUS');
    healthStatus.statusEndpointWorking = statusWorking;
    
    // Check ping endpoint (handled by teacher.js)
    const pingWorking = await checkEndpoint(HEALTH_CONFIG.endpoints.ping, 'PING');
    healthStatus.pingEndpointWorking = pingWorking;
    
    // Check health.json endpoint
    await checkEndpoint(HEALTH_CONFIG.endpoints.health, 'HEALTH');
    
    // Update last check time
    healthStatus.lastCheck = Date.now();
    
    // Report to Core
    await reportHealthToCore();
    
    // Log summary
    if (statusWorking && pingWorking) {
        console.log(`[HEALTH] ✅ All endpoints healthy (uptime: ${Math.floor(healthStatus.uptime)}s)`);
    } else {
        console.warn(`[HEALTH] ⚠️ Status: ${statusWorking ? 'OK' : 'FAIL'}, Ping: ${pingWorking ? 'OK' : 'FAIL'}`);
    }
}

async function checkEndpoint(endpoint, name) {
    for (let attempt = 0; attempt < HEALTH_CONFIG.maxRetries; attempt++) {
        try {
            const controller = new AbortController();
            const timeoutId = setTimeout(() => controller.abort(), 10000);
            
            const response = await fetch(endpoint, {
                method: 'GET',
                headers: { 
                    'Cache-Control': 'no-cache, no-store',
                    'Pragma': 'no-cache'
                },
                signal: controller.signal
            });
            
            clearTimeout(timeoutId);
            
            if (response.ok) {
                healthStatus.requestsServed++;
                console.log(`[HEALTH] ✅ ${name} endpoint OK (${response.status})`);
                
                // If this is the status endpoint, try to parse the JSON
                if (endpoint === HEALTH_CONFIG.endpoints.status) {
                    try {
                        const data = await response.json();
                        console.log(`[HEALTH] Status data: model=${data.model}, version=${data.version}`);
                    } catch (e) {
                        // Not JSON or empty response
                    }
                }
                
                return true;
            } else {
                console.warn(`[HEALTH] ⚠️ ${name} endpoint returned ${response.status}`);
                if (attempt === HEALTH_CONFIG.maxRetries - 1) {
                    healthStatus.errors++;
                    healthStatus.lastError = `${name} HTTP ${response.status}`;
                }
            }
        } catch (e) {
            console.warn(`[HEALTH] ❌ ${name} endpoint attempt ${attempt + 1} failed: ${e.message}`);
            if (attempt === HEALTH_CONFIG.maxRetries - 1) {
                healthStatus.errors++;
                healthStatus.lastError = `${name}: ${e.message}`;
            }
            if (attempt < HEALTH_CONFIG.maxRetries - 1) {
                await new Promise(r => setTimeout(r, HEALTH_CONFIG.retryDelay));
            }
        }
    }
    return false;
}

async function reportHealthToCore() {
    try {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 5000);
        
        const response = await fetch('https://yaiwon.github.io/Core/api/teacher/heartbeat', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                teacher_url: window.location.origin,
                status: {
                    online: healthStatus.statusEndpointWorking || healthStatus.pingEndpointWorking,
                    lastCheck: healthStatus.lastCheck,
                    uptime: healthStatus.uptime,
                    requestsServed: healthStatus.requestsServed,
                    errors: healthStatus.errors,
                    endpoints: {
                        status: healthStatus.statusEndpointWorking,
                        ping: healthStatus.pingEndpointWorking
                    }
                },
                timestamp: Date.now()
            }),
            signal: controller.signal
        });
        
        clearTimeout(timeoutId);
        
        if (response.ok) {
            console.log(`[HEALTH] Core heartbeat reported (uptime: ${Math.floor(healthStatus.uptime)}s)`);
        } else {
            console.warn(`[HEALTH] Core heartbeat returned ${response.status}`);
        }
    } catch (e) {
        if (e.name === 'AbortError') {
            console.log('[HEALTH] Core heartbeat timed out');
        } else {
            console.log('[HEALTH] Could not report to Core:', e.message);
        }
    }
}

// Update uptime every minute
const startTime = Date.now();
setInterval(() => {
    healthStatus.uptime = (Date.now() - startTime) / 1000;
}, 60000);

// Start health checks after a delay (let the page load first)
setTimeout(() => {
    console.log('[KEEPALIVE] Starting health checks...');
    checkHealth();
    setInterval(checkHealth, HEALTH_CONFIG.interval);
}, 5000);

// Also run health check when page becomes visible
document.addEventListener('visibilitychange', () => {
    if (!document.hidden) {
        console.log('[KEEPALIVE] Page visible - checking health immediately');
        checkHealth();
    }
});

// Run health check when coming back online
window.addEventListener('online', () => {
    console.log('[KEEPALIVE] Browser online - checking health');
    checkHealth();
});

// Export for debugging
window.__teacherHealth = healthStatus;
