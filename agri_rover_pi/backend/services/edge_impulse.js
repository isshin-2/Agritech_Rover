const axios = require('axios');

// Placeholder for Edge Impulse Python Runner
// In a real deployment, this would spawn a Python process using the Edge Impulse Linux SDK
// to run the .eim model on the camera stream.

let isRunning = false;
let lastResult = null;

function startInference() {
    if (isRunning) return;
    console.log('[AI] Starting Edge Impulse Inference...');
    isRunning = true;
    
    // Simulate Inference Loop
    setInterval(() => {
        // Mock result: Randomly detect "Healthy" vs "Disease"
        const r = Math.random();
        lastResult = {
            label: r > 0.8 ? "Early Blight" : "Healthy",
            confidence: (0.7 + Math.random() * 0.29).toFixed(2),
            timestamp: Date.now()
        };
        // console.log('[AI] Result:', lastResult);
    }, 5000); 
}

function stopInference() {
    console.log('[AI] Stopping Inference.');
    isRunning = false;
}

function getLatestResult() {
    return lastResult;
}

module.exports = {
    startInference,
    stopInference,
    getLatestResult
};
