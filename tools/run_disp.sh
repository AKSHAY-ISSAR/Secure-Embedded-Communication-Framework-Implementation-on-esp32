#!/bin/bash

# Define the session name and serial port
SESSION_NAME="esp_link"
PORT=${1:-/dev/ttyUSB1} # Defaults to ttyUSB1 if no argument is provided
BAUD=115200

# Create the log file if it doesn't exist so 'tail' doesn't fail
touch telemetry.log

# Check if the tmux session already exists
tmux has-session -t $SESSION_NAME 2>/dev/null

if [ $? != 0 ]; then
    # 1. Create a new detached tmux session
    tmux new-session -d -s $SESSION_NAME

    # 2. Start the log viewer in the top pane (Pane 0)
    tmux send-keys -t $SESSION_NAME:0 "tail -f telemetry.log" C-m

    # 3. Split the window vertically (creates a top/bottom layout)
    tmux split-window -v -t $SESSION_NAME:0
    
    sleep 1
    # 4. Launch the Python frontend in the bottom pane (Pane 1)
    tmux send-keys -t $SESSION_NAME:1 "python3 tools/display_front.py $PORT $BAUD" C-m
    
    # 5. Focus the bottom pane so you can type immediately
    tmux select-pane -t $SESSION_NAME:1
fi

# Attach to the session
tmux attach -t $SESSION_NAME