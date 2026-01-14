# Contributing to ESP32-P4 Sonos Controller

Thank you for considering contributing to this project! 🎉

## How to Contribute

### Reporting Bugs

If you find a bug, please create an issue with:
- Clear description of the problem
- Steps to reproduce
- Expected vs actual behavior
- Hardware version (if applicable)
- Serial output logs (if available)

### Suggesting Features

Feature requests are welcome! Please:
- Check if the feature is already requested
- Describe the feature and its use case
- Explain why it would be valuable

### Pull Requests

1. **Fork the repository**

2. **Create a feature branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **Make your changes**
   - Follow existing code style
   - Add comments for complex logic
   - Test thoroughly on hardware

4. **Commit your changes**
   ```bash
   git commit -m "Add: your feature description"
   ```

   Use conventional commits:
   - `Add:` for new features
   - `Fix:` for bug fixes
   - `Update:` for improvements
   - `Refactor:` for code restructuring
   - `Docs:` for documentation

5. **Push to your fork**
   ```bash
   git push origin feature/your-feature-name
   ```

6. **Open a Pull Request**
   - Describe what changed and why
   - Reference any related issues
   - Include screenshots for UI changes

## Code Guidelines

### Style
- Use clear, descriptive variable names
- Comment non-obvious code sections
- Keep functions focused and reasonably sized
- Use `const` where appropriate

### Architecture
- FreeRTOS tasks for concurrent operations
- Mutex protection for shared resources
- PSRAM for large allocations (album art)
- vTaskDelay() instead of delay() in tasks

### Testing
- Test on actual ESP32-P4 hardware
- Verify WiFi connectivity
- Check memory usage (heap, PSRAM)
- Test with multiple Sonos devices if possible

## Development Setup

1. Install [PlatformIO](https://platformio.org/)
2. Clone your fork
3. Copy `include/credentials.h.example` to `include/credentials.h`
4. Build and upload: `pio run --target upload`

## Questions?

Feel free to open an issue for discussion!

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
