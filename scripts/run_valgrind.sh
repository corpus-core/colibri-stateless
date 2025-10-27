#!/bin/bash

# Lokales Valgrind Script mit Docker
# Führt die gleiche Valgrind-Analyse aus wie in der CI
#
# Verwendung:
#   ./scripts/run_valgrind.sh                                # Alle Tests
#   ./scripts/run_valgrind.sh test_server                    # Nur einen spezifischen Test
#   ./scripts/run_valgrind.sh server                         # test_ Prefix optional
#   ./scripts/run_valgrind.sh --quick                        # Nur wichtige Tests
#   ./scripts/run_valgrind.sh --memory-analysis              # Inkl. massif/memcheck Analyse
#   ./scripts/run_valgrind.sh test_server --memory-analysis  # Spezifischer Test mit Analyse
#   ./scripts/run_valgrind.sh --help                         # Hilfe anzeigen

set -e

# Farben für Output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🔍 Starte lokale Valgrind-Analyse mit Docker${NC}"
echo -e "${YELLOW}⚠️  Valgrind läuft nicht nativ auf macOS - wird in Docker ausgeführt${NC}"

# Prüfe ob Docker verfügbar ist
if ! command -v docker &> /dev/null; then
    echo -e "${RED}❌ Docker ist nicht installiert${NC}"
    echo -e "${YELLOW}Installiere Docker:${NC}"
    echo "  macOS: brew install --cask docker"
    echo "  oder: https://www.docker.com/products/docker-desktop"
    exit 1
fi

# Prüfe ob Docker läuft
if ! docker info &> /dev/null; then
    echo -e "${RED}❌ Docker läuft nicht${NC}"
    echo -e "${YELLOW}Bitte starte Docker Desktop oder den Docker Daemon${NC}"
    exit 1
fi

# Verzeichnisse
PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
RESULTS_DIR="$PROJECT_ROOT/valgrind_results"

echo -e "${BLUE}📁 Projekt-Root: $PROJECT_ROOT${NC}"
echo -e "${BLUE}📁 Ergebnis-Verzeichnis: $RESULTS_DIR${NC}"

# Cleanup vorheriger Ergebnisse
if [ -d "$RESULTS_DIR" ]; then
    echo -e "${YELLOW}🧹 Lösche vorherige Ergebnisse...${NC}"
    rm -rf "$RESULTS_DIR"
fi
mkdir -p "$RESULTS_DIR"

# Prüfe ob Valgrind Docker Image existiert
IMAGE_NAME="colibri-valgrind:latest"
if ! docker image inspect "$IMAGE_NAME" &> /dev/null; then
    echo -e "${BLUE}🐳 Docker Image nicht gefunden, erstelle $IMAGE_NAME...${NC}"
    
    # Erstelle einfaches Dockerfile nur mit Tools
    cat > "$RESULTS_DIR/Dockerfile.valgrind" << 'DOCKERFILE_END'
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential \
      clang \
      cmake \
      curl \
      git \
      valgrind \
      pkg-config \
      libssl-dev \
      libcurl4-openssl-dev \
      ca-certificates \
      wget && \
    update-ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# Installiere Rust (benötigt für Kona-P2P bridge)
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable

ENV PATH="/root/.cargo/bin:${PATH}"
ENV CARGO_NET_GIT_FETCH_WITH_CLI=true
ENV CARGO_REGISTRIES_CRATES_IO_PROTOCOL=sparse

WORKDIR /app

CMD ["/bin/bash"]
DOCKERFILE_END
    
    # Baue Image
    docker build -t "$IMAGE_NAME" -f "$RESULTS_DIR/Dockerfile.valgrind" "$RESULTS_DIR" 2>&1 | tee "$RESULTS_DIR/docker-build.log"
    
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        echo -e "${RED}❌ Docker Image Build fehlgeschlagen${NC}"
        echo -e "${YELLOW}Siehe Log: $RESULTS_DIR/docker-build.log${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}✅ Docker Image erfolgreich gebaut${NC}"
else
    echo -e "${GREEN}✅ Docker Image bereits vorhanden${NC}"
fi

# Parse Argumente
QUICK_MODE=false
MEMORY_ANALYSIS=false
SPECIFIC_TEST=""

