const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const db = require('../database');

let port = null;
let io = null;
let pumpState = false;
let motorState = "stopped";
let lastSend = 0;

const ARDUINO_PORT = process.env.ARDUINO_PORT || '/dev/ttyUSB0';
const BAUD_RATE = parseInt(process.env.ARDUINO_BAUD) || 9600;

function init() {
    try {
        port = new SerialPort({ path: ARDUINO_PORT, baudRate: BAUD_RATE });
        const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

        port.on('open', () => {
            console.log('Serial Port Opened');
            setTimeout(() => {
                if (!lastSend) {
                    console.log('⚠️ Serial Open but Silent. Starting SIMULATION MODE.');
                    startSimulation();
                }
            }, 5000);
        });

        parser.on('data', (data) => {
            lastSend = Date.now();
            handleData(data.trim());
        });

        port.on('error', (err) => {
            console.error('Serial Error: ', err.message);
            if (!port.isOpen) {
                console.log('⚠️ Serial Port Failed. Starting SIMULATION MODE.');
                startSimulation();
            }
        });

    } catch (err) {
        console.error('Failed to open Serial Port:', err.message);
        console.log('⚠️ Starting SIMULATION MODE');
        startSimulation();
    }
}

function startSimulation() {
    // FIX: Simulation now matches Arduino's exact serial format:
    // M:<moisture>|W:<rain>|AT:<airTemp>|ST:<soilTemp>|H:<humidity>|O:<obstacle>
    setInterval(() => {
        const simData = `M:${400 + Math.floor(Math.random() * 100)}|W:${Math.random() > 0.8 ? 1 : 0}|AT:${(25 + Math.random() * 5).toFixed(1)}|ST:${(22 + Math.random() * 5).toFixed(1)}|H:${60 + Math.floor(Math.random() * 10)}|O:0`;
        handleData(simData);
    }, 2000);
}

function handleData(line) {
    // FIX: Regex updated to match Arduino output format exactly:
    // "M:450|W:0|AT:28.5|ST:24.3|H:65|O:0"
    console.log('RX:', line);

    const regex = /M:(\d+)\|W:(\d+)\|AT:([\d.]+)\|ST:([\d.]+)\|H:(\d+)\|O:(\d)/;
    const match = line.match(regex);

    if (match) {
        const rawMoisture = parseInt(match[1]);
        const sensors = {
            // FIX: Analog moisture 0-1023 is inverted (higher = drier). Map to 0-100%.
            soil_moisture: Math.round(mapValue(rawMoisture, 0, 1023, 100, 0)),
            is_raining: parseInt(match[2]) === 1,
            air_temperature: parseFloat(match[3]),
            soil_temperature: parseFloat(match[4]),
            // FIX: DHT11 outputs humidity directly as 0-100, no mapping needed
            humidity: parseInt(match[5]),
            obstacle_detected: parseInt(match[6]) === 1
        };

        // SAFETY LOGIC
        if (sensors.obstacle_detected && motorState !== 'stopped') {
            console.log('CRITICAL: Obstacle Detected! Stopping Rover.');
            stopMotors();
            if (io) io.emit('alert', { type: 'obstacle', message: 'Obstacle Detected! Stopping.' });
        }

        checkAutoWater(sensors.soil_moisture);

        // FIX: Save using correct DB column names (air_temperature, soil_temperature)
        const stmt = db.prepare(
            `INSERT INTO sensor_data (soil_moisture, water_level, air_temperature, soil_temperature, humidity, obstacle_detected)
             VALUES (?, ?, ?, ?, ?, ?)`
        );
        stmt.run(
            sensors.soil_moisture,
            sensors.is_raining ? 1 : 0,
            sensors.air_temperature,
            sensors.soil_temperature,
            sensors.humidity,
            sensors.obstacle_detected ? 1 : 0
        );
        stmt.finalize();

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
                if (pumpState) { console.log('Auto-Water: Safety Timeout. PUMP OFF.'); setPump(false); }
            }, 120000);
        } else if (currentMoisture > (threshold + 15) && pumpState) {
            console.log(`Auto-Water: Moisture ${currentMoisture}% > ${threshold + 15}%. PUMP OFF.`);
            setPump(false);
        }
    });
}

function mapValue(x, in_min, in_max, out_min, out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

function setPump(on) {
    if (port && port.isOpen) port.write(on ? "P:ON\n" : "P:OFF\n");
    pumpState = on;
    logAction(on ? 'pump_on' : 'pump_off', 'success');
    if (io) io.emit('rover_update', { pump: pumpState, motor: motorState });
}

function moveForward() {
    if (port && port.isOpen) port.write("MOT:F\n");
    motorState = "forward";
    logAction('move_forward', 'success');
    if (io) io.emit('rover_update', { pump: pumpState, motor: motorState });
}

function moveBackward() {
    if (port && port.isOpen) port.write("MOT:B\n");
    motorState = "backward";
    logAction('move_backward', 'success');
    if (io) io.emit('rover_update', { pump: pumpState, motor: motorState });
}

function stopMotors() {
    if (port && port.isOpen) port.write("MOT:S\n");
    motorState = "stopped";
    logAction('motor_stop', 'success');
    if (io) io.emit('rover_update', { pump: pumpState, motor: motorState });
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
    getPumpStatus: () => pumpState,
    getMotorStatus: () => motorState
};

init();
