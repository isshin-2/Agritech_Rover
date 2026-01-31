const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const db = require('../database');

let port = null;
let io = null;
let pumpState = false;
let motorState = "stopped"; // stopped, forward, backward
let lastSend = 0; // Timestamp of last received data

// Config
// Config
const ARDUINO_PORT = process.env.ARDUINO_PORT || '/dev/ttyUSB0'; // Default to USB
const BAUD_RATE = parseInt(process.env.ARDUINO_BAUD) || 9600;

function init() {
    try {
        port = new SerialPort({ path: ARDUINO_PORT, baudRate: BAUD_RATE });
        const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

        port.on('open', () => {
            console.log('Serial Port Opened');
            // Watchdog: If no data received in 5 seconds, start simulation
            setTimeout(() => {
                if (!lastSend) {
                    console.log('⚠️ Serial Open but Silent. Starting SIMULATION MODE.');
                    startSimulation();
                }
            }, 5000);
        });

        parser.on('data', (data) => {
            lastSend = Date.now(); // Update timestamp
            handleData(data.trim());
        });
        
        port.on('error', (err) => {
            console.error('Serial Error: ', err.message);
            // If port fails (e.g. Access Denied on GPIO), fallback to simulation
            if (!port.isOpen) {
                console.log('⚠️ Serial Port Failed. Starting SIMULATION MODE.');
                startSimulation();
            }
        });

    } catch (err) {
        console.error('Failed to open Serial Port:', err.message);
        console.log('⚠️ Starting SIMULATION MODE (Generates fake sensor data)');
        startSimulation();
    }
}

function startSimulation() {
    setInterval(() => {
        const simData = `M:${400 + Math.floor(Math.random()*100)}|W:${500 + Math.floor(Math.random()*50)}|T:${25 + Math.random()*5}|H:${60 + Math.floor(Math.random()*10)}|O:0`;
        handleData(simData);
    }, 2000);
}

function handleData(line) {
    // Format: "M:450|W:512|T:28.5|H:65|O:0"
    console.log('RX:', line);
    
    const regex = /M:(\d+)\|W:(\d+)\|T:([\d.]+)\|H:(\d+)\|O:(\d)/;
    const match = line.match(regex);
    
    if (match) {
        const sensors = {
            soil_moisture: mapValue(parseInt(match[1]), 0, 1023, 0, 100), // Map if needed, user said 0-1023 -> 0-100%
            is_raining: parseInt(match[2]) === 1, // Changed from water_level (0 or 1)
            temperature: parseFloat(match[3]),
            humidity: mapValue(parseInt(match[4]), 0, 1023, 0, 100), // Assuming analog DHT needs mapping? Or is it direct?
            // "DHT22 analog equivalent, 0-5V -> 0-100%" implies mapping needed.
            obstacle_detected: parseInt(match[5]) === 1
        };

        // SAFETY LOGIC
        if (sensors.obstacle_detected && motorState !== 'stopped') {
            console.log('CRITICAL: Obstacle Detected! Stopping Rover.');
            stopMotors();
            if(io) io.emit('alert', { type: 'obstacle', message: 'Obstacle Detected! Stopping.' });
        }

        // AUTO-WATERING LOGIC
        checkAutoWater(sensors.soil_moisture);

        // Save to DB
        // Save to DB (Mapping is_raining to water_level column for now to save schema change)
        const stmt = db.prepare(`INSERT INTO sensor_data (soil_moisture, water_level, temperature, humidity, obstacle_detected) VALUES (?, ?, ?, ?, ?)`);
        stmt.run(sensors.soil_moisture, sensors.is_raining ? 100 : 0, sensors.temperature, sensors.humidity, sensors.obstacle_detected ? 1 : 0);
        stmt.finalize();

        // Broadcast
        if (io) io.emit('sensor_update', sensors);
    }
}

function checkAutoWater(currentMoisture) {
    db.get(`SELECT value FROM settings WHERE key = 'auto_water_threshold'`, [], (err, row) => {
        if (err || !row) return;

        const threshold = parseInt(row.value);
        
        if (currentMoisture < threshold && !pumpState) {
            console.log(`Auto-Water: Moisture ${currentMoisture}% < ${threshold}%. PUMP ON.`);
            setPump(true);
            setTimeout(() => {
                // Safety timeout (2 mins max)
                if (pumpState) {
                     console.log('Auto-Water: Safety Timeout. PUMP OFF.');
                     setPump(false);
                }
            }, 120000);
        } else if (currentMoisture > (threshold + 15) && pumpState) {
            console.log(`Auto-Water: Moisture ${currentMoisture}% > ${threshold+15}%. PUMP OFF.`);
            setPump(false);
        }
    });
}

function mapValue(x, in_min, in_max, out_min, out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// COMMANDS
function setPump(on) {
    if (port) port.write(on ? "P:ON\n" : "P:OFF\n");
    pumpState = on;
    logAction(on ? 'pump_on' : 'pump_off', 'success');
    if(io) io.emit('rover_update', { pump: pumpState, motor: motorState });
}

function moveForward() {
    if (port) port.write("MOT:F\n");
    motorState = "forward";
    logAction('move_forward', 'success');
    if(io) io.emit('rover_update', { pump: pumpState, motor: motorState });
}

function moveBackward() {
    if (port) port.write("MOT:B\n");
    motorState = "backward";
    logAction('move_backward', 'success');
    if(io) io.emit('rover_update', { pump: pumpState, motor: motorState });
}

function stopMotors() {
    if (port) port.write("MOT:S\n");
    motorState = "stopped";
    logAction('motor_stop', 'success');
    if(io) io.emit('rover_update', { pump: pumpState, motor: motorState });
}

function logAction(action, status) {
    db.run(`INSERT INTO rover_log (action, status) VALUES (?, ?)`, [action, status]);
}

module.exports = {
    init,
    setSocket: (socketIo) => { io = socketIo; },
    setPump,
    moveForward,
    moveBackward,
    stopMotors,
    getPumpStatus: () => pumpState
};

// Start
init();
