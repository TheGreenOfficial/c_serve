# c_serve 🚀

A lightweight, high-performance web server written in C with built-in PHP support. Designed for production use with security, performance, and simplicity in mind.

![Web Server](https://img.shields.io/badge/Web-Server-brightgreen)
![C Language](https://img.shields.io/badge/Language-C-blue)
![PHP Support](https://img.shields.io/badge/PHP-Supported-777bb3)
![License](https://img.shields.io/badge/License-MIT-green)

## ✨ Features

- **🚀 High Performance** - Fork-based architecture for concurrent connections
- **🔒 Security First** - Path traversal protection and input sanitization
- **🐘 PHP Support** - Built-in PHP-CGI execution for dynamic content
- **📁 Static File Serving** - Automatic MIME type detection
- **📂 Directory Listing** - Clean file browser when no index file found
- **🌐 HTTP/1.1 Compliant** - Full protocol support
- **🦄 Production Ready** - Daemon mode, signal handling, and error recovery
- **⚡ Lightweight** - Minimal dependencies, maximum efficiency

## 🛠️ Installation

### Prerequisites
- GCC compiler
- PHP-CLI (for PHP support)
- Linux/Unix system

### Compilation

```bash
git clone https://github.com/TheGreenOfficial/c_serve.git
cd c_serve
gcc c_serve.c -o c_serve
```

### Quick Start

```bash
# Serve current directory on port 8080
./c_serve

# Serve specific directory
./c_serve --root /var/www --port 80

# Run as background daemon
./c_serve --root /var/www --port 80 --daemon
```

## 📖 Usage

### Basic Usage

```bash
./c_serve [OPTIONS]
```

### Command Line Options

| Option    | Short | Description                        | Default       |
|-----------|-------|------------------------------------|---------------|
| --root    | -r    | Root directory to serve            | Current dir   |
| --port    | -p    | Port to listen on                  | 8080          |
| --daemon  | -d    | Run as background daemon          | False         |
| --help    | -h    | Show help message                  | -             |

### Examples

```bash
# Development server
./c_serve

# Production server on port 80
./c_serve -r /var/www -p 80 -d

# Custom web root
./c_serve --root /home/user/website --port 3000
```

## 🏗️ Architecture

`c_serve` uses a pre-fork architecture where each incoming connection is handled by a separate child process, providing:

- **Isolation** - Process separation for security
- **Concurrency** - Multiple simultaneous connections
- **Stability** - Child process crashes don't affect the main server

## 🔧 Configuration

### Index File Priority
The server looks for index files in this order:

1. index.php
2. index.html
3. index.htm

### Supported File Types

- **Text**: .html, .htm, .css, .js, .txt
- **Images**: .png, .jpg, .jpeg, .gif
- **Data**: .json, .pdf
- **PHP**: .php (requires PHP-CLI)

### Security Features

- Path traversal protection (../ blocking)
- URL encoding/decoding
- Input sanitization
- Safe file path construction
- Process isolation

## 🐘 PHP Support

`c_serve` automatically executes PHP files using the system's `php` command. Ensure PHP-CLI is installed:

```bash
# Ubuntu/Debian
sudo apt install php-cli

# CentOS/RHEL
sudo yum install php-cli

# Verify installation
php --version
```

## 📊 Performance

`c_serve` is optimized for:

- Low memory footprint
- Fast static file serving
- Efficient PHP execution
- Minimal context switching

## 🚨 Error Handling

The server provides appropriate HTTP status codes:

- **200 OK** - Successful request
- **404 Not Found** - File not found
- **500 Internal Error** - Server error
- **501 Not Implemented** - Unsupported method
- **414 URI Too Long** - Path exceeds limits

## 🔍 Logging

When running in foreground mode, `c_serve` displays:

- Server startup information
- Connection details
- Error messages
- Access patterns

## 🐛 Troubleshooting

### Common Issues

- **Port already in use**:
  ```bash
  Error: bind: Address already in use
  ```
  **Solution**: Use a different port or wait for the current one to free up.

- **PHP not found**:
  ```bash
  PHP execution failed
  ```
  **Solution**: Install `php-cli` package.

- **Permission denied**:
  ```bash
  Error: bind: Permission denied
  ```
  **Solution**: Use ports above 1024 or run as root.

- **Directory not found**:
  ```bash
  Error: Root directory does not exist
  ```
  **Solution**: Check the path with `--root` option.

## 🤝 Contributing

We welcome contributions! Please feel free to submit pull requests, report bugs, or suggest new features.

### Development Setup

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

### Code Style

- Follow standard C conventions
- Include error handling
- Add comments for complex logic
- Test with various file types and sizes

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🙏 Acknowledgments

- Inspired by lightweight web servers like lighttpd and nginx
- Built with security and performance as primary goals
- Community feedback and contributions

## 📞 Support

If you encounter any issues or have questions:

- Check the troubleshooting section
- Search existing GitHub issues
- Create a new issue with detailed information

`c_serve` - Because sometimes you just need a simple, fast, and reliable web server. 🎯