for arg in "$@"; do
    if [[ "$arg" == "--quick" ]]; then
        QUICK_MODE=true
        echo -e "${BLUE}🔧 Quick-Modus: Führt nur wichtige Tests aus${NC}"
    elif [[ "$arg" == "--memory-analysis" ]]; then
        MEMORY_ANALYSIS=true
        echo -e "${BLUE}🔧 Memory-Analyse-Modus: Inkl. massif/memcheck${NC}"
    elif [[ "$arg" == "--list" ]]; then
        echo -e "${BLUE}📋 Verfügbare Tests:${NC}"
        echo -e "${YELLOW}Baue Docker Image um Tests aufzulisten...${NC}"
        # Führe nur bis zum Build durch
        echo "Hinweis: Das kann beim ersten Mal einige Minuten dauern."
        echo ""
        exit 0
    elif [[ "$arg" == "--help" ]] || [[ "$arg" == "-h" ]]; then
        echo -e "${BLUE}Valgrind Test Script${NC}"
        echo ""
        echo "Verwendung:"
        echo "  ./scripts/run_valgrind.sh [TEST_NAME] [FLAGS]"
        echo ""
        echo "Beispiele:"
        echo "  ./scripts/run_valgrind.sh                              # Alle Tests"
        echo "  ./scripts/run_valgrind.sh test_server                  # Nur test_server"
        echo "  ./scripts/run_valgrind.sh server                       # Auch ohne 'test_' Prefix"
        echo "  ./scripts/run_valgrind.sh test_server --memory-analysis # Mit Memory-Analyse"
        echo ""
        echo "Flags:"
        echo "  --quick              Schneller Modus (nur wichtige Tests)"
        echo "  --memory-analysis    Detaillierte Memory-Analyse mit massif/memcheck"
        echo "  --help, -h           Diese Hilfe anzeigen"
        echo ""
        exit 0
    elif [[ ! "$arg" == --* ]]; then
        # Kein Flag, also ein Test-Name
        SPECIFIC_TEST="$arg"
        # Füge "test_" Prefix hinzu falls nicht vorhanden
        if [[ ! "$SPECIFIC_TEST" == test_* ]]; then
            SPECIFIC_TEST="test_${SPECIFIC_TEST}"
        fi
        echo -e "${BLUE}🎯 Spezifischer Test: ${SPECIFIC_TEST}${NC}"
    fi
done

# Baue Projekt im Container
echo -e "${BLUE}🔨 Baue Projekt im Docker Container (Debug)...${NC}"

# Prüfe ob Build-Verzeichnis von einem anderen System erstellt wurde
BUILD_MARKER="$PROJECT_ROOT/build/valgrind/.docker_build_marker"
if [ -d "$PROJECT_ROOT/build/valgrind" ] && [ ! -f "$BUILD_MARKER" ]; then
    echo -e "${YELLOW}🧹 Build-Verzeichnis wurde nicht von Docker erstellt, räume auf...${NC}"
    rm -rf "$PROJECT_ROOT/build/valgrind"
fi

# Erstelle Build-Verzeichnis wenn es nicht existiert
mkdir -p "$PROJECT_ROOT/build/valgrind"

# Baue im Container mit isoliertem Build-Verzeichnis
# Source wird read-only gemountet, Build-Output in separatem Mount
docker run --rm \
  -v "$PROJECT_ROOT:/src:ro" \
  -v "$PROJECT_ROOT/build/valgrind:/build" \
  -w /build \
  "$IMAGE_NAME" \
  bash -c "touch /build/.docker_build_marker && \
    if [ ! -f CMakeCache.txt ]; then \
      echo '⚙️  Konfiguriere CMake (erster Build)...'; \
      cmake -DTEST=true -DCURL=true -DPROVER_CACHE=true -DHTTP_SERVER=true -DCMAKE_BUILD_TYPE=Debug /src; \
    else \
      echo '⚙️  CMake bereits konfiguriert, überspringe Konfiguration...'; \
    fi && \
    make -j\$(nproc)" \
  2>&1 | tee "$RESULTS_DIR/build.log"

if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}❌ Build fehlgeschlagen${NC}"
    echo -e "${YELLOW}Siehe Log: $RESULTS_DIR/build.log${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Build erfolgreich${NC}"

