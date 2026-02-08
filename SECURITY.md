# Security Policy

## Supported Versions

We release patches for security vulnerabilities for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| 1.2.x   | :white_check_mark: |
| 1.1.x   | :x:                |
| < 1.1   | :x:                |

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you discover a security vulnerability within SonosESP, please send an email to the project maintainer or create a **private security advisory** on GitHub:

1. Go to the **Security** tab
2. Click **Report a vulnerability**
3. Fill in the details

### What to Include

When reporting a vulnerability, please include:

- **Type of issue** (e.g., buffer overflow, SQL injection, cross-site scripting, etc.)
- **Full paths** of source file(s) related to the issue
- **Location** of the affected source code (tag/branch/commit or direct URL)
- **Step-by-step instructions** to reproduce the issue
- **Proof-of-concept or exploit code** (if possible)
- **Impact** of the issue, including how an attacker might exploit it

### What to Expect

- You should receive an acknowledgment within **48 hours**
- We will send a more detailed response within **7 days** indicating next steps
- We will keep you informed of the progress towards a fix
- We may ask for additional information or guidance

## Security Considerations for Deployment

### Network Security

**WiFi Credentials:**
- WiFi credentials are stored in NVS (Non-Volatile Storage)
- Credentials are NOT encrypted in NVS
- ⚠️ **Do not share firmware dumps** as they contain your WiFi password
- ⚠️ **Factory reset** before disposing of hardware

**Network Traffic:**
- Album art from public CDNs uses HTTP (not HTTPS) for performance
- Sonos SOAP API uses HTTP over local network (Sonos limitation)
- Lyrics API uses HTTPS with certificate validation disabled (performance trade-off)
- OTA updates use HTTPS with certificate validation disabled

**Recommendations:**
- Use a secure, private WiFi network
- Do not expose the device to untrusted networks
- Consider network segmentation (IoT VLAN)

### Physical Security

**Flash Memory:**
- Firmware contains WiFi credentials in plaintext
- Enable flash encryption in production deployments (performance impact)
- Use secure boot for critical deployments

**Serial Access:**
- Serial port provides full system access
- Disable serial output in production builds if needed
- Physical access = full compromise

### OTA Updates

**Update Security:**
- OTA downloads use HTTPS but skip certificate validation
- ⚠️ **Verify** download URLs before updating
- Updates are **not cryptographically signed** (ESP-IDF limitation in Arduino framework)

**Recommendations:**
- Only update from official GitHub releases
- Verify firmware file hashes if provided
- Use a trusted network for updates

### Code Security

**Known Limitations:**
- No authentication on device (physical access = control)
- No encryption of stored data (NVS)
- Sonos API does not use authentication (local network trust model)
- LVGL UI does not sanitize all input

**Best Practices:**
- Keep firmware up to date
- Monitor serial logs for suspicious activity
- Report suspicious behavior immediately

## Disclosure Policy

- We will publish security advisories for accepted vulnerabilities
- Credit will be given to the reporter (unless anonymity is requested)
- We aim to release patches within 30 days of a verified report
- We follow coordinated disclosure - please allow time for a patch before public disclosure

## Comments on This Policy

If you have suggestions on how this process could be improved, please submit a pull request or open an issue.

---

Last updated: 2025-01-XX
