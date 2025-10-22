#!/bin/bash
# Check Colibri Server installation on macOS

echo "🔍 Colibri Server Installation Check"
echo "======================================"
echo ""

# 1. Check Binary
echo "1. Binary Location"
echo "------------------"
if [ -f "/usr/local/bin/colibri-server" ]; then
    echo "✅ Binary found: /usr/local/bin/colibri-server"
    ls -lh /usr/local/bin/colibri-server
    echo ""
    echo "Version info:"
    /usr/local/bin/colibri-server -h 2>&1 | head -3 || echo "Cannot run binary"
else
    echo "❌ Binary not found at /usr/local/bin/colibri-server"
fi
echo ""

# 2. Check Config
echo "2. Configuration File"
echo "--------------------"
if [ -f "/usr/local/etc/colibri/server.conf" ]; then
    echo "✅ Config found: /usr/local/etc/colibri/server.conf"
    ls -lh /usr/local/etc/colibri/server.conf
    echo ""
    echo "Current port setting:"
    grep "^PORT=" /usr/local/etc/colibri/server.conf || echo "PORT not set"
else
    echo "❌ Config not found at /usr/local/etc/colibri/server.conf"
fi
echo ""

# 3. Check LaunchDaemon
echo "3. LaunchDaemon"
echo "---------------"
if [ -f "/Library/LaunchDaemons/io.corpuscore.colibri-server.plist" ]; then
    echo "✅ LaunchDaemon found: /Library/LaunchDaemons/io.corpuscore.colibri-server.plist"
    ls -lh /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
else
    echo "❌ LaunchDaemon not found"
fi
echo ""

# 4. Check Service Status
echo "4. Service Status"
echo "-----------------"
if launchctl list | grep -q "io.corpuscore.colibri-server"; then
    echo "✅ Service is loaded"
    launchctl list | grep colibri-server
    echo ""
    
    # Check if it's actually running
    if pgrep -f colibri-server > /dev/null; then
        echo "✅ Service is RUNNING"
        echo "Process:"
        ps aux | grep colibri-server | grep -v grep
    else
        echo "⚠️  Service is loaded but NOT RUNNING"
    fi
else
    echo "❌ Service is not loaded"
    echo "   Try: sudo launchctl load /Library/LaunchDaemons/io.corpuscore.colibri-server.plist"
fi
echo ""

# 5. Check Logs
echo "5. Service Logs"
echo "---------------"
if [ -f "/var/log/colibri-server.log" ]; then
    echo "✅ Log file exists: /var/log/colibri-server.log"
    echo "Last 20 lines:"
    echo "---"
    tail -20 /var/log/colibri-server.log
else
    echo "⚠️  Log file not found: /var/log/colibri-server.log"
fi
echo ""

if [ -f "/var/log/colibri-server.error.log" ]; then
    echo "✅ Error log exists: /var/log/colibri-server.error.log"
    if [ -s "/var/log/colibri-server.error.log" ]; then
        echo "Last 20 lines:"
        echo "---"
        tail -20 /var/log/colibri-server.error.log
    else
        echo "(empty - no errors)"
    fi
else
    echo "⚠️  Error log not found: /var/log/colibri-server.error.log"
fi
echo ""

# 6. Check Port Usage
echo "6. Port Check"
echo "-------------"
CONFIG_PORT=$(grep "^PORT=" /usr/local/etc/colibri/server.conf 2>/dev/null | cut -d= -f2)
if [ -n "$CONFIG_PORT" ]; then
    echo "Configured port: $CONFIG_PORT"
    if lsof -i :$CONFIG_PORT > /dev/null 2>&1; then
        echo "⚠️  Port $CONFIG_PORT is already in use:"
        lsof -i :$CONFIG_PORT
    else
        echo "✅ Port $CONFIG_PORT is free"
    fi
else
    echo "Using default port: 8090"
    if lsof -i :8090 > /dev/null 2>&1; then
        echo "⚠️  Port 8090 is already in use:"
        lsof -i :8090
    else
        echo "✅ Port 8090 is free"
    fi
fi
echo ""

# 7. Useful Commands
echo "7. Useful Commands"
echo "------------------"
echo "Start service:   sudo launchctl start io.corpuscore.colibri-server"
echo "Stop service:    sudo launchctl stop io.corpuscore.colibri-server"
echo "Restart service: sudo launchctl stop io.corpuscore.colibri-server && sudo launchctl start io.corpuscore.colibri-server"
echo "Unload service:  sudo launchctl unload /Library/LaunchDaemons/io.corpuscore.colibri-server.plist"
echo "Load service:    sudo launchctl load /Library/LaunchDaemons/io.corpuscore.colibri-server.plist"
echo "View logs:       tail -f /var/log/colibri-server.log"
echo "View errors:     tail -f /var/log/colibri-server.error.log"
echo ""

