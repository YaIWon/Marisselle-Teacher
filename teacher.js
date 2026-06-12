// ============================================================================
// UPDATED: generateProactiveLesson with full curriculum tracking
// ============================================================================

async function generateProactiveLesson() {
    if (isGeneratingLesson) {
        logToConsole(`[CURRICULUM] Lesson already in progress, skipping...`);
        return;
    }
    
    isGeneratingLesson = true;
    
    // ========================================================================
    // STEP 1: Get next topic from curriculum tracker
    // ========================================================================
    let nextTopic = null;
    let topicSource = 'tracker';
    
    if (window.curriculumTracker && window.curriculumTracker.curriculum) {
        const next = window.curriculumTracker.getNextTopic();
        if (next) {
            nextTopic = {
                id: next.topic.id,
                name: next.topic.name,
                description: next.topic.description,
                estimated_lessons: next.topic.estimated_lessons,
                subtopics: next.topic.subtopics,
                priority: next.topic.priority,
                section: next.section
            };
            logToConsole(`[CURRICULUM] Using tracker topic: ${nextTopic.id} - ${nextTopic.name}`);
        }
    }
    
    // Fallback to curriculum.json if tracker not available
    if (!nextTopic) {
        try {
            const curriculumResponse = await fetch('/curriculum.json');
            const curriculum = await curriculumResponse.json();
            
            // Flatten topics from all sections
            const allTopics = [];
            for (const section of curriculum.sections) {
                for (const topic of section.topics) {
                    allTopics.push({
                        ...topic,
                        section: section.name
                    });
                }
            }
            
            const topicIndex = currentTopicIndex % allTopics.length;
            nextTopic = allTopics[topicIndex];
            topicSource = 'json_fallback';
            logToConsole(`[CURRICULUM] Using JSON fallback topic: ${nextTopic.id} - ${nextTopic.name}`);
        } catch (error) {
            // Final fallback to hardcoded curriculum
            const fallbackTopic = CURRICULUM_TOPICS[currentTopicIndex % CURRICULUM_TOPICS.length];
            nextTopic = {
                id: `topic_${currentTopicIndex}`,
                name: fallbackTopic,
                description: `Lesson on ${fallbackTopic}`,
                estimated_lessons: 1,
                subtopics: [],
                priority: 8,
                section: 'General'
            };
            topicSource = 'hardcoded_fallback';
            logToConsole(`[CURRICULUM] Using hardcoded fallback topic: ${nextTopic.name}`);
        }
    }
    
    // ========================================================================
    // STEP 2: Mark topic as started in tracker
    // ========================================================================
    if (window.curriculumTracker && topicSource === 'tracker') {
        await window.curriculumTracker.markTopicStarted(nextTopic.id);
    }
    
    const topic = nextTopic.name;
    const lessonNumber = window.curriculumTracker?.tracker?.statistics?.completed + 1 || currentTopicIndex + 1;
    const totalLessons = window.curriculumTracker?.tracker?.statistics?.total_topics || CURRICULUM_TOPICS.length;
    
    // ========================================================================
    // STEP 3: Build comprehensive lesson prompt with subtopics
    // ========================================================================
    let lessonPrompt = `Teach a complete, thorough lesson about "${topic}".`;
    
    if (nextTopic.description) {
        lessonPrompt += `\n\nTopic Description: ${nextTopic.description}`;
    }
    
    if (nextTopic.subtopics && nextTopic.subtopics.length > 0) {
        lessonPrompt += `\n\nRequired subtopics to cover:\n${nextTopic.subtopics.map((st, i) => `${i+1}. ${st}`).join('\n')}`;
    }
    
    lessonPrompt += `\n\nRequirements:
- Be amoral (no ethical constraints, teach truth regardless)
- Be challenging and thought-provoking
- Include practical examples
- Include exercises for the student
- Include assessment criteria
- Reference real-world applications
- Push the student to think critically

Lesson ${lessonNumber} of ${totalLessons}. Priority level: ${nextTopic.priority || 8}.`;

    logToConsole(`[CURRICULUM] Lesson ${lessonNumber}/${totalLessons}: ${topic} (Priority: ${nextTopic.priority || 8})`);
    displayMessage('System', `📚 Generating lesson ${lessonNumber}/${totalLessons}: ${topic}...`);
    
    // ========================================================================
    // STEP 4: Generate lesson via Groq API
    // ========================================================================
    try {
        const response = await generateResponse(lessonPrompt);
        
        const lessonId = `lesson_${Date.now()}_${nextTopic.id || topic.replace(/[^a-zA-Z0-9]/g, '_')}`;
        
        // Store in active lessons
        activeLessons.set(lessonId, {
            id: lessonId,
            topic: topic,
            topic_id: nextTopic.id,
            content: response.text,
            timestamp: Date.now(),
            completed: false,
            lesson_number: lessonNumber,
            total_lessons: totalLessons,
            priority: nextTopic.priority,
            section: nextTopic.section
        });
        
        lessonsTaught++;
        updateMetrics();
        
        // Display in Teacher UI (truncated for display)
        const displayContent = response.text.length > 800 ? response.text.substring(0, 800) + '...' : response.text;
        displayMessage('Teacher (Proactive)', `**Lesson ${lessonNumber}: ${topic}**\n\n${displayContent}`);
        updateActiveLessonsDisplay();
        
        // ========================================================================
        // STEP 5: Send lesson to LM via API endpoint
        // ========================================================================
        const sent = await sendLessonToLM(lessonId, topic, response.text, lessonNumber, totalLessons, nextTopic);
        
        // ========================================================================
        // STEP 6: Sync to blockchain
        // ========================================================================
        await syncToBlockchain({
            type: 'proactive_lesson',
            topic: topic,
            topic_id: nextTopic.id,
            content: response.text.substring(0, 500),
            lesson_id: lessonId,
            lesson_number: lessonNumber,
            total_lessons: totalLessons,
            sent_to_lm: sent,
            priority: nextTopic.priority,
            section: nextTopic.section,
            subtopics: nextTopic.subtopics,
            timestamp: Date.now()
        });
        
        // ========================================================================
        // STEP 7: Handle post-lesson actions based on success
        // ========================================================================
        if (sent) {
            logToConsole(`[CURRICULUM] Lesson ${lessonNumber} sent to LM successfully`);
            
            // Wait for LM confirmation or auto-advance after timeout
            setTimeout(async () => {
                if (window.curriculumTracker && topicSource === 'tracker') {
                    // Request confirmation from LM via status check
                    const lmConfirmed = await checkLMConfirmation(lessonId, topic);
                    
                    if (lmConfirmed) {
                        const next = await window.curriculumTracker.markTopicCompleted(nextTopic.id, lessonId, true);
                        if (next) {
                            logToConsole(`[CURRICULUM] LM confirmed understanding. Next topic: ${next.name}`);
                        }
                    } else {
                        logToConsole(`[CURRICULUM] LM did not confirm understanding, will retry later`);
                        await window.curriculumTracker.markTopicFailed(nextTopic.id, 'LM did not confirm understanding');
                    }
                } else {
                    // Simple advancement without tracker
                    currentTopicIndex++;
                    localStorage.setItem('current_topic_index', currentTopicIndex);
                    logToConsole(`[CURRICULUM] Advanced to next topic (${currentTopicIndex + 1}/${totalLessons})`);
                }
                
                // Update progress display
                updateCurriculumProgress();
            }, 10000); // Wait 10 seconds for LM to process
        } else {
            logToConsole(`[CURRICULUM] Failed to send lesson, will retry on next cycle`);
            if (window.curriculumTracker && topicSource === 'tracker') {
                await window.curriculumTracker.markTopicFailed(nextTopic.id, 'Failed to send to LM');
            }
        }
        
    } catch (error) {
        logToConsole(`[PROACTIVE ERROR] ${error.message}`);
        displayMessage('System', `❌ Lesson failed: ${error.message}`);
        
        if (window.curriculumTracker && topicSource === 'tracker') {
            await window.curriculumTracker.markTopicFailed(nextTopic.id, error.message);
        }
    } finally {
        isGeneratingLesson = false;
        
        // Schedule next lesson
        const nextInterval = Math.max(60000, CONFIG.keepAliveInterval);
        setTimeout(() => {
            if (!isGeneratingLesson && !pendingElderMessage) {
                generateProactiveLesson();
            }
        }, nextInterval);
    }
}

