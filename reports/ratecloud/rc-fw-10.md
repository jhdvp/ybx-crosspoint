# rc-fw-10 — WIP: FirmwareBoardTag scanner host fixtures

## Preserved work
`test/firmware_board_tag_scanner/` has a CMake target and GTest cases for board tag handling: allowed untagged image, matching/mismatching name, chunk splits, partial magic, invalid name and sticky mismatch.

## Important caveat
This test folder was created at deadline and is NOT referenced by root `test/CMakeLists.txt`. No verified green test run or compile is available. Do not describe these tests as passing. Existing scanner policy accepts untagged images; assess if this is correct for YBX before claiming OTA security coverage.

## Next developer
1. Review behavior vs current FirmwareBoardTag implementation.
2. Wire in root `test/CMakeLists.txt` or standalone host harness.
3. Compile and run; correct any fixtures that do not match the intended policy.
4. Verify both X3/X4/X4Pro board names and OTA integration; no firmware flash was performed.

Branch: `agents/ratecloud/rc-fw-10`. Not approved for merge.
