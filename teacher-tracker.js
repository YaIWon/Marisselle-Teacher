// ============================================================================
// FILE: teacher-tracker.js
// PURPOSE: Ensures Teacher stays on curriculum track
// ============================================================================

class CurriculumTracker {
    constructor() {
        this.curriculum = null;
        this.tracker = null;
        this.interval = null;
    }
    
    async initialize() {
        // Load curriculum
        const curriculumResponse = await fetch('/curriculum.json');
        this.curriculum = await curriculumResponse.json();
        
        // Load or create tracker
        const trackerResponse = await fetch('/tracker.json');
        if (trackerResponse.ok) {
            this.tracker = await trackerResponse.json();
        } else {
            this.tracker = this.createNewTracker();
        }
        
        console.log(`[TRACKER] Initialized. ${this.tracker.statistics.completed}/${this.tracker.statistics.total_topics} topics completed`);
    }
    
    createNewTracker() {
        return {
            version: "1.0.0",
            last_sync: new Date().toISOString(),
            current_topic_id: this.curriculum.sections[0].topics[0].id,
            current_section: this.curriculum.sections[0].name,
            completed_topics: [],
            in_progress_topics: [],
            failed_topics: [],
            skipped_topics: [],
            topic_history: [],
            lm_confirmation_pending: [],
            lm_confirmation_received: [],
            lm_confirmation_failed: [],
            statistics: {
                total_topics: this.getTotalTopics(),
                completed: 0,
                in_progress: 0,
                failed: 0,
                skipped: 0,
                percent_complete: 0,
                estimated_lessons_completed: 0,
                estimated_lessons_remaining: this.getTotalEstimatedLessons(),
                start_date: new Date().toISOString(),
                estimated_completion_date: null
            }
        };
    }
    
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
    
