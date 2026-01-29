const http = require('http');

function checkBackend() {
    console.log("🔍 Checking AgriROV Backend status...");

    const options = {
        hostname: 'localhost',
        port: 5000,
        path: '/',
        method: 'GET',
        timeout: 2000
    };

    const req = http.request(options, (res) => {
        console.log(`✅ Connection Successful! Status Code: ${res.statusCode}`);
        
        let data = '';
        res.on('data', (chunk) => { data += chunk; });
        res.on('end', () => {
            console.log(`📜 Response: ${data}`);
            console.log("🚀 Backend is RUNNING and REACHABLE.");
        });
    });

    req.on('error', (e) => {
        console.error(`❌ Connection Failed: ${e.message}`);
        console.error("⚠️  Possible Causes:");
        console.error("   1. Backend process crashed.");
        console.error("   2. Port 5000 is blocked or used by another app.");
        console.error("   3. start_rover.sh didn't run correctly.");
        console.log("\nTrying debugging logs...");
        console.log("Run: cat backend.log");
    });

    req.end();
}

checkBackend();
