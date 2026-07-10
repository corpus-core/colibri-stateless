#!/bin/bash

# Lokales Static Analysis Script mit scan-build
# Führt die gleiche statische Code-Analyse aus wie in der CI
#
# Verwendung:
#   ./scripts/run_static_analysis.sh
#   ./scripts/run_static_analysis.sh --simple    # Fallback ohne spezielle Checker
#   ./scripts/run_static_analysis.sh --sarif     # SARIF-Output für VSCode

set -e

# Farben für Output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🔍 Starte lokale statische Code-Analyse mit scan-build${NC}"

# Prüfe ob scan-build verfügbar ist
if ! command -v scan-build &> /dev/null; then
    echo -e "${RED}❌ scan-build ist nicht installiert${NC}"
    echo -e "${YELLOW}Installiere clang-tools:${NC}"
    echo "  macOS: brew install llvm"
    echo "  Ubuntu: sudo apt-get install clang-tools"
    exit 1
fi

# Verzeichnisse vorbereiten
PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR="$PROJECT_ROOT/build_static_analysis"
RESULTS_DIR="$BUILD_DIR/scan-build-results"

echo -e "${BLUE}📁 Projekt-Root: $PROJECT_ROOT${NC}"
echo -e "${BLUE}📁 Build-Verzeichnis: $BUILD_DIR${NC}"

# Cleanup vorheriger Builds
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}🧹 Lösche vorherigen Build...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# scan-build Optionen konfigurieren
SARIF_MODE=false
if [[ "$1" == "--sarif" ]]; then
    SARIF_MODE=true
    echo -e "${BLUE}🔧 SARIF-Modus für VSCode Integration${NC}"
elif [[ "$1" == "--simple" ]]; then
    echo -e "${YELLOW}🔧 Verwende einfachen Modus ohne erweiterte Checker${NC}"
    SCAN_BUILD_OPTS=""
else
    echo -e "${BLUE}🔧 Konfiguriere erweiterte Checker...${NC}"
    
    # Prüfe verfügbare Checker und verwende nur die, die verfügbar sind
    AVAILABLE_CHECKERS=$(scan-build --help-checkers 2>/dev/null | grep -v "^$" || echo "")
    
    # Basis-Optionen (angeglichen an CI)
    SCAN_BUILD_OPTS="\
      -analyzer-config mode=deep \
      -analyzer-config aggressive-binary-operation-simplification=true \
      -analyzer-config explore-paths=true \
      -analyzer-config strict-mode=true"
    
    # Füge verfügbare Checker hinzu (angeglichen an CI)
    # CI verwendet: alpha.core.SizeofPtr, alpha.core.TestAfterDivZero, alpha.security.ArrayBoundV2,
    # alpha.security.MallocOverflow, alpha.security.ReturnPtrRange, optin.performance.Padding

    if echo "$AVAILABLE_CHECKERS" | grep -q "alpha.core.SizeofPtr"; then
        SCAN_BUILD_OPTS="$SCAN_BUILD_OPTS -enable-checker alpha.core.SizeofPtr"
        echo -e "${GREEN}  ✓ alpha.core.SizeofPtr${NC}"
    fi

    if echo "$AVAILABLE_CHECKERS" | grep -q "alpha.core.TestAfterDivZero"; then
        SCAN_BUILD_OPTS="$SCAN_BUILD_OPTS -enable-checker alpha.core.TestAfterDivZero"
        echo -e "${GREEN}  ✓ alpha.core.TestAfterDivZero${NC}"
    fi

    # Bevorzuge V2 wenn verfügbar, sonst Fallback oder nichts (CI nutzt V2)
    if echo "$AVAILABLE_CHECKERS" | grep -q "alpha.security.ArrayBoundV2"; then
        SCAN_BUILD_OPTS="$SCAN_BUILD_OPTS -enable-checker alpha.security.ArrayBoundV2"
        echo -e "${GREEN}  ✓ alpha.security.ArrayBoundV2${NC}"
    elif echo "$AVAILABLE_CHECKERS" | grep -q "alpha.security.ArrayBound"; then
        SCAN_BUILD_OPTS="$SCAN_BUILD_OPTS -enable-checker alpha.security.ArrayBound"
        echo -e "${GREEN}  ✓ alpha.security.ArrayBound (Fallback für V2)${NC}"
    fi
    
    if echo "$AVAILABLE_CHECKERS" | grep -q "alpha.security.MallocOverflow"; then
        SCAN_BUILD_OPTS="$SCAN_BUILD_OPTS -enable-checker alpha.security.MallocOverflow"
        echo -e "${GREEN}  ✓ alpha.security.MallocOverflow${NC}"
    fi
    
    if echo "$AVAILABLE_CHECKERS" | grep -q "alpha.security.ReturnPtrRange"; then
        SCAN_BUILD_OPTS="$SCAN_BUILD_OPTS -enable-checker alpha.security.ReturnPtrRange"
        echo -e "${GREEN}  ✓ alpha.security.ReturnPtrRange${NC}"
    fi
    
    if echo "$AVAILABLE_CHECKERS" | grep -q "optin.performance.Padding"; then
        SCAN_BUILD_OPTS="$SCAN_BUILD_OPTS -enable-checker optin.performance.Padding"
        echo -e "${GREEN}  ✓ optin.performance.Padding${NC}"
    fi
    
    if [[ -z "$AVAILABLE_CHECKERS" ]]; then
        echo -e "${YELLOW}⚠️  Konnte verfügbare Checker nicht ermitteln, verwende Standard-Checker${NC}"
        SCAN_BUILD_OPTS=""
    fi
