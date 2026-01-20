#!/usr/bin/env python3
# coding=utf-8

"""
Linux platform toolchain preparation script.
Supports automatic download and configuration of cross-compilation toolchains for different platforms.
"""

import os
import sys
import ssl
import platform
import urllib.request
import shutil

from tools.util import get_country_code, rm_rf, calc_sha256, extract_archive


# ============================================================================
# Constant Definitions
# ============================================================================

# Toolchain configuration info
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
TOOLCHAIN_CONFIGS = {
    "linux": {
        "url": "https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu.tar.xz",
        "url_cn": "https://images.tuyacn.com/rms-static/96000060-f5df-11f0-9295-ddfc290a59b7-1768900109158.tar.xz?tyName=arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu.tar.xz",
        "sha256": "47aeefc02b0ee39f6d4d1812110952975542d365872a7474b5306924bca4faa1",
        "filename": "arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-linux-gnu.tar.xz",
        "folder": "aarch64-none-linux-gnu-14.2-2024.10",
    },
    "windows": {
        "url": "https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-aarch64-none-linux-gnu.zip",
        "url_cn": "https://images.tuyacn.com/rms-static/726d23c0-f5e0-11f0-946a-f10d211a0860-1768900478972.zip?tyName=arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-aarch64-none-linux-gnu.zip",
        "sha256": "bd5f4808995af2ec647bd0fe8f62815e2c65abcf0558f38f183188d05328d0a0",
        "filename": "arm-gnu-toolchain-14.2.rel1-mingw-w64-i686-aarch64-none-linux-gnu.zip",
        "folder": "aarch64-none-linux-gnu-14.2-2024.10",
    },
}



# ============================================================================
# Utility Functions
# ============================================================================

def get_toolchain_root():
    """Get the root directory for the toolchain."""
    current_dir = os.path.dirname(os.path.abspath(__file__))
    platform_dir = os.path.dirname(current_dir)
    return os.path.join(platform_dir, "tools")


def show_progress(block_num, block_size, total_size):
    """Show download progress bar."""
    if not hasattr(show_progress, "last_progress"):
        show_progress.last_progress = 0
    
    downloaded = block_num * block_size
    if total_size > 0:
        progress = min(100, int((downloaded / total_size) * 100))
        
        if progress != show_progress.last_progress:
            print(f"\rProgress: {progress}%", end="", flush=True)
            show_progress.last_progress = progress
            
            if progress >= 100:
                print()  # New line on completion
                show_progress.last_progress = 0


def download_file(url, file_path):
    """
    Download file from the given URL.
    
    Args:
        url: Download link
        file_path: Save path
        
    Returns:
        bool: Whether the download succeeded
    """
    try:
        print(f"Start downloading: {os.path.basename(file_path)}")
        context = ssl._create_unverified_context()
        urllib.request.urlretrieve(url, file_path, reporthook=show_progress)
        return True
    except Exception as e:
        print(f"\nDownload failed: {e}")
        rm_rf(file_path)
        return False


def verify_file_checksum(file_path, expected_sha256):
    """
    Verify SHA256 checksum of a file.
    
    Args:
        file_path: Path to file
        expected_sha256: Expected SHA256 value
        
    Returns:
        bool: Whether the checksum matched
    """
    if not os.path.exists(file_path):
        return False
    
    actual_sha256 = calc_sha256(file_path)
    print(f"=========================== Actual SHA256: {actual_sha256}")
    print(f"Expected SHA256: {expected_sha256}")
    if not actual_sha256:
        return False
    
    if actual_sha256 == expected_sha256:
        print("✓ File checksum passed")
        return True
    else:
        print("✗ File checksum failed")
        return False


def get_toolchain_config():
    """
    Get toolchain config for current system.
    
    Returns:
        dict: Toolchain configuration, or None if unsupported
    """
    sys_name = platform.system().lower()
    
    # Windows includes multiple variants
    if sys_name.startswith("mingw") or sys_name == "windows":
        sys_name = "windows"
    
    config = TOOLCHAIN_CONFIGS.get(sys_name)
    
    if config:
        # Provide alternative URL for China users if available
        country = get_country_code()
        if country == "China" and "url_cn" in config:
            config = config.copy()
            config["url"] = config["url_cn"]
    
    return config


