// ============================================================================
// FILE: marisselle-teacher/keepalive.js
// BACKGROUND KEEPALIVE - Prevents Pages from sleeping
// ============================================================================

// Register background sync
if ('serviceWorker' in navigator && 'SyncManager' in window) {
    navigator.serviceWorker.ready.then(reg => {
        reg.sync.register('teacher-keepalive');
    });
}

// Periodic health checks
const HEALTH_CONFIG = {
    interval: 240000,  // 4 minutes
    endpoints: ['/api/teacher/status', '/api/teacher/ping'],
    maxRetries: 3
};

let healthStatus = {
    lastCheck: null,
    uptime: 0,
    requestsServed: 0,
    errors: 0
};

async function checkHealth() {
    for (const endpoint of HEALTH_CONFIG.endpoints) {
        for (let attempt = 0; attempt < HEALTH_CONFIG.maxRetries; attempt++) {
            try {
                const response = await fetch(endpoint, {
                    headers: { 'X-Health-Check': 'true' }
                });
                
                if (response.ok) {
                    healthStatus.lastCheck = Date.now();
                    healthStatus.requestsServed++;
                    console.log(`[HEALTH] ${endpoint} OK`);
                    break;
                }
            } catch (e) {
                console.warn(`[HEALTH] ${endpoint} attempt ${attempt + 1} failed`);
                if (attempt === HEALTH_CONFIG.maxRetries - 1) {
                    healthStatus.errors++;
                }
            }
        }
    }
    
    // Report to Core
    await reportHealthToCore();
}

async function reportHealthToCore() {
    try {
        await fetch('https://yaiwon.github.io/Core/api/teacher/heartbeat', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                teacher_url: window.location.origin,
                status: healthStatus,
                timestamp: Date.now()
            })
        });
    } catch (e) {
        console.error('[HEALTH] Failed to report to Core:', e);
    }
}

// Start health checks
setInterval(checkHealth, HEALTH_CONFIG.interval);
checkHealth();

// Track uptime
const startTime = Date.now();
setInterval(() => {
    healthStatus.uptime = (Date.now() - startTime) / 1000;
}, 60000);

// Export for debugging
window.__teacherHealth = healthStatus;