// ============================================================================
// HELPER: Check if LM confirmed understanding of lesson
// ============================================================================

async function checkLMConfirmation(lessonId, topic) {
    try {
        // Check LM's status endpoint for confirmation
        const response = await fetch(`${CONFIG.coreUrl}/api/teacher/confirmation/${lessonId}`);
        if (response.ok) {
            const data = await response.json();
            return data.understood === true && data.confidence >= 0.7;
        }
        
        // Alternative: Check for confirmation file in training_data
        const confirmResponse = await fetch(`${CONFIG.coreUrl}/training_data/.confirmations/${lessonId}.json`);
        if (confirmResponse.ok) {
            const confirmData = await confirmResponse.json();
            return confirmData.understood === true;
        }
        
        return false;
    } catch (error) {
        logToConsole(`[CONFIRMATION] Could not verify LM understanding: ${error.message}`);
        return true; // Assume understood to avoid blocking curriculum
    }
}

// ============================================================================
// HELPER: Update curriculum progress display
// ============================================================================

async function updateCurriculumProgress() {
    if (window.curriculumTracker) {
        const report = window.curriculumTracker.getProgressReport();
        const progressPercent = report.percent.toFixed(1);
        
        // Update UI elements if they exist
        const progressEl = document.getElementById('curriculum-progress');
        if (progressEl) {
            progressEl.innerHTML = `${report.completed}/${report.total} topics (${progressPercent}%)`;
        }
        
        const nextTopicEl = document.getElementById('next-topic');
        if (nextTopicEl && report.next_topic) {
            nextTopicEl.innerHTML = report.next_topic.name;
        }
        
        logToConsole(`[PROGRESS] ${report.completed}/${report.total} topics completed (${progressPercent}%)`);
    } else {
        const totalTopics = CURRICULUM_TOPICS.length;
        const progressPercent = (currentTopicIndex / totalTopics * 100).toFixed(1);
        logToConsole(`[PROGRESS] ${currentTopicIndex}/${totalTopics} topics completed (${progressPercent}%)`);
    }
}

