#!/usr/bin/env bash

# Hosts to update in parallel
hosts=(
  cc_mainnet1
  cc_mainnet2
  cc_mainnet3
  cc_gnosis1
)

# Colors (enabled for TTY)
if [ -t 1 ]; then
  YELLOW=$'\033[33m'
  GREEN=$'\033[32m'
  RED=$'\033[31m'
  BLUE=$'\033[34m'
  RESET=$'\033[0m'
else
  YELLOW=""; GREEN=""; RED=""; BLUE=""; RESET=""
fi

pids=()
hosts_running=()

for host in "${hosts[@]}"; do
  log_file="${host}.log"
  : > "$log_file"
  # Run update on remote host; redirect stdout and stderr to per-host log file
  ssh -o BatchMode=yes "$host" 'devops/update.sh' >"$log_file" 2>&1 &
  pids+=("$!")
  hosts_running+=("$host")
done

exit_codes=()
all_ok=1
fail_hosts=()
done_flags=()

num_hosts=${#hosts_running[@]}
done_count=0

echo "Monitoring updates (refresh 2s)..."
first_draw=1
while [ "$done_count" -lt "$num_hosts" ]; do
  if [ "$first_draw" -eq 0 ]; then
    printf "\033[%dA" "$num_hosts"
  fi

  for i in "${!pids[@]}"; do
    host="${hosts_running[$i]}"
    pid="${pids[$i]}"
    log_file="${host}.log"

    line=""
    host_padded=$(printf '%-12s' "$host")
    host_colored="${YELLOW}${host_padded}${RESET}"
    if [ "${done_flags[$i]}" = "1" ]; then
      code="${exit_codes[$i]}"
      if [ "$code" -eq 0 ]; then
        line="${host_colored}: ${GREEN}OK${RESET}"
      else
        line="${host_colored}: ${RED}FAIL${RESET} (exit=${code})"
      fi
    else
      if kill -0 "$pid" 2>/dev/null; then
        last_line=$(tail -n 1 "$log_file" 2>/dev/null || true)
        [ -z "$last_line" ] && last_line="(no output yet)"
        display_last="$last_line"
        max_len=60
        if [ ${#display_last} -gt $max_len ]; then
          display_last="${display_last:0:$max_len}…"
        fi
        svc_prefix=""
        svc_line=$(grep -aE '^:: \[' "$log_file" 2>/dev/null | tail -n 1 || true)
        if [ -n "$svc_line" ]; then
          # Remove leading ":: " and keep the bracket part plus following project name
          svc_prefix="${svc_line#:: }"
          [ -z "$svc_prefix" ] && svc_prefix="$svc_line"
          max_svc_len=40
          if [ ${#svc_prefix} -gt $max_svc_len ]; then
            svc_prefix="${svc_prefix:0:$max_svc_len}…"
          fi
          svc_prefix="$svc_prefix "
          svc_prefix_colored="${BLUE}${svc_prefix}${RESET}"
        fi
        if [ -n "$svc_prefix" ]; then
          line="${host_colored}: ${svc_prefix_colored}: ${display_last}"
        else
          line="${host_colored}: running : ${display_last}"
        fi
      else
        if wait "$pid"; then
          code=0
        else
          code=$?
        fi
        exit_codes[$i]="$code"
        printf "%s\n" "$code" > "${host}.exitcode"
        if [ "$code" -eq 0 ]; then
          line="${host_colored}: ${GREEN}OK${RESET}"
        else
          line="${host_colored}: ${RED}FAIL${RESET} (exit=${code})"
          fail_hosts+=("$host")
          all_ok=0
        fi
        done_flags[$i]=1
        done_count=$((done_count + 1))
      fi
    fi

    printf "\r\033[K%s\n" "$line"
  done

  first_draw=0
  if [ "$done_count" -lt "$num_hosts" ]; then
    sleep 2
  fi
done

if [ "$all_ok" -eq 1 ]; then
  exit 0
else
  exit 1
fi


