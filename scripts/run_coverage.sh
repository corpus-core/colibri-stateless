#!/bin/bash

# Lokales Test Coverage Script
# Führt die gleiche Coverage-Analyse aus wie in der CI
#
# Verwendung:
#   ./scripts/run_coverage.sh                 # Vollständige Coverage-Analyse
#   ./scripts/run_coverage.sh --html-only     # Nur HTML-Report generieren (setzt vorherigen Build voraus)
#   ./scripts/run_coverage.sh --no-open       # HTML-Report nicht automatisch öffnen
#   ./scripts/run_coverage.sh --reconfigure   # CMake neu konfigurieren (z.B. nach Dependencies-Update)

set -e

# Farben für Output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}🧪 Starte lokale Test Coverage Analyse${NC}"

# Prüfe ob gcovr verfügbar ist und installiere es falls nötig
if ! command -v gcovr &> /dev/null; then
    echo -e "${YELLOW}⚠️  gcovr ist nicht installiert${NC}"
    
    # Versuche automatische Installation mit Homebrew (macOS)
    if command -v brew &> /dev/null; then
        echo -e "${BLUE}📦 Installiere gcovr mit Homebrew...${NC}"
        brew install gcovr
        
        if ! command -v gcovr &> /dev/null; then
            echo -e "${RED}❌ Installation fehlgeschlagen${NC}"
            exit 1
        fi
        echo -e "${GREEN}✅ gcovr erfolgreich installiert${NC}"
    else
        echo -e "${RED}❌ gcovr ist nicht installiert und Homebrew ist nicht verfügbar${NC}"
        echo -e "${YELLOW}Installiere gcovr manuell:${NC}"
        echo "  brew install gcovr"
        echo "  oder: pip3 install gcovr"
        exit 1
    fi
fi

# Verzeichnisse vorbereiten
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build_coverage"
COVERAGE_DIR="$BUILD_DIR/coverage-report"

echo -e "${BLUE}📁 Projekt-Root: $PROJECT_ROOT${NC}"
echo -e "${BLUE}📁 Build-Verzeichnis: $BUILD_DIR${NC}"

# Parse command line arguments
HTML_ONLY=false
NO_OPEN=false
RECONFIGURE=false

for arg in "$@"; do
    case $arg in
        --html-only)
            HTML_ONLY=true
            shift
            ;;
        --no-open)
            NO_OPEN=true
            shift
            ;;
        --reconfigure)
            RECONFIGURE=true
            shift
            ;;
        *)
            echo -e "${RED}❌ Unbekannte Option: $arg${NC}"
            echo "Verwendung: $0 [--html-only] [--no-open] [--reconfigure]"
            exit 1
            ;;
    esac
done

