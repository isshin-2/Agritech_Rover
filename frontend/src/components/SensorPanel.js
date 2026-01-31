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
    return (
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '10px' }}>
            <Card title="Soil Moisture" value={data.soil_moisture} unit="%" color={data.soil_moisture < 30 ? '#ff4444' : '#4caf50'} />
            <Card title="Soil Temp" value={data.soil_temperature} unit="°C" color="#ff9800" />
            <Card title="Air Temp" value={data.air_temperature} unit="°C" color="#ff9800" />
            <Card title="Humidity" value={data.air_humidity} unit="%" color="#00bcd4" />
            <Card title="Rain Status" value={data.is_raining ? "RAINING" : "CLEAR"} unit="" color={data.is_raining ? "#2196f3" : "#4caf50"} />
            
            {data.obstacle_detected ? (
                <div style={{ gridColumn: '1 / -1', background: '#d32f2f', color: 'white', padding: '10px', borderRadius: '8px', textAlign: 'center', fontWeight: 'bold' }}>
                    🛑 OBSTACLE DETECTED
                </div>
            ) : null}
        </div>
    );
}
