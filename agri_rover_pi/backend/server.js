const express = require('express');
const cors = require('cors');
const http = require('http');
const { Server } = require('socket.io');
require('dotenv').config();

const db = require('./database');
const serialService = require('./services/serial');

const sensorRoutes = require('./routes/sensors');
const roverRoutes = require('./routes/rover');
const healthRoutes = require('./routes/health');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
    cors: { origin: "*" }
});

// FIX: Default port changed from 3000 to 5000 to match frontend API_URL and ESP32 display config
const PORT = process.env.PORT || 5000;

app.use(cors());
app.use(express.json());

app.use('/api/sensors', sensorRoutes);
app.use('/api/rover', roverRoutes);
app.use('/api/health', healthRoutes);

app.get('/', (req, res) => {
    res.send('<h1>🚜 AgriROV Backend is Running!</h1><p>API available on port ' + PORT + '.</p>');
});

// Lightweight endpoint for ESP32 display
// FIX: Query now uses correct column names (air_temperature, soil_temperature)
app.get('/api/display/status', (req, res) => {
    db.get(`SELECT * FROM sensor_data ORDER BY id DESC LIMIT 1`, [], (err, row) => {
        if (err) return res.status(500).json({ error: err.message });
        const data = row || { soil_moisture: 0, air_temperature: 0, soil_temperature: 0, obstacle_detected: 0 };
        res.json({
            plant_status: "Check App",
            moisture: data.soil_moisture,
            air_temp: data.air_temperature,
            soil_temp: data.soil_temperature,
            pump_status: serialService.getPumpStatus() ? "ON" : "OFF",
            obstacle: data.obstacle_detected ? 1 : 0
        });
    });
});

io.on('connection', (socket) => {
    console.log('Client connected');
    socket.on('disconnect', () => console.log('Client disconnected'));
});

serialService.setSocket(io);

server.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
    console.log(`Arduino Port: ${process.env.ARDUINO_PORT || '/dev/ttyUSB0'}`);
});
