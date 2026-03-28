const express = require('express');
const router = express.Router();
const db = require('../database');

// GET Latest sensor reading
router.get('/', (req, res) => {
    db.get(`SELECT * FROM sensor_data ORDER BY id DESC LIMIT 1`, [], (err, row) => {
        if (err) return res.status(500).json({ error: err.message });
        const data = row || {
            soil_moisture: 0,
            air_temperature: 0,
            soil_temperature: 0,
            humidity: 0,
            water_level: 0,
            obstacle_detected: 0
        };

        // FIX: Return correct field names matching the DB schema and what the frontend/ESP32 expects.
        // Previously returned 'temperature' (undefined) instead of 'air_temperature' and 'soil_temperature'.
        // Also maps water_level (0/1) back to is_raining boolean for frontend, and rain_status string for ESP32.
        res.json({
            soil_moisture: data.soil_moisture,
            soil_temperature: data.soil_temperature,
            air_temperature: data.air_temperature,
            air_humidity: data.humidity,
            is_raining: data.water_level === 1,
            rain_status: data.water_level === 1 ? "Raining" : "No Rain",
            obstacle_detected: data.obstacle_detected ? 1 : 0,
            plant_status: "Healthy",
            timestamp: data.timestamp
        });
    });
});

// GET History (for graphs)
router.get('/history', (req, res) => {
    const hours = parseInt(req.query.hours) || 24;
    db.all(
        `SELECT * FROM sensor_data WHERE timestamp >= datetime('now', '-${hours} hours') ORDER BY timestamp ASC`,
        [],
        (err, rows) => {
            if (err) return res.status(500).json({ error: err.message });
            res.json(rows);
        }
    );
});

// POST Manual ingest
router.post('/', (req, res) => {
    const { moisture, water_level, air_temp, soil_temp, humidity, obstacle_detected } = req.body;
    if (moisture === undefined) return res.status(400).json({ error: "Missing data" });

    const stmt = db.prepare(
        `INSERT INTO sensor_data (soil_moisture, water_level, air_temperature, soil_temperature, humidity, obstacle_detected)
         VALUES (?, ?, ?, ?, ?, ?)`
    );
    stmt.run(moisture, water_level, air_temp, soil_temp, humidity, obstacle_detected ? 1 : 0, function (err) {
        if (err) return res.status(500).json({ error: err.message });
        res.json({ success: true, id: this.lastID });
    });
    stmt.finalize();
});

module.exports = router;