fi

echo -e "${BLUE}🔧 Finale Checker-Optionen: $SCAN_BUILD_OPTS${NC}"

echo -e "${BLUE}⚙️  Konfiguriere CMake mit scan-build...${NC}"

# CMake Konfiguration mit scan-build (identisch zur CI)
scan-build $SCAN_BUILD_OPTS cmake \
  -DCMAKE_BUILD_TYPE=DEBUG \
  -DTEST=true \
  -DCURL=false \
  ..

echo -e "${BLUE}🔨 Starte scan-build Analyse...${NC}"

# scan-build Ausführung
if [[ "$SARIF_MODE" == "true" ]]; then
    # SARIF-Modus für VSCode
    echo -e "${BLUE}📄 Generiere SARIF-Output für VSCode...${NC}"
    
    # Erstelle SARIF-kompatible Ausgabe
    scan-build \
      --force-analyze-debug-code \
      --status-bugs \
      -v \
      -o scan-build-results \
      --exclude ../libs \
      --exclude ../test \
      --exclude ../build \
      --exclude _deps \
      $SCAN_BUILD_OPTS \
      make > scan-build-output.txt 2>&1
    
    # Konvertiere zu SARIF (vereinfacht)
    echo -e "${BLUE}🔄 Erstelle SARIF-Datei...${NC}"
    cat > "../scan-build-results.sarif" << 'EOF'
{
  "$schema": "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json",
  "version": "2.1.0",
  "runs": [
    {
      "tool": {
        "driver": {
          "name": "scan-build",
          "version": "1.0.0"
        }
      },
      "results": []
    }
  ]
}
EOF
    echo -e "${GREEN}✅ SARIF-Datei erstellt: ../scan-build-results.sarif${NC}"
    
else
    # Standard-Modus
    scan-build \
      --force-analyze-debug-code \
      --status-bugs \
      -v \
      -o scan-build-results \
      --exclude ../libs \
      --exclude ../test \
      --exclude ../build \
      --exclude _deps \
      $SCAN_BUILD_OPTS \
      make 2>&1 | tee scan-build-output.txt
fi

# Exit-Code von scan-build speichern
SCAN_EXIT_CODE=${PIPESTATUS[0]}

echo -e "\n${BLUE}📊 Analysiere Ergebnisse...${NC}"

# Prüfe ob Bugs gefunden wurden
if grep -q -E "No bugs found\.|scan-build: 0 bugs found" scan-build-output.txt; then
    echo -e "${GREEN}✅ Keine Bugs von der statischen Analyse gefunden!${NC}"
    
    # Zeige Zusammenfassung
    echo -e "\n${BLUE}📈 Analyse-Zusammenfassung:${NC}"
    grep -E "scan-build:" scan-build-output.txt || true
    
    exit 0
