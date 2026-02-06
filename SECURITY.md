# Security Policy

## Supported Versions

The following versions of BzPeri are currently supported with security updates:

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |
| 0.0.x   | :x:                |

## Reporting a Vulnerability

We take security vulnerabilities seriously. If you discover a security issue, please report it responsibly.

### How to Report

**Please do NOT report security vulnerabilities through public GitHub issues.**

Instead, please report them via one of these methods:

1. **Email**: Send details to the maintainer (see [GitHub profile](https://github.com/jy1655))
2. **GitHub Security Advisory**: Use [GitHub's private vulnerability reporting](https://github.com/jy1655/BzPeri/security/advisories/new)

### What to Include

Please include the following information in your report:

- Type of vulnerability (e.g., buffer overflow, authentication bypass, etc.)
- Full paths of source file(s) related to the vulnerability
- Location of the affected source code (tag/branch/commit or direct URL)
- Step-by-step instructions to reproduce the issue
- Proof-of-concept or exploit code (if possible)
- Impact assessment

### Response Timeline

- **Initial Response**: Within 48 hours
- **Status Update**: Within 7 days
- **Resolution Target**: Within 30 days (depending on complexity)

### What to Expect

1. **Acknowledgment**: We will acknowledge receipt of your report
2. **Investigation**: We will investigate and validate the issue
3. **Fix Development**: We will develop and test a fix
4. **Disclosure**: We will coordinate disclosure timing with you
5. **Credit**: We will credit you in the security advisory (unless you prefer anonymity)

## Security Considerations for Users

### Bluetooth Security

BzPeri handles Bluetooth Low Energy communications. Consider these security aspects:

1. **Bonding/Pairing**: Enable bonding for secure connections:
   ```cpp
   bzpStartWithBondable("bzperi.app", "name", "short", getter, setter, 30000, 1);
   ```

2. **D-Bus Permissions**: The included D-Bus policy restricts access to root and bluetooth group users only.

3. **BlueZ Configuration**: Running with `--experimental` flag enables additional BlueZ features. Understand the implications for your use case.

### Best Practices

- Run BzPeri applications with minimal required privileges
- Keep BlueZ and system packages updated
- Review D-Bus policy files before deployment
- Use bonding/pairing for sensitive applications
- Monitor system logs for unexpected Bluetooth activity

## Security Updates

Security updates will be released as patch versions (e.g., 0.1.x → 0.1.y) and announced via:

- GitHub Releases
- CHANGELOG.md updates
- GitHub Security Advisories (for critical issues)

## Acknowledgments

We appreciate the security research community's efforts in responsibly disclosing vulnerabilities. Contributors who report valid security issues will be acknowledged (with permission) in our security advisories.