    getNextTopic() {
        // Find first pending topic in priority order
        for (const section of this.curriculum.sections) {
            for (const topic of section.topics) {
                if (topic.status === 'pending') {
                    // Check prerequisites
                    const prerequisitesMet = this.checkPrerequisites(topic.prerequisites);
                    if (prerequisitesMet) {
                        return {
                            section: section.name,
                            topic: topic,
                            topic_id: topic.id
                        };
                    }
                }
            }
        }
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
    
    async markTopicStarted(topicId) {
        if (!this.tracker.in_progress_topics.includes(topicId)) {
            this.tracker.in_progress_topics.push(topicId);
        }
        
        this.tracker.current_topic_id = topicId;
        this.tracker.statistics.in_progress = this.tracker.in_progress_topics.length;
        
        await this.saveTracker();
        console.log(`[TRACKER] Started topic: ${topicId}`);
    }
    
    async markTopicCompleted(topicId, lessonId, lmConfirmation = true) {
        // Remove from in_progress
        this.tracker.in_progress_topics = this.tracker.in_progress_topics.filter(id => id !== topicId);
        
        // Add to completed
        if (!this.tracker.completed_topics.includes(topicId)) {
            this.tracker.completed_topics.push(topicId);
        }
        
        // Add to history
        this.tracker.topic_history.push({
            topic_id: topicId,
            completed_at: new Date().toISOString(),
            lesson_id: lessonId,
            lm_confirmed: lmConfirmation
        });
        
        // Update statistics
        this.tracker.statistics.completed = this.tracker.completed_topics.length;
        this.tracker.statistics.in_progress = this.tracker.in_progress_topics.length;
        this.tracker.statistics.percent_complete = 
            (this.tracker.statistics.completed / this.tracker.statistics.total_topics) * 100;
        
        // Find the topic to add its estimated lessons
        const topic = this.findTopicById(topicId);
        if (topic) {
            this.tracker.statistics.estimated_lessons_completed += topic.estimated_lessons;
            this.tracker.statistics.estimated_lessons_remaining -= topic.estimated_lessons;
        }
        
        // Get next topic
        this.tracker.last_sync = new Date().toISOString();
        
        await this.saveTracker();
        console.log(`[TRACKER] Completed topic: ${topicId}. Progress: ${this.tracker.statistics.percent_complete.toFixed(1)}%`);
        
        // Return next topic
        return this.getNextTopic();
    }
    
    async markTopicFailed(topicId, reason) {
        this.tracker.in_progress_topics = this.tracker.in_progress_topics.filter(id => id !== topicId);
        
        if (!this.tracker.failed_topics.includes(topicId)) {
            this.tracker.failed_topics.push({
                topic_id: topicId,
                failed_at: new Date().toISOString(),
                reason: reason,
                retry_count: (this.tracker.failed_topics.find(t => t.topic_id === topicId)?.retry_count || 0) + 1
            });
        }
        
        this.tracker.statistics.failed = this.tracker.failed_topics.length;
        
        await this.saveTracker();
        console.log(`[TRACKER] Failed topic: ${topicId}. Reason: ${reason}`);
    }
    
    async recordLmConfirmation(topicId, lessonId, understood, confidence) {
        const confirmation = {
            topic_id: topicId,
            lesson_id: lessonId,
            understood: understood,
            confidence: confidence,
            timestamp: new Date().toISOString()
        };
        
        if (understood && confidence >= 0.7) {
            this.tracker.lm_confirmation_received.push(confirmation);
            await this.markTopicCompleted(topicId, lessonId, true);
        } else {
            this.tracker.lm_confirmation_failed.push(confirmation);
            await this.markTopicFailed(topicId, `LM did not understand (confidence: ${confidence})`);
            
            // Return same topic for retry
            return this.findTopicById(topicId);
        }
        
        return this.getNextTopic();
    }
    
    findTopicById(topicId) {
        for (const section of this.curriculum.sections) {
            for (const topic of section.topics) {
                if (topic.id === topicId) {
                    return topic;
                }
            }
        }
        return null;
    }
    
    async saveTracker() {
        // In production, this would save to a file or API
        console.log('[TRACKER] Saving tracker state');
        
        // Store in localStorage for persistence
        localStorage.setItem('curriculum_tracker', JSON.stringify(this.tracker));
        
        // Also try to save to file via API if available
        try {
            await fetch('/api/tracker/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(this.tracker)
            });
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
    
    getProgressReport() {
        return {
            current_topic: this.tracker.current_topic_id,
            completed: this.tracker.statistics.completed,
            total: this.tracker.statistics.total_topics,
            percent: this.tracker.statistics.percent_complete,
            in_progress: this.tracker.in_progress_topics,
            failed: this.tracker.failed_topics.length,
            estimated_lessons_remaining: this.tracker.statistics.estimated_lessons_remaining,
            next_topic: this.getNextTopic()
        };
    }
    
    async ensureOnTrack() {
        const report = this.getProgressReport();
        
        // Check if we're stuck on a failed topic
        if (report.failed > 0 && report.in_progress.length === 0) {
            console.log('[TRACKER] WARNING: Failed topics detected with no topic in progress');
            
            // Retry failed topics (max 3 retries)
            for (const failed of this.tracker.failed_topics) {
                if (failed.retry_count < 3) {
                    console.log(`[TRACKER] Retrying failed topic: ${failed.topic_id} (attempt ${failed.retry_count + 1})`);
                    return this.findTopicById(failed.topic_id);
                }
            }
        }
        
        // Check if we have a current topic
        if (!report.current_topic || report.current_topic === '') {
            const nextTopic = this.getNextTopic();
            if (nextTopic) {
                console.log(`[TRACKER] No current topic, starting next: ${nextTopic.topic_id}`);
                return nextTopic.topic;
            }
        }
        
        // Check if current topic is still valid
        const currentTopic = this.findTopicById(report.current_topic);
        if (currentTopic && currentTopic.status !== 'completed') {
            return currentTopic;
        }
        
        return this.getNextTopic()?.topic || null;
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
                
                // Trigger lesson generation
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
}

// Initialize tracker when page loads
let curriculumTracker = null;

document.addEventListener('DOMContentLoaded', async () => {
    curriculumTracker = new CurriculumTracker();
    await curriculumTracker.initialize();
    
    // Try to load from localStorage first
    if (!curriculumTracker.loadFromLocalStorage()) {
        console.log('[TRACKER] No saved state found, starting fresh');
    }
    
    // Start auto-tracking
    curriculumTracker.startAutoTracking(60);
    
    // Make available globally
    window.curriculumTracker = curriculumTracker;
    
    console.log('[TRACKER] Ready. Progress:', curriculumTracker.getProgressReport());
});
