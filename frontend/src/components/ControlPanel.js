import React from 'react';
import axios from 'axios';

export default function ControlPanel({ apiUrl }) {
    const sendPump = async (action) => {
        try { await axios.post(`${apiUrl}/api/rover/pump/control`, { action }); } catch(err) { alert(err.message); }
    };
    
    const sendMotor = async (cmd) => {
        try { await axios.post(`${apiUrl}/api/rover/motor/command`, { command: cmd }); } catch(err) { alert(err.message); }
    };

    return (
        <div style={{ background: '#2d2d2d', padding: '15px', borderRadius: '8px' }}>
            <h3>🚜 Controls</h3>
            
            <div style={{ marginBottom: '20px' }}>
                <label>Pump:</label>
                <div style={{ display: 'flex', gap: '10px', marginTop: '5px' }}>
                    <button onClick={() => sendPump('on')} style={{ flex: 1, padding: '10px', background: '#2196f3', border: 'none', color: 'white', borderRadius: '4px', cursor: 'pointer' }}>ON</button>
                    <button onClick={() => sendPump('off')} style={{ flex: 1, padding: '10px', background: '#555', border: 'none', color: 'white', borderRadius: '4px', cursor: 'pointer' }}>OFF</button>
                </div>
            </div>

            {/* Motor Controls Removed by User Request */}
        </div>
    );
}
