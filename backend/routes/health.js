const express = require('express');
const router = express.Router();
const db = require('../database');
// const model = require('../models/plant_disease_model'); // Edge Impulse Stub

// POST Detect (Called by ESP32 or manual upload)
router.post('/detect', (req, res) => {
    // In a real implementation with Edge Impulse:
    // 1. Decode image
    // 2. model.classify(image)
    // 3. Get results
    
    // STUB LOGIC
    const diseases = ['Healthy', 'Early Blight', 'Late Blight', 'Leaf Spot'];
    const randomDisease = diseases[Math.floor(Math.random() * diseases.length)];
    const confidence = Math.floor(Math.random() * (99 - 70) + 70);
    
    const result = {
        disease_type: randomDisease,
        confidence: confidence,
        status: randomDisease === 'Healthy' ? 'Healthy' : 'Diseased',
        solutions: getSolutions(randomDisease)
    };
    
    const stmt = db.prepare(`INSERT INTO plant_health (status, disease_type, confidence) VALUES (?, ?, ?)`);
    stmt.run(result.status, result.disease_type, result.confidence);
    stmt.finalize();
    
    res.json(result);
});

router.get('/current', (req, res) => {
    db.get(`SELECT * FROM plant_health ORDER BY id DESC LIMIT 1`, [], (err, row) => {
        if (err) return res.status(500).json({ error: err.message });
        res.json(row || { status: "Unknown", confidence: 0 });
    });
});

router.get('/history', (req, res) => {
    const limit = parseInt(req.query.limit) || 20;
    db.all(`SELECT * FROM plant_health ORDER BY timestamp DESC LIMIT ?`, [limit], (err, rows) => {
        if (err) return res.status(500).json({ error: err.message });
        res.json(rows);
    });
});

function getSolutions(disease) {
    const remedies = {
        'Early Blight': 'Remove infected leaves. Apply copper-based fungicide. Improve air circulation.',
        'Late Blight': 'Remove entire plant immediately. Do not compost. Apply fungicide as preventative.',
        'Leaf Spot': 'Water at the base, not overhead. Apply neem oil or fungicide.',
        'Healthy': 'Continue standard care. Monitor moisture levels.'
    };
    return remedies[disease] || "Consult an expert.";
}

module.exports = router;
