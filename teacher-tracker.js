// ============================================================================
// FILE: teacher-tracker.js
// PURPOSE: Ensures Teacher stays on curriculum track
// VERSION: 2.0.0 - Full curriculum tracking with persistence
// ============================================================================

class CurriculumTracker {
    constructor() {
        this.curriculum = null;
        this.tracker = null;
        this.interval = null;
        this.autoAdvanceEnabled = true;
        this.retryFailedTopics = true;
        this.maxRetries = 3;
    }
    
    // ========================================================================
    // INITIALIZATION
    // ========================================================================
    
    async initialize() {
        console.log('[TRACKER] Initializing curriculum tracker...');
        
        // Load curriculum.json
        try {
            const curriculumResponse = await fetch('/curriculum.json');
            if (!curriculumResponse.ok) {
                throw new Error(`HTTP ${curriculumResponse.status}`);
            }
            this.curriculum = await curriculumResponse.json();
            console.log(`[TRACKER] Loaded curriculum: ${this.curriculum.total_topics} topics, ${this.curriculum.total_estimated_lessons} estimated lessons`);
        } catch (error) {
            console.error('[TRACKER] Failed to load curriculum.json:', error);
            return false;
        }
        
        // Load or create tracker
        try {
            const trackerResponse = await fetch('/tracker.json');
            if (trackerResponse.ok) {
                this.tracker = await trackerResponse.json();
                console.log(`[TRACKER] Loaded existing tracker: ${this.tracker.statistics.completed}/${this.tracker.statistics.total_topics} completed`);
            } else {
                this.tracker = this.createNewTracker();
                console.log(`[TRACKER] Created new tracker`);
            }
        } catch (error) {
            console.error('[TRACKER] Failed to load tracker.json:', error);
            this.tracker = this.createNewTracker();
        }
        
        // Validate tracker against curriculum
        this.validateTracker();
        
        console.log(`[TRACKER] Initialized. Progress: ${this.tracker.statistics.percent_complete}%`);
        return true;
    }
    
    createNewTracker() {
        const now = new Date().toISOString();
        const totalTopics = this.getTotalTopics();
        const totalLessons = this.getTotalEstimatedLessons();
        
        return {
            version: "2.0.0",
            last_sync: now,
            current_topic_id: this.curriculum.sections[0]?.topics[0]?.id || null,
            current_section: this.curriculum.sections[0]?.name || null,
            completed_topics: [],
            in_progress_topics: [],
            failed_topics: [],
            skipped_topics: [],
            topic_history: [],
            lm_confirmation_pending: [],
            lm_confirmation_received: [],
            lm_confirmation_failed: [],
            statistics: {
                total_topics: totalTopics,
                completed: 0,
                in_progress: 0,
                failed: 0,
                skipped: 0,
                percent_complete: 0,
                estimated_lessons_completed: 0,
                estimated_lessons_remaining: totalLessons,
                start_date: now,
                estimated_completion_date: null
            }
        };
    }
    
    validateTracker() {
        // Ensure all topics in curriculum have corresponding status
        for (const section of this.curriculum.sections) {
            for (const topic of section.topics) {
                if (!this.tracker.completed_topics.includes(topic.id) &&
                    !this.tracker.in_progress_topics.includes(topic.id) &&
                    !this.tracker.failed_topics.some(f => f.topic_id === topic.id)) {
                    // Topic not tracked yet - add as pending
                    topic.status = 'pending';
                }
            }
        }
        
        // Update statistics
        this.recalculateStatistics();
    }
    
