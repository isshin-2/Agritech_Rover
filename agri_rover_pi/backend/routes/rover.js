const express = require('express');
const router = express.Router();
const serialService = require('../services/serial');
const db = require('../database');

router.post('/pump/control', (req, res) => {
    const { action } = req.body;
    if (!action) return res.status(400).json({ error: "Action required" });
    serialService.setPump(action === 'on');
    res.json({ success: true, status: action });
});

router.post('/motor/command', (req, res) => {
    const { command } = req.body;
    switch (command) {
        case 'forward':  serialService.moveForward();  break;
        case 'backward': serialService.moveBackward(); break;
        case 'stop':     serialService.stopMotors();   break;
        default: return res.status(400).json({ error: "Invalid command. Use: forward, backward, stop" });
    }
    res.json({ success: true, command });
});

router.post('/settings', (req, res) => {
    const { auto_water_threshold } = req.body;
    if (auto_water_threshold === undefined) return res.status(400).json({ error: "Missing threshold" });
    db.run(
        `INSERT OR REPLACE INTO settings (id, key, value)
         VALUES ((SELECT id FROM settings WHERE key = 'auto_water_threshold'), 'auto_water_threshold', ?)`,
        [auto_water_threshold],
        (err) => {
            if (err) return res.status(500).json({ error: err.message });
            res.json({ success: true });
        }
    );
});

// FIX: Now also returns motor_status using the new getMotorStatus() export
router.get('/status', (req, res) => {
    db.get(`SELECT value FROM settings WHERE key = 'auto_water_threshold'`, [], (err, row) => {
        res.json({
            pump_status: serialService.getPumpStatus(),
            motor_status: serialService.getMotorStatus(),
            auto_water_threshold: row ? parseInt(row.value) : 30
        });
    });
});

module.exports = router;
