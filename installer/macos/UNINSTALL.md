# Deinstallation von Colibri Server auf macOS

## Automatische Deinstallation (Empfohlen)

Das Uninstall-Skript entfernt alle Dateien sicher und stoppt den Service:

```bash
sudo /path/to/installer/macos/uninstall.sh
```

Oder wenn du es lokal hast:

```bash
curl -O https://raw.githubusercontent.com/YOUR_ORG/colibri-stateless/main/installer/macos/uninstall.sh
chmod +x uninstall.sh
sudo ./uninstall.sh
```

Das Skript fragt, ob du die Konfiguration und Logs behalten möchtest.

## Manuelle Deinstallation

Falls du die Dateien manuell entfernen möchtest:

### 1. Service stoppen und entfernen

```bash
# Service stoppen
sudo launchctl unload /Library/LaunchDaemons/io.corpuscore.colibri-server.plist

# LaunchDaemon entfernen
sudo rm /Library/LaunchDaemons/io.corpuscore.colibri-server.plist
```

### 2. Binary entfernen

```bash
sudo rm /usr/local/bin/colibri-server
```

### 3. Konfiguration und Daten entfernen (optional)

```bash
# Config-Verzeichnis
sudo rm -rf /usr/local/etc/colibri

# Log-Dateien
sudo rm /var/log/colibri-server.log
sudo rm /var/log/colibri-server.error.log

# Daten-Verzeichnis
sudo rm -rf /var/lib/colibri
```

### 4. Package-Receipt entfernen

```bash
sudo pkgutil --forget io.corpuscore.colibri-server
```

## Status überprüfen

Nach der Deinstallation kannst du überprüfen, ob alles entfernt wurde:

```bash
# Prüfen, ob Service noch läuft
launchctl list | grep colibri

# Prüfen, ob Binary existiert
which colibri-server

# Prüfen, ob Package-Receipt existiert
pkgutil --pkg-info io.corpuscore.colibri-server
```

Alle Befehle sollten keine Ergebnisse mehr liefern.