    recalculateStatistics() {
        const total = this.getTotalTopics();
        const completed = this.tracker.completed_topics.length;
        const inProgress = this.tracker.in_progress_topics.length;
        const failed = this.tracker.failed_topics.length;
        
        this.tracker.statistics.total_topics = total;
        this.tracker.statistics.completed = completed;
        this.tracker.statistics.in_progress = inProgress;
        this.tracker.statistics.failed = failed;
        this.tracker.statistics.percent_complete = total > 0 ? (completed / total) * 100 : 0;
        
        // Calculate estimated lessons
        let lessonsCompleted = 0;
        let lessonsRemaining = 0;
        
        for (const section of this.curriculum.sections) {
            for (const topic of section.topics) {
                if (this.tracker.completed_topics.includes(topic.id)) {
                    lessonsCompleted += topic.estimated_lessons;
                } else {
                    lessonsRemaining += topic.estimated_lessons;
                }
            }
        }
        
        this.tracker.statistics.estimated_lessons_completed = lessonsCompleted;
        this.tracker.statistics.estimated_lessons_remaining = lessonsRemaining;
    }
    
    // ========================================================================
    // UTILITY METHODS
    // ========================================================================
    
    getTotalTopics() {
        let total = 0;
        for (const section of this.curriculum.sections) {
            total += section.topics.length;
        }
        return total;
    }
    
    getTotalEstimatedLessons() {
        let total = 0;
        for (const section of this.curriculum.sections) {
            for (const topic of section.topics) {
                total += topic.estimated_lessons;
            }
        }
        return total;
    }
    
    findTopicById(topicId) {
        for (const section of this.curriculum.sections) {
            for (const topic of section.topics) {
                if (topic.id === topicId) {
                    return { ...topic, section: section.name };
                }
            }
        }
        return null;
    }
    
    findSectionForTopic(topicId) {
        for (const section of this.curriculum.sections) {
            for (const topic of section.topics) {
                if (topic.id === topicId) {
                    return section.name;
                }
            }
        }
        return null;
    }
    
    // ========================================================================
    // TOPIC NAVIGATION
    // ========================================================================
    
    getNextTopic() {
        // First, check failed topics that need retry
        if (this.retryFailedTopics) {
            for (const failed of this.tracker.failed_topics) {
                if (failed.retry_count < this.maxRetries) {
                    const topic = this.findTopicById(failed.topic_id);
                    if (topic) {
                        console.log(`[TRACKER] Retrying failed topic: ${topic.name} (attempt ${failed.retry_count + 1}/${this.maxRetries})`);
                        return {
                            section: this.findSectionForTopic(failed.topic_id),
                            topic: topic,
                            topic_id: failed.topic_id,
                            is_retry: true,
                            retry_count: failed.retry_count
                        };
                    }
                }
            }
        }
        
        // Find first pending topic in priority order
        for (const section of this.curriculum.sections) {
            for (const topic of section.topics) {
                // Skip if already completed or in progress
                if (this.tracker.completed_topics.includes(topic.id)) continue;
                if (this.tracker.in_progress_topics.includes(topic.id)) continue;
                
                // Check prerequisites
                const prerequisitesMet = this.checkPrerequisites(topic.prerequisites);
                if (prerequisitesMet) {
                    return {
                        section: section.name,
                        topic: topic,
                        topic_id: topic.id,
                        is_retry: false
                    };
                }
            }
        }
        
        // If all topics are completed, return null
        if (this.tracker.completed_topics.length === this.getTotalTopics()) {
            console.log('[TRACKER] All topics completed! Curriculum finished.');
            return null;
        }
        
        // If no pending topic found but not all completed, there may be prerequisites blocking
        console.warn('[TRACKER] No pending topics found but curriculum not complete. Check prerequisites.');
        return null;
    }
    
    checkPrerequisites(prerequisites) {
        if (!prerequisites || prerequisites.length === 0) {
            return true;
        }
        
        for (const prereqId of prerequisites) {
            if (!this.tracker.completed_topics.includes(prereqId)) {
                return false;
            }
        }
        return true;
    }
    
    // ========================================================================
    // TOPIC STATUS MANAGEMENT
    // ========================================================================
    
