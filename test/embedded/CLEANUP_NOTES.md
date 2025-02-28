# Embedded Test Cleanup Notes

## Compatibility and Build Fixes

### Fixed ARM Compiler Specs Conflict
- Removed redundant flags that were causing conflicts
- Removed `-specs=nosys.specs` from toolchain.cmake and added to individual target link flags
- Removed `-lnosys` from common system libraries to avoid duplicate linking
- These changes resolved the "attempt to rename spec 'link_gcc_c_sequence'" error during compilation

### Improved Compatibility Headers
- Added proper compatibility header (`src/util/compat.h`) to handle format macros in a centralized way
- Fixed conditional logic to ensure format macros are always defined for embedded builds
- Added `-DEMBEDDED` definition to identify when building for embedded targets
- Kept `-D__STDC_FORMAT_MACROS` and `-D_POSIX_C_SOURCE=200809L` definitions to ensure proper macro support

## Testing Improvements

### Added MPS2 Cortex-M3 Test

To improve testing reliability with QEMU, we've added a Cortex-M3 specific test that runs on the QEMU MPS2-AN385 board model:

1. **MPS2 Specific Files**:
   - Created a proper Cortex-M3 startup file with vector table in `mps2_startup.s`
   - Added a dedicated linker script `mps2_linker.ld` for the MPS2 board memory layout
   - Configured baremetal test to work with MPS2 UART

2. **Memory Map**:
   - For the MPS2 board, Flash memory starts at 0x00000000
   - RAM starts at 0x20000000
   - Properly aligned vector table at the beginning of Flash
   - Stack in RAM with correct alignment

3. **QEMU Configuration**:
   - Using `-machine mps2-an385` which is designed for baremetal ARM Cortex-M3 applications
   - Improved debug flags for better visibility into execution
   - Capturing logs for both standard output and debug information

This approach provides a much more reliable test environment than using the generic QEMU virt machine, as the MPS2 board model is specifically designed for simple microcontroller applications and doesn't require a complex bootloader. 