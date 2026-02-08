# Pull Request

## Description
<!-- Provide a brief description of your changes -->

## Related Issue
<!-- Link to related issue (if applicable) -->
Fixes #(issue number)

## Type of Change
<!-- Mark the relevant option with an "x" -->
- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update
- [ ] Performance improvement
- [ ] Code refactoring

## Changes Made
<!-- List the changes you've made -->
-
-
-

## Testing
<!-- Describe the tests you ran to verify your changes -->

### Hardware Testing
- [ ] Tested on GUITION JC4880P433C (ESP32-P4 + ESP32-C6)
- [ ] Tested on other hardware: _______________

### Functionality Testing
- [ ] Device discovery works
- [ ] Playback control works (play/pause/skip)
- [ ] Volume control works
- [ ] Album art loads correctly
- [ ] No crashes during normal use
- [ ] Tested for at least 30 minutes

### Edge Cases
- [ ] Source switching (music → radio → TV)
- [ ] WiFi reconnection
- [ ] Rapid track skipping
- [ ] Memory leak check (monitored free heap)

## Serial Logs
<!-- Include relevant serial output showing your changes work -->
```
Paste serial logs here
```

## Screenshots
<!-- If applicable, add screenshots showing the changes -->

## Memory Impact
<!-- If you modified code that affects memory usage -->
- **Before**: DRAM: X%, Flash: Y%
- **After**: DRAM: X%, Flash: Y%
- **Change**: +/- X bytes

## Checklist
- [ ] My code follows the project's code style
- [ ] I have performed a self-review of my code
- [ ] I have commented my code, particularly in hard-to-understand areas
- [ ] I have made corresponding changes to the documentation
- [ ] My changes generate no new compiler warnings
- [ ] I have tested my changes on actual hardware
- [ ] I have checked for memory leaks
- [ ] I have added CRITICAL comments for SDIO/mutex/memory-sensitive code
- [ ] I have verified no SDIO crashes occur with my changes

## Additional Notes
<!-- Any additional information that reviewers should know -->