# Erstelle Valgrind Test Script
cat > "$RESULTS_DIR/run_valgrind_tests.sh" << VALGRIND_SCRIPT_END
#!/bin/bash
set -e

echo "# Valgrind Memory Check Results" > /results/valgrind_summary.md
echo "" >> /results/valgrind_summary.md
EXIT_CODE=0

SPECIFIC_TEST="${SPECIFIC_TEST}"

for test_binary in /build/test/unittests/test_*; do
  if [ -x "\$test_binary" ]; then
    TEST_NAME=\$(basename "\$test_binary")
    
    # Wenn spezifischer Test angegeben, überspringe alle anderen
    if [ -n "\$SPECIFIC_TEST" ] && [ "\$TEST_NAME" != "\$SPECIFIC_TEST" ]; then
      continue
    fi
    
    echo "Running valgrind on \$test_binary"
    
    # Create a temporary file for valgrind output
    TEMP_OUTPUT=\$(mktemp)
    
    # Configure valgrind flags based on test type
    VALGRIND_FLAGS="--leak-check=full --error-exitcode=1 --track-origins=yes --show-leak-kinds=all"
    
    if [[ "\$TEST_NAME" == "test_server"* ]]; then
      # Server tests: use suppressions for threading/libuv/curl false positives
      VALGRIND_FLAGS="\$VALGRIND_FLAGS --suppressions=/test/valgrind/server.supp --fair-sched=yes"
      VALGRIND_FLAGS="\$VALGRIND_FLAGS --errors-for-leak-kinds=definite"
      echo "  → Using server suppressions (threading/libuv/curl)"
    else
      # Regular tests: strict mode (fail on definite + possible leaks)
      VALGRIND_FLAGS="\$VALGRIND_FLAGS --errors-for-leak-kinds=definite,possible"
    fi
    
    # Run valgrind and capture both stdout and stderr
    valgrind \$VALGRIND_FLAGS "\$test_binary" 2>&1 | tee "\$TEMP_OUTPUT"
    VALGRIND_STATUS=\$?
    
    # Extract valgrind-specific messages (lines starting with ==)
    VALGRIND_MESSAGES=\$(grep -E "^==[0-9]+== " "\$TEMP_OUTPUT" || true)
    
    # For server tests, check for actual definite leaks, not just exit code
    if [[ "\$TEST_NAME" == "test_server"* ]]; then
      DEFINITELY_LOST=\$(grep "definitely lost:" "\$TEMP_OUTPUT" | grep -oE "[0-9,]+ bytes" | head -1 | tr -d ',' || echo "0 bytes")
      if [[ "\$DEFINITELY_LOST" != "0 bytes" ]]; then
        EXIT_CODE=1
        echo "❌ \$TEST_NAME: definite memory leaks found (\$DEFINITELY_LOST)" >> /results/valgrind_summary.md
        echo '```' >> /results/valgrind_summary.md
        grep "definitely lost:" "\$TEMP_OUTPUT" >> /results/valgrind_summary.md || true
        echo '```' >> /results/valgrind_summary.md
      elif [ \$VALGRIND_STATUS -ne 0 ]; then
        # Exit code non-zero but no definite leaks - probably still reachable memory
        echo "⚠️  \$TEST_NAME: Valgrind exited with code \$VALGRIND_STATUS (no definite leaks, possibly still reachable)" >> /results/valgrind_summary.md
      else
        echo "✅ \$TEST_NAME:  ok" >> /results/valgrind_summary.md
      fi
    else
      # Regular tests: fail on any non-zero exit code
      if [ \$VALGRIND_STATUS -ne 0 ]; then
        EXIT_CODE=1
        echo "❌ \$TEST_NAME: memory issues found:" >> /results/valgrind_summary.md
        echo '```' >> /results/valgrind_summary.md
        grep -vE "^==[0-9]+== " "\$TEMP_OUTPUT" >> /results/valgrind_summary.md || true
        echo "" >> /results/valgrind_summary.md
        echo "\$VALGRIND_MESSAGES" >> /results/valgrind_summary.md
        echo '```' >> /results/valgrind_summary.md
        echo "" >> /results/valgrind_summary.md
      else
        echo "✅ \$TEST_NAME:  ok" >> /results/valgrind_summary.md
      fi
    fi
    
    # Save detailed output
    mv "\$TEMP_OUTPUT" "/results/\${TEST_NAME}.log"
  fi
