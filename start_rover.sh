#!/bin/bash

# AgriROV Startup Script

echo "🚜 Starting AgriROV System..."

# Function to kill process on a port
kill_port() {
  PORT=$1
  PID=$(lsof -t -i:$PORT)
  if [ -n "$PID" ]; then
    echo "Killing process on port $PORT (PID: $PID)..."
    kill -9 $PID
  fi
}

# Cleanup existing processes
echo "Cleaning up ports 5000 and 3000..."
kill_port 5000
kill_port 3000

# Start Backend
echo "🚀 Starting Backend (Port 5000)..."
cd backend
if [ ! -d "node_modules" ]; then
    echo "Installing Backend Dependencies..."
    npm install
fi
# Start in background using nohup to keep running after shell closes, or just &
# Using & for now as it's a simple script. 
# In production, use pm2: pm2 start server.js --name agri_backend
node server.js > ../backend.log 2>&1 &
BACKEND_PID=$!
echo "Backend running (PID: $BACKEND_PID)"

# Wait for backend to be ready
sleep 2

# Start Frontend
echo "💻 Starting Frontend (Port 3000)..."
cd ../frontend
if [ ! -d "node_modules" ]; then
    echo "Installing Frontend Dependencies..."
    npm install --legacy-peer-deps
fi

# Serve the build using 'serve' or just run dev for simplicity in this phase
# For production build:
# npm run build
# npx serve -s build -l 3000 > ../frontend.log 2>&1 &

# For User's request of "simplified startup", running dev mode is often easier to debug initially.
# But let's verify if they have 'serve'. 
# Let's stick to 'npm start' which usually runs 'react-scripts start'.
# We need to make sure BROWSER=none is set so it doesn't try to open a browser window on the remote Pi if not needed.
BROWSER=none npm start > ../frontend.log 2>&1 &
FRONTEND_PID=$!
echo "Frontend running (PID: $FRONTEND_PID)"

echo "✅ AgriROV System Started!"
echo "Backend Logs: tail -f backend.log"
echo "Frontend Logs: tail -f frontend.log"
