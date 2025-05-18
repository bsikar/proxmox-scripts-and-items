#!/bin/bash

# IP address of the TrueNAS server (using the NFS NIC)
TRUENAS_NFS_IP="10.10.10.1"

# Array of mount configurations:
# "arbitrary_name_for_logging:local_mount_point:nfs_share_path_on_truenas"
MOUNT_CONFIGS=(
  "proxmox-configs:/nfs/mnt/raid-z2/configs:/mnt/raid-z2/configs"
  "proxmox-media:/nfs/mnt/raid-z2/media-and-fetching:/mnt/raid-z2/media-and-fetching"
)

# --- Script Functions ---

# Function to check if TrueNAS is reachable via the NFS NIC
check_truenas_nfs() {
  local ip=$1
  echo "[INFO] Pinging TrueNAS NFS NIC at $ip..."
  if ping -c 1 -W 3 $ip &> /dev/null; then
    echo "[INFO] TrueNAS NFS NIC at $ip is responsive."
    return 0
  else
    echo "[ERROR] TrueNAS NFS NIC at $ip is NOT reachable. Please check network and firewall."
    return 1
  fi
}

# Function to mount the NFS share
mount_nfs_share() {
  local name_for_log=$1
  local local_mount_point=$2
  local nfs_share_path=$3 # This is the path on the NFS server
  local nfs_server_ip=$4

  echo "[INFO] Processing mount: $name_for_log"

  # Check if the local mount point directory exists, create if not
  if [ ! -d "$local_mount_point" ]; then
    echo "[INFO] Local mount point '$local_mount_point' does not exist. Creating it..."
    # Create parent directories as well if they don't exist
    sudo mkdir -p "$local_mount_point"
    if [ $? -ne 0 ]; then
      echo "[ERROR] Failed to create directory '$local_mount_point'. Please check permissions."
      return 1 # Return an error code from this function
    fi
  else
    echo "[INFO] Local mount point '$local_mount_point' already exists."
  fi

  # Check if the directory is already mounted
  if mountpoint -q "$local_mount_point"; then
    echo "[INFO] '$local_mount_point' is already mounted (for $name_for_log)."
  else
    echo "[INFO] Attempting to mount: NFS share '$nfs_server_ip:$nfs_share_path' to '$local_mount_point'"
    # Mount the NFS share
    # Adding 'sudo' as mounting typically requires root privileges. Remove if running script as root.
    sudo mount -t nfs "$nfs_server_ip:$nfs_share_path" "$local_mount_point"
    if [ $? -eq 0 ]; then
      echo "[SUCCESS] Successfully mounted '$nfs_server_ip:$nfs_share_path' to '$local_mount_point' (for $name_for_log)."
    else
      echo "[ERROR] Failed to mount '$nfs_server_ip:$nfs_share_path' to '$local_mount_point' (for $name_for_log)."
      echo "[DETAIL] Attempted command: sudo mount -t nfs \"$nfs_server_ip:$nfs_share_path\" \"$local_mount_point\""
      echo "[HINT] Check NFS server export settings for '$nfs_share_path', network connectivity, and ensure 'nfs-common' (or equivalent) is installed on Proxmox."
      return 1 # Return an error code from this function
    fi
  fi
  return 0 # Success
}

# --- Main Script Logic ---

echo "[PROCESS] Starting NFS mount script..."

# Check reachability of the TrueNAS NFS NIC
if ! check_truenas_nfs "$TRUENAS_NFS_IP"; then
  echo "[FATAL] Cannot proceed with mounting as TrueNAS NFS NIC is unreachable. Exiting."
  exit 1
fi

all_mounts_succeeded=true

# Loop through all mount configurations
for mount_config in "${MOUNT_CONFIGS[@]}"; do
  IFS=':' read -r LOG_NAME LOCAL_MOUNT NFS_PATH <<< "$mount_config"

  echo "---" # Separator for clarity between mount operations

  # Attempt to mount the share
  if ! mount_nfs_share "$LOG_NAME" "$LOCAL_MOUNT" "$NFS_PATH" "$TRUENAS_NFS_IP"; then
    all_mounts_succeeded=false
    echo "[WARNING] Mount operation for '$LOG_NAME' failed. See errors above."
    # Decide if you want to stop on first error or try all
    # To stop on first error, uncomment the next line:
    # exit 1
  fi
done

echo "---"

if $all_mounts_succeeded; then
  echo "[PROCESS] NFS mount script finished. All configured mounts processed successfully."
  exit 0
else
  echo "[PROCESS] NFS mount script finished, but one or more mount operations failed. Please review the output."
  exit 1
fi