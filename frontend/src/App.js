import React, { useState, useEffect } from 'react';
import io from 'socket.io-client';
import axios from 'axios';
import SensorPanel from './components/SensorPanel';
import ControlPanel from './components/ControlPanel';
import StreamPanel from './components/StreamPanel';
import HistoryPanel from './components/HistoryPanel';

// Assuming Pi IP, change if needed or use relative if building static
const API_URL = 'http://localhost:3000'; 
const socket = io(API_URL);

function App() {
  const [sensors, setSensors] = useState({
    soil_moisture: 0, water_level: 0, temperature: 0, humidity: 0, obstacle_detected: 0
  });
  const [health, setHealth] = useState(null);
  const [roverStatus, setRoverStatus] = useState({ pump: false, motor: 'stopped' });

  useEffect(() => {
    // Initial Load
    fetchData();

    // Socket Updates
    socket.on('sensor_update', (data) => {
      setSensors(data);
    });
    
    socket.on('alert', (data) => {
        alert(`${data.type.toUpperCase()}: ${data.message}`);
    });

    return () => socket.disconnect();
  }, []);

  const fetchData = async () => {
    try {
        const sRes = await axios.get(`${API_URL}/api/sensors`);
        setSensors(sRes.data);
        const hRes = await axios.get(`${API_URL}/api/health/current`);
        setHealth(hRes.data);
    } catch(err) { console.error(err); }
  };

  return (
    <div className="App" style={{ padding: '20px' }}>
      <header style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '20px', borderBottom: '1px solid #333', paddingBottom: '10px' }}>
        <h1 style={{ color: '#4caf50', margin: 0 }}>🌱 AgriROV Dashboard</h1>
        <div>{new Date().toLocaleTimeString()}</div>
      </header>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 2fr', gap: '20px' }}>
        {/* Left Column */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
            <SensorPanel data={sensors} />
            <ControlPanel status={roverStatus} apiUrl={API_URL} />
        </div>

        {/* Right Column */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
            <StreamPanel camUrl="http://192.168.1.101:81/stream" health={health} /> {/* Update IP */}
            <HistoryPanel apiUrl={API_URL} />
        </div>
      </div>
    </div>
  );
}

export default App;