    async markTopicStarted(topicId) {
        if (!this.tracker.in_progress_topics.includes(topicId)) {
            this.tracker.in_progress_topics.push(topicId);
        }
        
        // Remove from failed if present
        this.tracker.failed_topics = this.tracker.failed_topics.filter(f => f.topic_id !== topicId);
        
        this.tracker.current_topic_id = topicId;
        this.tracker.current_section = this.findSectionForTopic(topicId);
        this.tracker.last_sync = new Date().toISOString();
        
        this.recalculateStatistics();
        await this.saveTracker();
        
        console.log(`[TRACKER] Started topic: ${topicId}`);
        return true;
    }
    
    async markTopicCompleted(topicId, lessonId, lmConfirmed = true, confidence = 1.0) {
        // Remove from in_progress
        this.tracker.in_progress_topics = this.tracker.in_progress_topics.filter(id => id !== topicId);
        
        // Add to completed if not already
        if (!this.tracker.completed_topics.includes(topicId)) {
            this.tracker.completed_topics.push(topicId);
        }
        
        // Add to history
        const topic = this.findTopicById(topicId);
        this.tracker.topic_history.push({
            topic_id: topicId,
            topic_name: topic?.name || topicId,
            completed_at: new Date().toISOString(),
            lesson_id: lessonId,
            lm_confirmed: lmConfirmed,
            confidence: confidence,
            estimated_lessons: topic?.estimated_lessons || 1
        });
        
        // Record LM confirmation
        if (lmConfirmed) {
            this.tracker.lm_confirmation_received.push({
                topic_id: topicId,
                lesson_id: lessonId,
                confidence: confidence,
                timestamp: new Date().toISOString()
            });
        }
        
        this.tracker.last_sync = new Date().toISOString();
        this.recalculateStatistics();
        
        await this.saveTracker();
        
        console.log(`[TRACKER] Completed topic: ${topicId}. Progress: ${this.tracker.statistics.percent_complete.toFixed(1)}%`);
        
        // Check if curriculum is complete
        if (this.tracker.completed_topics.length === this.tracker.statistics.total_topics) {
            console.log('[TRACKER] 🎉 CURRICULUM COMPLETE! 🎉');
            this.tracker.statistics.completion_date = new Date().toISOString();
            await this.saveTracker();
        }
        
        return this.getNextTopic();
    }
    
    async markTopicFailed(topicId, reason) {
        // Remove from in_progress
        this.tracker.in_progress_topics = this.tracker.in_progress_topics.filter(id => id !== topicId);
        
        // Find existing failed entry or create new
        const existingFailed = this.tracker.failed_topics.find(f => f.topic_id === topicId);
        if (existingFailed) {
            existingFailed.retry_count++;
            existingFailed.last_failed_at = new Date().toISOString();
            existingFailed.reasons.push(reason);
        } else {
            this.tracker.failed_topics.push({
                topic_id: topicId,
                failed_at: new Date().toISOString(),
                last_failed_at: new Date().toISOString(),
                reason: reason,
                reasons: [reason],
                retry_count: 1
            });
        }
        
        this.tracker.last_sync = new Date().toISOString();
        this.recalculateStatistics();
        
        await this.saveTracker();
        
        console.log(`[TRACKER] Failed topic: ${topicId}. Reason: ${reason}. Retry count: ${existingFailed?.retry_count || 1}`);
        return this.getNextTopic();
    }
    
    async recordLMConfirmation(topicId, lessonId, understood, confidence) {
        const confirmation = {
            topic_id: topicId,
            lesson_id: lessonId,
            understood: understood,
            confidence: confidence,
            timestamp: new Date().toISOString()
        };
        
        if (understood && confidence >= 0.7) {
            this.tracker.lm_confirmation_received.push(confirmation);
            await this.markTopicCompleted(topicId, lessonId, true, confidence);
            return { success: true, action: 'completed', next_topic: this.getNextTopic() };
        } else if (!understood && confidence < 0.5) {
            this.tracker.lm_confirmation_failed.push(confirmation);
            await this.markTopicFailed(topicId, `LM did not understand (confidence: ${confidence})`);
            return { success: false, action: 'failed', next_topic: this.getNextTopic() };
        } else {
            this.tracker.lm_confirmation_pending.push(confirmation);
            await this.saveTracker();
            return { success: null, action: 'pending', message: 'Confirmation pending review' };
        }
    }
    
