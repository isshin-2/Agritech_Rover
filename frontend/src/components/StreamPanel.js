import React from 'react';

export default function StreamPanel({ camUrl, health }) {
    return (
        <div style={{ background: '#2d2d2d', padding: '15px', borderRadius: '8px', height: '100%' }}>
            <h3 style={{ marginTop: 0 }}>📷 Live Feed</h3>
            <div style={{ width: '100%', height: '300px', background: '#000', borderRadius: '4px', overflow: 'hidden', position: 'relative' }}>
                <img src={camUrl} alt="Live Stream" style={{ width: '100%', height: '100%', objectFit: 'contain' }} onError={(e) => {e.target.style.display='none'}} />
                <div style={{ position: 'absolute', top: '50%', left: '50%', transform: 'translate(-50%, -50%)', color: '#555' }}>Stream Offline</div>
                
                {health && (
                    <div style={{ position: 'absolute', bottom: '10px', left: '10px', background: 'rgba(0,0,0,0.7)', padding: '8px', borderRadius: '4px' }}>
                        <div style={{ fontWeight: 'bold', color: health.status === 'Healthy' ? '#4caf50' : '#f44336' }}>
                            {health.status.toUpperCase()}
                        </div>
                        <div style={{ fontSize: '0.8rem' }}>{health.disease_type} ({health.confidence}%)</div>
                    </div>
                )}
            </div>
            
            {health && health.status !== 'Healthy' && (
                <div style={{ marginTop: '10px', padding: '10px', background: '#3e2723', borderRadius: '4px', fontSize: '0.9rem' }}>
                    <strong>Treatment:</strong> {health.solutions}
                </div>
            )}
        </div>
    );
}
