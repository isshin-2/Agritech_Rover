const express = require('express');
const cors = require('cors');
const http = require('http');
const { Server } = require('socket.io');
require('dotenv').config();

const db = require('./database');
const serialService = require('./services/serial');

// Routes
const sensorRoutes = require('./routes/sensors');
const roverRoutes = require('./routes/rover');
const healthRoutes = require('./routes/health');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
    cors: { origin: "*" }
});

const PORT = process.env.PORT || 3000;

// Middleware
app.use(cors());
app.use(express.json());

// API Routes
app.use('/api/sensors', sensorRoutes);
app.use('/api/rover', roverRoutes);
app.use('/api/health', healthRoutes);

// Display Endpoint (Lightweight for ESP32)
app.get('/api/display/status', (req, res) => {
    // Get latest sensor data
    db.get(`SELECT * FROM sensor_data ORDER BY id DESC LIMIT 1`, [], (err, row) => {
        if (err) return res.status(500).json({ error: err.message });
        
        const data = row || { soil_moisture: 0, temperature: 0, obstacle_detected: 0 };
        
        res.json({
            plant_status: "Check App", // Placeholder, logic can update this based on health check
            moisture: data.soil_moisture,
            temp: data.temperature,
            pump_status: serialService.getPumpStatus() ? "ON" : "OFF",
            obstacle: data.obstacle_detected ? 1 : 0
        });
    });
});

// Socket.io for Real-time push
io.on('connection', (socket) => {
    console.log('Client connected');
    socket.on('disconnect', () => console.log('Client disconnected'));
});

// Pass IO to Serial Service to broadcast updates
serialService.setSocket(io);

server.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
    console.log(`Arduino Port: ${process.env.ARDUINO_PORT || '/dev/ttyUSB0'}`);
});