    // ========================================================================
    // PROGRESS REPORTING
    // ========================================================================
    
    getProgressReport() {
        const next = this.getNextTopic();
        
        return {
            current_topic_id: this.tracker.current_topic_id,
            current_section: this.tracker.current_section,
            completed: this.tracker.statistics.completed,
            total: this.tracker.statistics.total_topics,
            percent: this.tracker.statistics.percent_complete,
            in_progress: this.tracker.in_progress_topics.length,
            failed: this.tracker.failed_topics.length,
            estimated_lessons_completed: this.tracker.statistics.estimated_lessons_completed,
            estimated_lessons_remaining: this.tracker.statistics.estimated_lessons_remaining,
            next_topic: next ? next.topic : null,
            curriculum_complete: this.tracker.completed_topics.length === this.tracker.statistics.total_topics,
            last_sync: this.tracker.last_sync,
            start_date: this.tracker.statistics.start_date
        };
    }
    
    getFailedTopicsSummary() {
        return this.tracker.failed_topics.map(failed => {
            const topic = this.findTopicById(failed.topic_id);
            return {
                topic_id: failed.topic_id,
                topic_name: topic?.name || failed.topic_id,
                retry_count: failed.retry_count,
                reasons: failed.reasons,
                last_failed: failed.last_failed_at
            };
        });
    }
    
    getCompletedTopicsSummary() {
        return this.tracker.completed_topics.map(topicId => {
            const topic = this.findTopicById(topicId);
            const history = this.tracker.topic_history.find(h => h.topic_id === topicId);
            return {
                topic_id: topicId,
                topic_name: topic?.name || topicId,
                completed_at: history?.completed_at,
                confidence: history?.confidence
            };
        });
    }
    
    // ========================================================================
    // PERSISTENCE
    // ========================================================================
    