if [[ "$HTML_ONLY" == "false" ]]; then
    # Prüfe ob Build-Verzeichnis bereits existiert und konfiguriert ist
    if [ -d "$BUILD_DIR" ] && [ -f "$BUILD_DIR/CMakeCache.txt" ] && [[ "$RECONFIGURE" == "false" ]]; then
        echo -e "${GREEN}✓ Build-Verzeichnis ist bereits konfiguriert${NC}"
        echo -e "${CYAN}  (Verwende --reconfigure um CMake neu zu konfigurieren)${NC}"
        cd "$BUILD_DIR"
    else
        # Cleanup vorheriger Builds wenn Reconfigure oder Build-Dir existiert
        if [ -d "$BUILD_DIR" ]; then
            if [[ "$RECONFIGURE" == "true" ]]; then
                echo -e "${YELLOW}🔄 Reconfigure angefordert - lösche vorherigen Build...${NC}"
            else
                echo -e "${YELLOW}🧹 Lösche vorherigen Build...${NC}"
            fi
            rm -rf "$BUILD_DIR"
        fi

        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"

        echo -e "${BLUE}⚙️  Konfiguriere CMake mit Coverage-Flags...${NC}"
        echo -e "${CYAN}   Optionen: TEST=true CURL=true HTTP_SERVER=true PROVER_CACHE=true CMAKE_BUILD_TYPE=Debug COVERAGE=true${NC}"

        # CMake Konfiguration (identisch zur CI)
        cmake \
          -DCMAKE_BUILD_TYPE=Debug \
          -DTEST=true \
          -DCURL=true \
          -DPROVER_CACHE=true \
          -DHTTP_SERVER=true \
          -DCOVERAGE=true \
          ..
    fi

    echo -e "${BLUE}🔨 Baue Projekt...${NC}"
    make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

    echo -e "${BLUE}🧪 Führe Tests aus...${NC}"
    
    # Tests ausführen (in test/unittests Verzeichnis)
    cd test/unittests
    
    # Prüfe ob ctest verfügbar ist
    if ! command -v ctest &> /dev/null; then
        echo -e "${RED}❌ ctest ist nicht verfügbar${NC}"
        exit 1
    fi
    
    # Führe Tests aus
    if ctest --output-on-failure; then
        echo -e "${GREEN}✅ Alle Tests bestanden${NC}"
    else
        echo -e "${RED}❌ Einige Tests sind fehlgeschlagen${NC}"
        echo -e "${YELLOW}⚠️  Coverage-Report wird trotzdem generiert${NC}"
    fi
    
    # Zurück zum Build-Root für Coverage-Analyse
    cd "$BUILD_DIR"
else
    echo -e "${YELLOW}⚡ HTML-Only Modus - überspringe Build und Tests${NC}"
    
    if [ ! -d "$BUILD_DIR" ]; then
        echo -e "${RED}❌ Build-Verzeichnis existiert nicht. Führe erst einen vollständigen Build aus.${NC}"
        exit 1
    fi
    
    cd "$BUILD_DIR"
fi

echo -e "${BLUE}📊 Generiere Coverage-Reports...${NC}"

# Coverage-Report Verzeichnis erstellen
mkdir -p "$COVERAGE_DIR"

# Build list of excludes from ignore file
IGNORE_FILE="$SCRIPT_DIR/coverage_ignore.txt"
EXCLUDE_ARGS=""