done

# Prüfe ob der spezifische Test gefunden wurde
if [ -n "\$SPECIFIC_TEST" ]; then
    if [ ! -f "/results/\${SPECIFIC_TEST}.log" ]; then
      echo "❌ Test '\$SPECIFIC_TEST' nicht gefunden!" >> /results/valgrind_summary.md
      echo "" >> /results/valgrind_summary.md
      echo "Verfügbare Tests:" >> /results/valgrind_summary.md
      for tb in /build/test/unittests/test_*; do
        if [ -x "\$tb" ]; then
          echo "  - \$(basename \$tb)" >> /results/valgrind_summary.md
        fi
      done
      EXIT_CODE=1
    fi
  fi

exit \$EXIT_CODE
VALGRIND_SCRIPT_END

# Erstelle Memory Analysis Script (optional)
if [[ "$MEMORY_ANALYSIS" == "true" ]]; then
cat > "$RESULTS_DIR/run_memory_analysis.sh" << 'MEMORY_SCRIPT_END'
#!/bin/bash
set -e

echo "Running memory analysis on verify_only..."

TEST_BINARY="/build/test/unittests/verify_only"

# Create directory for detailed reports
mkdir -p /results/memory-analysis

# Run massif for heap analysis
valgrind --tool=massif \
  --detailed-freq=1 \
  --max-snapshots=100 \
  --threshold=0.1 \
  --stacks=yes \
  --pages-as-heap=no \
  --heap=yes \
  --ignore-fn=dl_start \
  --ignore-fn=_dl_start \
  --ignore-fn=_dl_* \
  --ignore-fn=dl_* \
  --ignore-fn=__GI_* \
  --ignore-fn=__libc_* \
  --ignore-fn=malloc \
  --ignore-fn=calloc \
  --ignore-fn=realloc \
  --ignore-fn=free \
  --ignore-fn=__malloc_* \
  --ignore-fn=__realloc_* \
  --ignore-fn=__calloc_* \
  --ignore-fn=__free_* \
  --ignore-fn=*mmap* \
  --ignore-fn=*brk* \
  --ignore-fn=tcache* \
  --ignore-fn=sbrk \
  --ignore-fn=sysmalloc \
  --ignore-fn=_int_malloc \
  --massif-out-file=massif.out \
  $TEST_BINARY

# Run memcheck for stack analysis
valgrind --tool=memcheck \
  --track-origins=yes \
  --max-stackframe=8388608 \
  --main-stacksize=8388608 \
  --log-file=memcheck.out \
  --verbose \
  --trace-children=yes \
  $TEST_BINARY

# Generate full massif report
ms_print massif.out > /results/memory-analysis/massif-full-report.txt

# Create summary
{
  echo "# Memory Analysis Summary"
  echo ""
  echo "## Memory Usage"
  echo "| Metric | Size |"
  echo "|--------|------|"
  # Extract peak heap memory (convert to KB)
  echo "| Peak Heap | $(awk '/heap_tree=peak/{getline; split($0,a," "); print a[2]/1024" KB"}' massif.out) |"
  # Find maximum stack size across all snapshots (convert to KB)
  echo "| Peak Stack | $(awk '/mem_stacks_B/{split($0,a,"="); if(a[2]>max) max=a[2]} END{print max/1024" KB"}' massif.out) |"
  # Get executable size in KB
  echo "| Executable | $(ls -l $TEST_BINARY | awk '{print int($5/1024)" KB"}') |"
  echo ""
} > /results/memory_summary.md

# Save detailed logs
cp memcheck.out /results/memory-analysis/
cp massif.out /results/memory-analysis/

echo "Memory analysis complete!"
MEMORY_SCRIPT_END
fi

chmod +x "$RESULTS_DIR/run_valgrind_tests.sh"
[[ "$MEMORY_ANALYSIS" == "true" ]] && chmod +x "$RESULTS_DIR/run_memory_analysis.sh"

# Führe Valgrind Tests im Container aus
echo -e "${BLUE}🔬 Führe Valgrind Tests aus...${NC}"
echo -e "${YELLOW}Dies kann mehrere Minuten dauern...${NC}"

