import React from 'react';
import axios from 'axios';

export default function ControlPanel({ apiUrl, status }) {
    const sendPump = async (action) => {
        try { await axios.post(`${apiUrl}/api/rover/pump/control`, { action }); }
        catch (err) { alert(err.message); }
    };

    const sendMotor = async (cmd) => {
        try { await axios.post(`${apiUrl}/api/rover/motor/command`, { command: cmd }); }
        catch (err) { alert(err.message); }
    };

    // FIX: Display current pump & motor status from the `status` prop (was received but never used)
    const pumpOn = status && status.pump;
    const motorState = (status && status.motor) || 'stopped';

    const motorColor = (state) => ({
        forward: '#4caf50',
        backward: '#ff9800',
        stopped: '#555'
    }[state] || '#555');

    return (
        <div style={{ background: '#2d2d2d', padding: '15px', borderRadius: '8px' }}>
            <h3 style={{ marginTop: 0 }}>🚜 Controls</h3>

            {/* Pump Controls */}
            <div style={{ marginBottom: '20px' }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '5px' }}>
                    <label>Pump:</label>
                    <span style={{
                        fontSize: '0.8rem',
                        padding: '2px 8px',
                        borderRadius: '10px',
                        background: pumpOn ? '#1565c0' : '#444',
                        color: '#fff'
                    }}>
                        {pumpOn ? '● ON' : '○ OFF'}
                    </span>
                </div>
                <div style={{ display: 'flex', gap: '10px' }}>
                    <button
                        onClick={() => sendPump('on')}
                        style={{ flex: 1, padding: '10px', background: pumpOn ? '#1565c0' : '#2196f3', border: 'none', color: 'white', borderRadius: '4px', cursor: 'pointer' }}
                    >ON</button>
                    <button
                        onClick={() => sendPump('off')}
                        style={{ flex: 1, padding: '10px', background: pumpOn ? '#555' : '#333', border: 'none', color: 'white', borderRadius: '4px', cursor: 'pointer' }}
                    >OFF</button>
                </div>
            </div>

            {/* Motor Controls */}
            <div>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '5px' }}>
                    <label>Motors:</label>
                    <span style={{
                        fontSize: '0.8rem',
                        padding: '2px 8px',
                        borderRadius: '10px',
                        background: motorColor(motorState),
                        color: '#fff'
                    }}>
                        {motorState.toUpperCase()}
                    </span>
                </div>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: '5px' }}>
                    <button onClick={() => sendMotor('forward')} style={{ padding: '10px', background: '#388e3c', border: 'none', color: 'white', borderRadius: '4px', cursor: 'pointer' }}>▲ Fwd</button>
                    <button onClick={() => sendMotor('stop')} style={{ padding: '10px', background: '#b71c1c', border: 'none', color: 'white', borderRadius: '4px', cursor: 'pointer' }}>⬛ Stop</button>
                    <button onClick={() => sendMotor('backward')} style={{ padding: '10px', background: '#f57c00', border: 'none', color: 'white', borderRadius: '4px', cursor: 'pointer' }}>▼ Rev</button>
                </div>
            </div>
        </div>
    );
}