# Read coverage ignore list and build exclude arguments
if [ -f "$IGNORE_FILE" ]; then
    echo -e "${CYAN}📋 Lade Ignore-Liste: $IGNORE_FILE${NC}"
    while IFS= read -r line || [ -n "$line" ]; do
        # Skip comments and empty lines
        [[ "$line" =~ ^#.*$ ]] && continue
        [[ -z "$line" ]] && continue
        # Add exclude argument
        EXCLUDE_ARGS="$EXCLUDE_ARGS --exclude $PROJECT_ROOT/$line"
        echo -e "${CYAN}   - Ignoriere: $line${NC}"
    done < "$IGNORE_FILE"
fi

# Gleiche Excludes wie in der CI (mit absoluten Pfaden für Third-Party Code)
echo -e "${CYAN}   Standard-Excludes: libs/, build*/, test/, src/cli/, _deps/${NC}"

# 1. XML-Report für maschinelle Verarbeitung (identisch zur CI)
echo -e "${BLUE}📄 Generiere XML-Report...${NC}"
eval "gcovr --root \"$PROJECT_ROOT\" \"$BUILD_DIR\" \
      --exclude \"$PROJECT_ROOT/libs/.*\" \
      --exclude \"$BUILD_DIR/_deps/.*\" \
      --exclude \".*/build.*/.*\" \
      --exclude \".*/build_.*\" \
      --exclude \"$PROJECT_ROOT/test/.*\" \
      --exclude \"$PROJECT_ROOT/src/cli/.*\" \
      $EXCLUDE_ARGS \
      --xml \"$BUILD_DIR/coverage.xml\""

# 2. HTML-Report mit Details (identisch zur CI)
echo -e "${BLUE}🌐 Generiere HTML-Report...${NC}"
eval "gcovr --root \"$PROJECT_ROOT\" \"$BUILD_DIR\" \
      --exclude \"$PROJECT_ROOT/libs/.*\" \
      --exclude \"$BUILD_DIR/_deps/.*\" \
      --exclude \".*/build.*/.*\" \
      --exclude \".*/build_.*\" \
      --exclude \"$PROJECT_ROOT/test/.*\" \
      --exclude \"$PROJECT_ROOT/src/cli/.*\" \
      $EXCLUDE_ARGS \
      --html-details \"$COVERAGE_DIR/index.html\" \
      --html-title \"Colibri Coverage Report\""

# 3. Text-Zusammenfassung für die Konsole
echo -e "${BLUE}📝 Generiere Text-Zusammenfassung...${NC}"
eval "gcovr --root \"$PROJECT_ROOT\" \"$BUILD_DIR\" \
      --exclude \"$PROJECT_ROOT/libs/.*\" \
      --exclude \"$BUILD_DIR/_deps/.*\" \
      --exclude \".*/build.*/.*\" \
      --exclude \".*/build_.*\" \
      --exclude \"$PROJECT_ROOT/test/.*\" \
      --exclude \"$PROJECT_ROOT/src/cli/.*\" \
      $EXCLUDE_ARGS \
      --print-summary > \"$COVERAGE_DIR/coverage_summary.txt\""

echo -e "\n${GREEN}✅ Coverage-Reports erfolgreich generiert${NC}"

# Zeige Coverage-Zusammenfassung
echo -e "\n${BLUE}════════════════════════════════════════════════${NC}"
echo -e "${BLUE}📈 Coverage-Zusammenfassung${NC}"
echo -e "${BLUE}════════════════════════════════════════════════${NC}"
eval "gcovr --root \"$PROJECT_ROOT\" \"$BUILD_DIR\" \
      --exclude \"$PROJECT_ROOT/libs/.*\" \
      --exclude \"$BUILD_DIR/_deps/.*\" \
      --exclude \".*/build.*/.*\" \
      --exclude \".*/build_.*\" \
      --exclude \"$PROJECT_ROOT/test/.*\" \
      --exclude \"$PROJECT_ROOT/src/cli/.*\" \
      $EXCLUDE_ARGS"
echo -e "${BLUE}════════════════════════════════════════════════${NC}"

# Zeige Dateipfade
echo -e "\n${GREEN}📁 Generierte Reports:${NC}"
echo -e "  ${CYAN}HTML (detailliert):${NC} file://$COVERAGE_DIR/index.html"
echo -e "  ${CYAN}XML:${NC}               $BUILD_DIR/coverage.xml"
echo -e "  ${CYAN}Text-Zusammenfassung:${NC} $COVERAGE_DIR/coverage_summary.txt"

# HTML-Report automatisch öffnen (macOS)
if [[ "$NO_OPEN" == "false" ]]; then
    if command -v open &> /dev/null; then
        echo -e "\n${YELLOW}🌐 Öffne HTML-Report im Browser...${NC}"
        open "$COVERAGE_DIR/index.html"
    elif command -v xdg-open &> /dev/null; then
        echo -e "\n${YELLOW}🌐 Öffne HTML-Report im Browser...${NC}"
        xdg-open "$COVERAGE_DIR/index.html"
    fi
else
    echo -e "\n${YELLOW}💡 HTML-Report kann mit diesem Befehl geöffnet werden:${NC}"
    echo -e "   open $COVERAGE_DIR/index.html"
fi

echo -e "\n${GREEN}✅ Coverage-Analyse abgeschlossen${NC}"
echo -e "${CYAN}💡 Tipp: Mit --html-only kannst du nur den HTML-Report neu generieren${NC}"
echo -e "${CYAN}💡 Tipp: Mit --no-open wird der Browser nicht automatisch geöffnet${NC}"
echo -e "${CYAN}💡 Tipp: Mit --reconfigure wird CMake neu konfiguriert (z.B. nach Dependencies-Update)${NC}"