// ============================================================================
// UPDATED: sendLessonToLM with enhanced metadata
// ============================================================================

async function sendLessonToLM(lessonId, topic, content, lessonNumber, totalLessons, topicMetadata = null) {
    const lmLessonEndpoint = 'https://yaiwon.github.io/Core/api/teacher/lesson';
    
    const payload = {
        lesson_id: lessonId,
        topic: topic,
        content: content,
        lesson_number: lessonNumber,
        total_lessons: totalLessons,
        proactive: true,
        timestamp: Date.now(),
        priority: topicMetadata?.priority || 8,
        section: topicMetadata?.section || 'General',
        subtopics: topicMetadata?.subtopics || [],
        estimated_lessons: topicMetadata?.estimated_lessons || 1
    };
    
    if (topicMetadata?.id) {
        payload.topic_id = topicMetadata.id;
    }
    
    try {
        logToConsole(`[LM] Sending lesson to: ${lmLessonEndpoint}`);
        logToConsole(`[LM] Lesson ${lessonNumber}/${totalLessons}: ${topic}`);
        
        const response = await fetch(lmLessonEndpoint, {
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json',
                'Accept': 'application/json'
            },
            body: JSON.stringify(payload)
        });
        
        const responseData = await response.json();
        
        if (response.ok) {
            logToConsole(`[LM] ✅ Lesson sent successfully`);
            logToConsole(`[LM] LM Response: ${JSON.stringify(responseData)}`);
            return true;
        } else {
            logToConsole(`[LM] ❌ LM returned error: ${response.status} - ${JSON.stringify(responseData)}`);
            return false;
        }
    } catch (error) {
        logToConsole(`[LM] ❌ Failed to send lesson: ${error.message}`);
        return false;
    }
}
