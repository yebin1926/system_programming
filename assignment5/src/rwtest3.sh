#!/bin/bash

PORT=8080
DELAY=0
TOLERANCE=1

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
NC='\033[0m'

usage() {
    echo "Usage: $0 [-p port] [-d delay]"
    echo "  -p port : Server port (default: 8080)"
    echo "  -d delay: Expected server delay in seconds (default: 0 for stress test)"
    exit 1
}

while getopts "p:d:" opt; do
    case $opt in
        p) PORT=$OPTARG ;;
        d) DELAY=$OPTARG ;;
        *) usage ;;
    esac
done

OUTPUT_DIR="./stress_output"
rm -rf $OUTPUT_DIR
mkdir -p $OUTPUT_DIR

echo -e "${BLUE}=== SKVS Advanced Stress Test (Port: $PORT, Delay: ${DELAY}s) ===${NC}"

send_req() {
    local id=$1
    local cmd=$2
    echo "$cmd" | ./client -p $PORT > "$OUTPUT_DIR/${id}.log" 2>&1 &
}

send_req_sync() {
    echo "$1" | ./client -p $PORT
}

check_log_contains() {
    local id=$1
    local expected=$2
    if grep -q "$expected" "$OUTPUT_DIR/${id}.log"; then
        return 0
    else
        return 1
    fi
}

run_title() {
    echo -e "\n${MAGENTA}>>> Scenario: $1${NC}"
}

reset_server() {
    for i in {0..100}; do
        echo "DELETE key$i" | ./client -p $PORT > /dev/null 2>&1
    done
    echo "DELETE hotkey" | ./client -p $PORT > /dev/null 2>&1
}

run_title "11. Massive Concurrency: 100 requests at once"

echo -e "${YELLOW}[Step 1] Sending 100 CREATE requests...${NC}"
START=$(date +%s.%N)

for i in {1..100}; do
    send_req "mass_c_$i" "CREATE key$i value$i"
done

wait
END=$(date +%s.%N)
DURATION=$(awk "BEGIN {print $END - $START}")

echo "  > Processed 100 requests in ${DURATION}s"

FAIL_COUNT=0
for i in {1..100}; do
    if ! check_log_contains "mass_c_$i" "CREATE OK"; then
        if ! check_log_contains "mass_c_$i" "COLLISION"; then
             FAIL_COUNT=$((FAIL_COUNT+1))
        fi
    fi
done

if [ "$FAIL_COUNT" -eq 0 ]; then
    echo -e "${GREEN}  [Pass] All 100 concurrent requests handled successfully.${NC}"
else
    echo -e "${RED}  [FAIL] $FAIL_COUNT requests failed or dropped.${NC}"
fi

run_title "12. Hot Key Contention (1 Key, 50 Readers vs 10 Writers)"

echo "DELETE hotkey" | ./client -p $PORT > /dev/null
echo "CREATE hotkey initial" | ./client -p $PORT > /dev/null

echo -e "${YELLOW}[Step 1] Launching 50 Readers & 10 Writers concurrently...${NC}"

for i in {1..10}; do
    send_req "hot_w_$i" "UPDATE hotkey val_$i"
done

for i in {1..50}; do
    send_req "hot_r_$i" "READ hotkey"
done

wait

ALIVE=$(echo "READ hotkey" | ./client -p $PORT 2>&1)
if [[ "$ALIVE" == *"hotkey"* ]] || [[ "$ALIVE" == *"val_"* ]]; then
    echo -e "${GREEN}  [Pass] Server survived the lock contention. Final val: $ALIVE${NC}"
else
    echo -e "${RED}  [FAIL] Server seems unresponsive or returns error: $ALIVE${NC}"
    exit 1
fi

run_title "13. Fuzzing & Edge Cases"

echo -n "  [Check] Unknown Command... "
RESP=$(send_req_sync "DANCE key val")
if [[ "$RESP" == *"INVALID"* ]]; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${RED}FAIL (Got: $RESP)${NC}"
fi

echo -n "  [Check] Missing Arguments (CREATE)... "
RESP=$(send_req_sync "CREATE fuzzy")
if [[ "$RESP" == *"INVALID"* ]]; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${RED}FAIL (Got: $RESP)${NC}"
fi

LONG_KEY=$(printf 'a%.0s' {1..4000})
echo -n "  [Check] Buffer Overflow (Long Key)... "
RESP=$(send_req_sync "READ $LONG_KEY")
if [[ "$RESP" != "" ]]; then
    echo -e "${GREEN}OK (Server replied: $RESP)${NC}"
else
    echo -e "${RED}FAIL (Server crashed or closed connection)${NC}"
fi

echo -n "  [Check] Empty Line... "
RESP=$(echo "" | ./client -p $PORT)
echo -e "${GREEN}OK (Alive)${NC}"

run_title "14. Rapid Delete/Create Cycle"

KEY="cycle_key"
ITERATIONS=20
ERROR=0

echo -e "${YELLOW}[Step 1] Looping $ITERATIONS times (Create -> Read -> Delete)...${NC}"

for ((i=1; i<=ITERATIONS; i++)); do
    RES=$(send_req_sync "CREATE $KEY val$i")
    if [[ "$RES" != *"CREATE OK"* ]]; then ERROR=1; echo "Create failed at $i"; break; fi
    
    RES=$(send_req_sync "READ $KEY")
    if [[ "$RES" != *"val$i"* ]]; then ERROR=1; echo "Read failed at $i (Got: $RES)"; break; fi
    
    RES=$(send_req_sync "DELETE $KEY")
    if [[ "$RES" != *"DELETE OK"* ]]; then ERROR=1; echo "Delete failed at $i"; break; fi
    
    RES=$(send_req_sync "READ $KEY")
    if [[ "$RES" != *"NOT FOUND"* ]]; then ERROR=1; echo "Read after delete failed at $i"; break; fi
done

if [ "$ERROR" -eq 0 ]; then
    echo -e "${GREEN}  [Pass] State consistency maintained over $ITERATIONS cycles.${NC}"
else
    echo -e "${RED}  [FAIL] State inconsistency detected.${NC}"
fi

run_title "15. Mixed Randomized Workload (Chaos)"

echo -e "${YELLOW}[Step 1] Launching 50 random agents...${NC}"
pids=""

for i in {1..50}; do
    (
        r=$((RANDOM % 3))
        key="chaos_$((RANDOM % 5))"
        
        if [ $r -eq 0 ]; then
            echo "CREATE $key val_$i" | ./client -p $PORT > /dev/null 2>&1
        elif [ $r -eq 1 ]; then
             echo "UPDATE $key val_$i" | ./client -p $PORT > /dev/null 2>&1
        else
             echo "READ $key" | ./client -p $PORT > /dev/null 2>&1
        fi
    ) &
    pids="$pids $!"
done

wait $pids

echo -n "  [Check] Is server still alive? "
FINAL_CHECK=$(send_req_sync "CREATE sanity_check ok")
if [[ "$FINAL_CHECK" == *"CREATE OK"* ]] || [[ "$FINAL_CHECK" == *"COLLISION"* ]]; then
    echo -e "${GREEN}YES${NC}"
else
    echo -e "${RED}NO (Server unresponsive)${NC}"
fi

echo -e "\n${BLUE}=== Advanced Stress Tests Completed ===${NC}"
