const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

// CONFIG
const PORT_NAME = '/dev/ttyS0'; // The GPIO Serial Port
const BAUD_RATE = 9600;

console.log(`🔌 Testing Connection to Arduino on ${PORT_NAME}...`);

try {
    const port = new SerialPort({ path: PORT_NAME, baudRate: BAUD_RATE });
    const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

    console.log("✅ Serial Port Object Created.");

    port.on('open', () => {
        console.log("📂 Port Opened Successfully!");
        console.log("👂 Listening for data... (Press Ctrl+C to stop)");
        console.log("   (If you see nothing for 10s, swap TX/RX wires!)");
    });

    parser.on('data', (data) => {
        console.log(`📥 RECEIVED: ${data.trim()}`);
    });

    port.on('error', (err) => {
        console.error(`❌ SERIAL ERROR: ${err.message}`);
        if (err.message.includes('Access denied')) {
            console.log("💡 TIP: Try running with sudo: 'sudo node verify_serial.js'");
        }
    });

} catch (err) {
    console.error(`❌ CRITICAL FAILURE: ${err.message}`);
}
