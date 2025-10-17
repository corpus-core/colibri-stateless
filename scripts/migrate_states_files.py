#!/usr/bin/env python3
"""
Migration script for states_* files in test/data/

This script converts states_* files from the old format (48 bytes per block)
to the new compact format (0x01 header + 4 bytes period per block).

Old structure (48 bytes per block):
    - slot_bytes[8]       (offset 0-7)
    - blockhash[32]       (offset 8-39)
    - period_bytes[4]     (offset 40-43)
    - flags[4]            (offset 44-47)

New structure:
    - First byte: 0x01 (version marker)
    - Then: period_bytes[4] for each block, concatenated
    - Optional: additional bytes after blocks (preserved)
"""

import sys
from pathlib import Path
from typing import List, Tuple


def find_states_files(base_dir: Path) -> List[Path]:
    """Find all states_* files in the base directory and subdirectories."""
    return list(base_dir.rglob("states_*"))


def migrate_file(file_path: Path) -> Tuple[bool, str]:
    """
    Migrate a single states_* file from old to new format.
    
    Returns:
        Tuple of (success, message)
    """
    try:
        # Read the original file
        with open(file_path, 'rb') as f:
            old_data = f.read()
        
        file_len = len(old_data)
        
        # Calculate number of blocks
        block_count = (file_len - (file_len % 48)) // 48
        
        if block_count == 0:
            return False, f"File too small or empty (only {file_len} bytes)"
        
        # Calculate expected size (blocks * 48 + optional extra bytes)
        blocks_size = block_count * 48
        extra_bytes = old_data[blocks_size:] if file_len > blocks_size else b''
        
        # Extract period_bytes from each block
        period_bytes_list = []
        for i in range(block_count):
            block_offset = i * 48
            # period_bytes are at offset 40-43 within each block
            period_start = block_offset + 40
            period_end = period_start + 4
            period_bytes = old_data[period_start:period_end]
            period_bytes_list.append(period_bytes)
        
        # Build new file content
        new_data = bytearray()
        new_data.append(0x01)  # Version marker
        for period_bytes in period_bytes_list:
            new_data.extend(period_bytes)
        # Append any extra bytes that were after the blocks
        new_data.extend(extra_bytes)
        
        # Create backup of original file
        backup_path = file_path.with_suffix(file_path.suffix + '.backup')
        with open(backup_path, 'wb') as f:
            f.write(old_data)
        
        # Write new file
        with open(file_path, 'wb') as f:
            f.write(new_data)
        
        old_size = file_len
        new_size = len(new_data)
        extra_info = f" (+{len(extra_bytes)} extra bytes)" if extra_bytes else ""
        
        return True, f"{block_count} blocks, {old_size} -> {new_size} bytes{extra_info}"
    
    except Exception as e:
        return False, f"Error: {str(e)}"


def main():
    """Main migration function."""
    # Get the script directory and navigate to test/data
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    test_data_dir = repo_root / "test" / "data"
    
    if not test_data_dir.exists():
        print(f"Error: test/data directory not found at {test_data_dir}")
        sys.exit(1)
    
    # Find all states_* files
    print(f"Scanning for states_* files in {test_data_dir}...")
    states_files = find_states_files(test_data_dir)
    
    if not states_files:
        print("No states_* files found.")
        sys.exit(0)
    
    print(f"Found {len(states_files)} states_* file(s)\n")
    
    # Migrate each file
    success_count = 0
    error_count = 0
    
    for file_path in sorted(states_files):
        relative_path = file_path.relative_to(test_data_dir)
        success, message = migrate_file(file_path)
        
        if success:
            print(f"✓ {relative_path}: {message}")
            success_count += 1
        else:
            print(f"✗ {relative_path}: {message}")
            error_count += 1
    
    # Summary
    print(f"\n{'=' * 60}")
    print(f"Migration complete:")
    print(f"  Successfully migrated: {success_count}")
    print(f"  Errors: {error_count}")
    print(f"  Total files: {len(states_files)}")
    print(f"\nBackup files created with .backup suffix")
    
    if error_count > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()

