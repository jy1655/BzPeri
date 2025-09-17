# Packet Parser Testing

This directory contains test programs to validate the robustness of Gobbledegook's packet parsing logic.

## Running Tests

After building the project with `./configure && make`, you can run the packet parser tests:

```bash
./src/packet_parser_tests
```

## Test Coverage

The packet parser tests cover:

### 1. Valid Packet Parsing
- CommandCompleteEvent parsing with valid data
- CommandStatusEvent parsing with valid data
- DeviceConnectedEvent parsing with valid data
- DeviceDisconnectedEvent parsing with valid data

### 2. Malformed Input Handling
- Empty packets
- Undersized packets
- Single-byte packets
- Oversized packets

### 3. Boundary Conditions
- Minimum valid packet sizes
- Maximum reasonable packet sizes
- Edge cases in packet structure parsing

### 4. Memory Safety
- Validation that parsing doesn't crash with malformed input
- Buffer bounds checking
- Safe memory operations (memcpy vs reinterpret_cast)

## Expected Output

When all tests pass, you should see:
```
Running packet parser tests...
Testing CommandCompleteEvent parsing...
  Valid packet parsing: PASS
  Short packet handling: PASS
Testing CommandStatusEvent parsing...
  Valid packet parsing: PASS
[... more test output ...]
All tests PASSED
```

## Adding New Tests

To add tests for new packet types:

1. Add a new test method to the `PacketParserTests` class
2. Call it from `runAllTests()`
3. Follow the pattern of testing both valid and invalid inputs

## Integration with CI/CD

This test program returns:
- Exit code 0 if all tests pass
- Exit code 1 if any test fails

This makes it suitable for integration with continuous integration systems.