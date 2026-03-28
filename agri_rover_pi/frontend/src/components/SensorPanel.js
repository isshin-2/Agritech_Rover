import React from 'react';

const Card = ({ title, value, unit, color }) => (
    <div style={{ background: '#2d2d2d', padding: '15px', borderRadius: '8px', textAlign: 'center' }}>
        <h3 style={{ margin: '0 0 10px 0', fontSize: '0.9rem', color: '#888' }}>{title}</h3>
        <div style={{ fontSize: '1.5rem', fontWeight: 'bold', color: color || '#fff' }}>
            {value} <span style={{ fontSize: '0.8rem', color: '#666' }}>{unit}</span>
        </div>
    </div>
);

export default function SensorPanel({ data }) {
    // FIX: Use correct field names matching serial.js emit and sensors API response
    // Previously used data.temperature (undefined) — now uses air_temperature and soil_temperature
    const moisture = typeof data.soil_moisture === 'number' ? data.soil_moisture.toFixed(1) : '–';
    const airTemp = typeof data.air_temperature === 'number' ? data.air_temperature.toFixed(1) : '–';
    const soilTemp = typeof data.soil_temperature === 'number' ? data.soil_temperature.toFixed(1) : '–';
    const humidity = typeof data.humidity === 'number' ? data.humidity : (typeof data.air_humidity === 'number' ? data.air_humidity : '–');

    return (
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '10px' }}>
            <Card
                title="Soil Moisture"
                value={moisture}
                unit="%"
                color={data.soil_moisture < 30 ? '#ff4444' : '#4caf50'}
            />
            <Card
                title="Soil Temp"
                value={soilTemp}
                unit="°C"
                color="#ff9800"
            />
            <Card
                title="Air Temp"
                value={airTemp}
                unit="°C"
                color="#ff9800"
            />
            <Card
                title="Humidity"
                value={humidity}
                unit="%"
                color="#00bcd4"
            />
            <Card
                title="Rain Status"
                value={data.is_raining ? "RAINING" : "CLEAR"}
                unit=""
                color={data.is_raining ? "#2196f3" : "#4caf50"}
            />

            {data.obstacle_detected ? (
                <div style={{
                    gridColumn: '1 / -1',
                    background: '#d32f2f',
                    color: 'white',
                    padding: '10px',
                    borderRadius: '8px',
                    textAlign: 'center',
                    fontWeight: 'bold'
                }}>
                    🛑 OBSTACLE DETECTED
                </div>
            ) : null}
        </div>
    );
}
