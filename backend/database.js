const sqlite3 = require('sqlite3').verbose();
const path = require('path');
require('dotenv').config();

const dbPath = process.env.DATABASE_PATH || path.resolve(__dirname, 'data.db');

const db = new sqlite3.Database(dbPath, (err) => {
    if (err) {
        console.error('Error opening database ' + dbPath + ': ' + err.message);
    } else {
        console.log('Connected to the SQLite database.');
        initDb();
    }
});

function initDb() {
    db.serialize(() => {
        // Sensor Data Table
        db.run(`CREATE TABLE IF NOT EXISTS sensor_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            soil_moisture INTEGER,
            water_level INTEGER,
            air_temperature REAL,
            soil_temperature REAL,
            humidity INTEGER,
            obstacle_detected BOOLEAN
        )`);
        
        db.run(`CREATE INDEX IF NOT EXISTS idx_sensor_time ON sensor_data(timestamp)`);

        // Plant Health Table
        db.run(`CREATE TABLE IF NOT EXISTS plant_health (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            status TEXT,
            disease_type TEXT,
            confidence INTEGER,
            image_path TEXT
        )`);
        
        db.run(`CREATE INDEX IF NOT EXISTS idx_health_time ON plant_health(timestamp)`);

        // Rover Log Table
        db.run(`CREATE TABLE IF NOT EXISTS rover_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            action TEXT,
            status TEXT,
            details TEXT
        )`);
        
        db.run(`CREATE INDEX IF NOT EXISTS idx_log_time ON rover_log(timestamp)`);

        // Settings Table
        db.run(`CREATE TABLE IF NOT EXISTS settings (
            id INTEGER PRIMARY KEY,
            key TEXT UNIQUE,
            value TEXT
        )`);
        
        // Insert default settings if not exists
        db.run(`INSERT OR IGNORE INTO settings (key, value) VALUES ('auto_water_threshold', '30')`);
    });
}

module.exports = db;