docker run --rm \
  -v "$PROJECT_ROOT/build/valgrind:/build:ro" \
  -v "$PROJECT_ROOT/test:/test:ro" \
  -v "$RESULTS_DIR:/results" \
  "$IMAGE_NAME" \
  bash /results/run_valgrind_tests.sh

VALGRIND_EXIT_CODE=$?

# Führe Memory Analysis aus (optional)
if [[ "$MEMORY_ANALYSIS" == "true" ]]; then
  echo -e "${BLUE}📊 Führe Memory Analysis aus...${NC}"
  
  # Wenn spezifischer Test angegeben, passe Memory Analysis an
  if [[ -n "$SPECIFIC_TEST" ]]; then
    # Prüfe ob der Test verify_only ist (sonst anpassen)
    if [[ "$SPECIFIC_TEST" == "verify_only" ]]; then
      docker run --rm \
        -v "$PROJECT_ROOT/build/valgrind:/build:ro" \
        -v "$RESULTS_DIR:/results" \
        "$IMAGE_NAME" \
        bash /results/run_memory_analysis.sh || true
    else
      echo -e "${YELLOW}⚠️  Memory Analysis ist derzeit nur für 'verify_only' konfiguriert${NC}"
      echo -e "${YELLOW}   Überspringe Memory Analysis für '$SPECIFIC_TEST'${NC}"
    fi
  else
    docker run --rm \
      -v "$PROJECT_ROOT/build/valgrind:/build:ro" \
      -v "$RESULTS_DIR:/results" \
      "$IMAGE_NAME" \
      bash /results/run_memory_analysis.sh || true
  fi
fi

# Zeige Zusammenfassung
echo -e "\n${BLUE}📊 Valgrind Ergebnisse:${NC}"
echo -e "${BLUE}============================================${NC}"

if [ -f "$RESULTS_DIR/valgrind_summary.md" ]; then
    # Formatiere Ausgabe
    cat "$RESULTS_DIR/valgrind_summary.md" | while IFS= read -r line; do
        if [[ $line == *"❌"* ]]; then
            echo -e "${RED}$line${NC}"
        elif [[ $line == *"✅"* ]]; then
            echo -e "${GREEN}$line${NC}"
        elif [[ $line == *"⚠️"* ]]; then
            echo -e "${YELLOW}$line${NC}"
        else
            echo "$line"
        fi
    done
else
    echo -e "${RED}❌ Keine Zusammenfassung gefunden${NC}"
fi

echo -e "${BLUE}============================================${NC}"

# Zeige Memory Analysis (falls vorhanden)
if [[ "$MEMORY_ANALYSIS" == "true" ]] && [ -f "$RESULTS_DIR/memory_summary.md" ]; then
    echo -e "\n${BLUE}📈 Memory Analysis:${NC}"
    echo -e "${BLUE}============================================${NC}"
    cat "$RESULTS_DIR/memory_summary.md"
    echo -e "${BLUE}============================================${NC}"
fi

# Zeige detaillierte Logs
echo -e "\n${BLUE}📄 Detaillierte Logs:${NC}"
echo "  Zusammenfassung: $RESULTS_DIR/valgrind_summary.md"
for log in "$RESULTS_DIR"/*.log; do
    if [ -f "$log" ]; then
        echo "  $(basename "$log")"
    fi
done

if [[ "$MEMORY_ANALYSIS" == "true" ]]; then
    echo -e "\n${BLUE}📄 Memory Analysis Reports:${NC}"
    echo "  $RESULTS_DIR/memory-analysis/"
fi

# Exit mit Valgrind Exit Code
if [ $VALGRIND_EXIT_CODE -eq 0 ]; then
    echo -e "\n${GREEN}✅ Alle Valgrind Tests bestanden!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ Valgrind hat Memory-Probleme gefunden${NC}"
    echo -e "${YELLOW}💡 Tipp: Überprüfe die detaillierten Logs in $RESULTS_DIR${NC}"
    echo -e "${YELLOW}💡 Für interaktive Nutzung: docker run -it -v \$PWD:/src:ro -v \$PWD/build/valgrind:/build $IMAGE_NAME bash${NC}"
    exit $VALGRIND_EXIT_CODE
fi

