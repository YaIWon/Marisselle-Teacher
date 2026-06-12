// api/teacher/status.js
const status = {
    online: true,
    model: "llama-3.3-70b-versatile",
    version: "3.1.0",
    uptime_seconds: Math.floor((Date.now() - (window.performance?.timing?.navigationStart || Date.now())) / 1000),
    current_lesson: parseInt(localStorage.getItem('current_topic_index') || '0'),
    total_lessons: 14,
    is_generating: window.isGeneratingLesson || false,
    has_pending_elder: window.pendingElderMessage !== null,
    metrics: {
        totalTokens: window.totalTokensGenerated || 0,
        lessonsTaught: window.lessonsTaught || 0,
        activeLessons: window.activeLessons?.size || 0,
        syncCount: window.syncCount || 0
    },
    timestamp: Date.now()
};

// Output as JSON
document.write(JSON.stringify(status, null, 2));
