#!/bin/bash

SESSION="gpu_monitor"

# Start a new detached tmux session
tmux new-session -d -s $SESSION

# First pane (top): Intel Dg2 @ card2
tmux send-keys -t $SESSION "intel_gpu_top -d pci:vendor=8086,device=56A6,card=0" C-m

# Split bottom (pane 1) from top (pane 0)
tmux split-window -v -t $SESSION

# Second pane (middle): Intel Alderlake_s @ card1
tmux send-keys -t $SESSION:0.1 "intel_gpu_top -d pci:vendor=8086,device=4680,card=0" C-m

# Split bottom (pane 2) from middle (pane 1)
tmux split-window -v -t $SESSION

# Third pane (bottom): Intel Dg2 @ card0
tmux send-keys -t $SESSION:0.2 "intel_gpu_top -d pci:vendor=8086,device=56A6,card=1" C-m

# Make panes evenly sized vertically
tmux select-layout -t $SESSION even-vertical

# Attach to the session
tmux attach-session -t $SESSION