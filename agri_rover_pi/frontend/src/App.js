import React, { useState, useEffect } from 'react';
import io from 'socket.io-client';
import axios from 'axios';
import SensorPanel from './components/SensorPanel';
import ControlPanel from './components/ControlPanel';
import StreamPanel from './components/StreamPanel';
import HistoryPanel from './components/HistoryPanel';

// FIX: API_URL uses port 5000 to match backend default
const API_URL = `http://${window.location.hostname}:5000`;
const socket = io(API_URL);

function App() {
  // FIX: Initial state field names now match what serial.js emits and sensors API returns
  const [sensors, setSensors] = useState({
    soil_moisture: 0,
    is_raining: false,
    air_temperature: 0,
    soil_temperature: 0,
    humidity: 0,
    obstacle_detected: 0
  });
  const [health, setHealth] = useState(null);
  const [roverStatus, setRoverStatus] = useState({ pump: false, motor: 'stopped' });
  const [time, setTime] = useState(new Date().toLocaleTimeString());

  useEffect(() => {
    fetchData();

    // Update clock every second
    const clock = setInterval(() => setTime(new Date().toLocaleTimeString()), 1000);

    socket.on('sensor_update', (data) => {
      setSensors(data);
    });

    socket.on('rover_update', (data) => {
      setRoverStatus(data);
    });

    socket.on('alert', (data) => {
      alert(`${data.type.toUpperCase()}: ${data.message}`);
    });

    return () => {
      socket.disconnect();
      clearInterval(clock);
    };
  }, []);

  const fetchData = async () => {
    try {
      const sRes = await axios.get(`${API_URL}/api/sensors`);
      setSensors(sRes.data);
      const hRes = await axios.get(`${API_URL}/api/health/current`);
      setHealth(hRes.data);
    } catch (err) {
      console.error('Fetch error:', err);
    }
  };

  return (
    <div className="App" style={{ padding: '20px', background: '#1a1a1a', minHeight: '100vh', color: '#fff' }}>
      <header style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '20px', borderBottom: '1px solid #333', paddingBottom: '10px' }}>
        <h1 style={{ color: '#4caf50', margin: 0 }}>🌱 AgriROV Dashboard</h1>
        <div style={{ color: '#888' }}>{time}</div>
      </header>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 2fr', gap: '20px' }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
          <SensorPanel data={sensors} />
          <ControlPanel status={roverStatus} apiUrl={API_URL} />
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: '20px' }}>
          {/* FIX: cam URL uses window.location.hostname so it works on any network */}
          <StreamPanel camUrl={`http://${window.location.hostname}:81/stream`} health={health} />
          <HistoryPanel apiUrl={API_URL} />
        </div>
      </div>
    </div>
  );
}

export default App;
