const express = require('express');
const router = express.Router();
const serialService = require('../services/serial');
const db = require('../database');

router.post('/pump/control', (req, res) => {
    const { action } = req.body; // "on" or "off"
    if (!action) return res.status(400).json({ error: "Action required" });
    
    serialService.setPump(action === 'on');
    res.json({ success: true, status: action });
});

router.post('/motor/command', (req, res) => {
    const { command } = req.body; // "forward", "backward", "stop"
    
    switch(command) {
        case 'forward': serialService.moveForward(); break;
        case 'backward': serialService.moveBackward(); break;
        case 'stop': serialService.stopMotors(); break;
        default: return res.status(400).json({ error: "Invalid command" });
    }
    
    res.json({ success: true, command });
});

router.post('/settings', (req, res) => {
    const { auto_water_threshold } = req.body;
    if (auto_water_threshold !== undefined) {
        db.run(`INSERT OR REPLACE INTO settings (id, key, value) VALUES ((SELECT id FROM settings WHERE key = 'auto_water_threshold'), 'auto_water_threshold', ?)`, [auto_water_threshold], (err) => {
            if (err) return res.status(500).json({ error: err.message });
            res.json({ success: true });
        });
    } else {
        res.status(400).json({ error: "Missing threshold" });
    }
});

router.get('/status', (req, res) => {
    // Get latest log + settings
    db.get(`SELECT value FROM settings WHERE key = 'auto_water_threshold'`, [], (err, row) => {
        res.json({
            pump_status: serialService.getPumpStatus(),
             // In a real app, we might track motor state in serial service more robustly
            auto_water_threshold: row ? row.value : 30
        });
    });
});

module.exports = router;