else
    # Check for suppressed false positives (same logic as CI via scripts/scan-build-apply-suppressions.sh)
    if "$PROJECT_ROOT/scripts/scan-build-apply-suppressions.sh" scan-build-results "$PROJECT_ROOT/scripts/scan-build-suppressions.txt"; then
        echo -e "${GREEN}✅ Alle gefundenen Meldungen sind bekannte False Positives (siehe scripts/scan-build-suppressions.txt)${NC}"
        echo -e "\n${BLUE}📈 Analyse-Zusammenfassung:${NC}"
        grep -E "scan-build:" scan-build-output.txt || true
        exit 0
    fi

    echo -e "${RED}⚠️  Statische Analyse hat Probleme gefunden${NC}"
    
    # Zeige gefundene Issues
    echo -e "\n${YELLOW}🔍 Gefundene Probleme:${NC}"
    
    ISSUE_COUNT=0
    grep -E "^/.+\.(c|h):[0-9]+:[0-9]+: (warning|error):" scan-build-output.txt | \
    grep -v "/libs/" | \
    grep -v "/test/" | \
    grep -v "/build/" | \
    grep -v "/_deps/" | \
    while IFS= read -r line; do
      if [[ "$line" =~ ^(.+):([0-9]+):([0-9]+):[[:space:]]+(warning|error):[[:space:]]+(.+)[[:space:]]+\[([^\]]+)\] ]]; then
        filepath="${BASH_REMATCH[1]}"
        linenum="${BASH_REMATCH[2]}"
        colnum="${BASH_REMATCH[3]}"
        severity="${BASH_REMATCH[4]}"
        message="${BASH_REMATCH[5]}"
        checker="${BASH_REMATCH[6]}"

        shortpath=$(echo "$filepath" | sed 's|.*/src/|src/|; s|.*/bindings/|bindings/|')

        if [ "$severity" = "error" ]; then
          icon="❌"
        else
          icon="⚠️ "
        fi

        echo -e "  ${icon} ${RED}${message}${NC}"
        echo -e "     ${BLUE}[${checker}]${NC} in ${shortpath}:${linenum}:${colnum}"
        echo ""
        ISSUE_COUNT=$((ISSUE_COUNT + 1))
      fi
    done

    ISSUE_COUNT=$(grep -E "^/.+\.(c|h):[0-9]+:[0-9]+: (warning|error):" scan-build-output.txt | \
      grep -v "/libs/" | grep -v "/test/" | grep -v "/build/" | grep -v "/_deps/" | wc -l | tr -d ' ')
    echo -e "${YELLOW}Gefundene Issues im Quellcode: ${ISSUE_COUNT}${NC}"
    
    # Zeige HTML Report Location falls vorhanden
    LATEST_REPORT=$(find scan-build-results -type d -name "20*" 2>/dev/null | sort | tail -n 1)
    if [ -n "$LATEST_REPORT" ] && [ -d "$LATEST_REPORT" ]; then
        echo -e "\n${BLUE}📄 Detaillierter HTML-Report verfügbar:${NC}"
        echo "  file://$LATEST_REPORT/index.html"
        
        # Versuche den Report automatisch zu öffnen (macOS/Linux)
        if command -v open &> /dev/null; then
            echo -e "${YELLOW}🌐 Öffne HTML-Report im Browser...${NC}"
            open "$LATEST_REPORT/index.html"
        elif command -v xdg-open &> /dev/null; then
            echo -e "${YELLOW}🌐 Öffne HTML-Report im Browser...${NC}"
            xdg-open "$LATEST_REPORT/index.html"
        fi
    fi
    
    echo -e "\n${YELLOW}💡 Tipp: Behebe die gefundenen Probleme und führe das Script erneut aus${NC}"
    echo -e "${YELLOW}💡 Bei Problemen mit Checkern versuche: ./scripts/run_static_analysis.sh --simple${NC}"
    
    exit $SCAN_EXIT_CODE
fi