# ============================================================================
# Main Functionality
# ============================================================================

def download_and_setup_toolchain(toolchain_root):
    """
    Download and set up the toolchain.
    
    Args:
        toolchain_root: Toolchain install root directory
        
    Returns:
        bool: Whether succeeded
    """
    print("=" * 60)
    print("Preparing to download Raspberry Pi toolchain...")
    print("=" * 60)
    
    # Get system-appropriate toolchain config
    config = get_toolchain_config()
    if not config:
        print(f"Unsupported system: {platform.system()}")
        return False
    
    # Make sure toolchain root exists
    os.makedirs(toolchain_root, exist_ok=True)
    
    # Check if toolchain is already installed
    toolchain_folder = os.path.join(toolchain_root, config["folder"])
    if os.path.exists(toolchain_folder):
        print(f"✓ Toolchain already exists: {config['folder']}")
        return True
    
    # Prepare to download
    download_path = os.path.join(toolchain_root, config["filename"])
    
    # If file exists, verify checksum first
    if os.path.exists(download_path):
        print(f"Found downloaded file, verifying...")
        
        if verify_file_checksum(download_path, config["sha256"]):
            print("Skip downloading")
        else:
            print("File corrupted, re-downloading")
            rm_rf(download_path)
            
            if not download_file(config["url"], download_path):
                return False
            
            if not verify_file_checksum(download_path, config["sha256"]):
                rm_rf(download_path)
                return False
    else:
        # Download file
        if not download_file(config["url"], download_path):
            return False
        
        # Verify downloaded file
        if not verify_file_checksum(download_path, config["sha256"]):
            rm_rf(download_path)
            return False
    
    # Record existing directories before extraction
    existing_dirs = set(os.listdir(toolchain_root)) if os.path.exists(toolchain_root) else set()
    
    # Extract toolchain
    print("Extracting toolchain...")
    if not extract_archive(download_path, toolchain_root):
        return False
    
    # Find the newly extracted directory
    current_dirs = set(os.listdir(toolchain_root))
    new_dirs = current_dirs - existing_dirs
    
    # If extracted directory name differs from expected folder name, rename it
    if new_dirs and config["folder"] not in new_dirs:
        extracted_name = list(new_dirs)[0]
        actual_folder = os.path.join(toolchain_root, extracted_name)
        
        if os.path.isdir(actual_folder):
            print(f"Renaming '{extracted_name}' -> '{config['folder']}'")
            if os.path.exists(toolchain_folder):
                rm_rf(toolchain_folder)
            shutil.move(actual_folder, toolchain_folder)
    
    # Check extraction result
    if not os.path.exists(toolchain_folder):
        print(f"Extraction failed: directory not found {config['folder']}")
        return False
    
    print("=" * 60)
    print("✓ Toolchain installed successfully")
    print("=" * 60)
    return True


def prepare_raspberry_pi():
    """Prepare Raspberry Pi platform"""
    toolchain_root = get_toolchain_root()
    return download_and_setup_toolchain(toolchain_root)


def prepare_ubuntu():
    """Prepare Ubuntu platform"""
    print("Ubuntu platform preparation complete")
    return True


# ============================================================================
# Program Entry
# ============================================================================

def main():
    """Main function"""
    if len(sys.argv) < 2:
        print("Error: Missing platform argument")
        sys.exit(1)
    
    platform_chip = sys.argv[1]
    
    # Platform handler map
    platform_handlers = {
        "Raspberry_Pi": prepare_raspberry_pi,
        "Ubuntu": prepare_ubuntu,
    }
    
    handler = platform_handlers.get(platform_chip)
    
    if not handler:
        print(f"Error: Unsupported platform '{platform_chip}'")
        sys.exit(1)
    
    # Execute platform preparation
    if not handler():
        print(f"Error: Platform '{platform_chip}' preparation failed")
        sys.exit(1)
    
    sys.exit(0)


if __name__ == "__main__":
    main()