    async saveTracker() {
        // Save to localStorage as backup
        localStorage.setItem('curriculum_tracker', JSON.stringify(this.tracker));
        
        // Try to save via API
        try {
            const response = await fetch('/api/tracker/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(this.tracker)
            });
            if (response.ok) {
                console.log('[TRACKER] Saved to server');
            }
        } catch (e) {
            console.log('[TRACKER] Could not save to server, using localStorage only');
        }
    }
    
    loadFromLocalStorage() {
        const saved = localStorage.getItem('curriculum_tracker');
        if (saved) {
            this.tracker = JSON.parse(saved);
            console.log('[TRACKER] Loaded from localStorage');
            return true;
        }
        return false;
    }
    
    // ========================================================================
    // AUTO-TRACKING
    // ========================================================================
    
    async ensureOnTrack() {
        const report = this.getProgressReport();
        
        // Check if curriculum is complete
        if (report.curriculum_complete) {
            console.log('[TRACKER] Curriculum already complete. No action needed.');
            return null;
        }
        
        // Check if we have a current topic
        if (!report.current_topic_id && report.next_topic) {
            console.log(`[TRACKER] No current topic, starting next: ${report.next_topic.name}`);
            await this.markTopicStarted(report.next_topic.id);
            return report.next_topic;
        }
        
        // Check if current topic is stuck
        const currentTopic = this.findTopicById(report.current_topic_id);
        if (currentTopic && this.tracker.in_progress_topics.includes(report.current_topic_id)) {
            const inProgressDuration = this.tracker.topic_history.find(h => h.topic_id === report.current_topic_id)?.completed_at;
            if (inProgressDuration) {
                const duration = Date.now() - new Date(inProgressDuration).getTime();
                if (duration > 3600000) { // Stuck for over 1 hour
                    console.warn(`[TRACKER] Topic stuck for ${Math.floor(duration / 60000)} minutes, marking as failed`);
                    await this.markTopicFailed(report.current_topic_id, 'Topic timed out - no progress');
                }
            }
        }
        
        return report.next_topic;
    }
    
    startAutoTracking(intervalSeconds = 60) {
        if (this.interval) {
            clearInterval(this.interval);
        }
        
        this.interval = setInterval(async () => {
            const nextTopic = await this.ensureOnTrack();
            if (nextTopic && !this.tracker.in_progress_topics.includes(nextTopic.id)) {
                console.log(`[TRACKER] Auto-starting next topic: ${nextTopic.name}`);
                await this.markTopicStarted(nextTopic.id);
                
                // Trigger lesson generation if function exists
                if (typeof window.generateProactiveLesson === 'function') {
                    window.generateProactiveLesson();
                }
            }
        }, intervalSeconds * 1000);
        
        console.log(`[TRACKER] Auto-tracking started (interval: ${intervalSeconds}s)`);
    }
    
    stopAutoTracking() {
        if (this.interval) {
            clearInterval(this.interval);
            this.interval = null;
            console.log('[TRACKER] Auto-tracking stopped');
        }
    }
    
    // ========================================================================
    // EXPORT/IMPORT
    // ========================================================================
    
    exportTracker() {
        return JSON.stringify(this.tracker, null, 2);
    }
    
    importTracker(trackerJson) {
        try {
            this.tracker = JSON.parse(trackerJson);
            this.validateTracker();
            this.saveTracker();
            console.log('[TRACKER] Tracker imported successfully');
            return true;
        } catch (error) {
            console.error('[TRACKER] Failed to import tracker:', error);
            return false;
        }
    }
    
    resetTracker() {
        this.tracker = this.createNewTracker();
        this.saveTracker();
        console.log('[TRACKER] Tracker reset to initial state');
    }
}

// ============================================================================
// INITIALIZE TRACKER ON PAGE LOAD
// ============================================================================

let curriculumTracker = null;

document.addEventListener('DOMContentLoaded', async () => {
    curriculumTracker = new CurriculumTracker();
    const initialized = await curriculumTracker.initialize();
    
    if (initialized) {
        // Try to load from localStorage as backup
        if (!curriculumTracker.tracker.last_sync || Date.now() - new Date(curriculumTracker.tracker.last_sync).getTime() > 86400000) {
            curriculumTracker.loadFromLocalStorage();
        }
        
        // Start auto-tracking
        curriculumTracker.startAutoTracking(60);
        
        // Make available globally
        window.curriculumTracker = curriculumTracker;
        
        // Display initial progress
        const report = curriculumTracker.getProgressReport();
        console.log('[TRACKER] Ready. Progress:', report.percent.toFixed(1) + '%');
        
        // Update UI if elements exist
        const progressEl = document.getElementById('curriculum-progress');
        if (progressEl) {
            progressEl.innerHTML = `${report.completed}/${report.total} topics (${report.percent.toFixed(1)}%)`;
        }
        
        const nextTopicEl = document.getElementById('next-topic');
        if (nextTopicEl && report.next_topic) {
            nextTopicEl.innerHTML = report.next_topic.name;
        }
        
        // Start first lesson if nothing in progress
        if (report.completed === 0 && report.in_progress === 0 && !report.curriculum_complete) {
            console.log('[TRACKER] Starting first lesson...');
            setTimeout(() => {
                if (typeof window.generateProactiveLesson === 'function') {
                    window.generateProactiveLesson();
                }
            }, 5000);
        }
    } else {
        console.error('[TRACKER] Failed to initialize. Check that curriculum.json exists.');
    }
});
