#!/bin/bash

PORT=8080
HOST="sp04.snucse.org"
DELAY=3
TOLERANCE=1

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

usage() {
    echo "Usage: $0 [-p port] [-d delay]"
    echo "  -p port : Server port (default: 8080)"
    echo "  -d delay: Expected server delay in seconds (default: 2)"
    exit 1
}

while getopts "p:d:" opt; do
    case $opt in
        p) PORT=$OPTARG ;;
        d) DELAY=$OPTARG ;;
        *) usage ;;
    esac
done

OUTPUT_DIR="./test_output"
rm -rf $OUTPUT_DIR
mkdir -p $OUTPUT_DIR

echo -e "${BLUE}=== SKVS Test Suite Started (Port: $PORT, Expected Delay: ${DELAY}s) ===${NC}"

send_req() {
    local id=$1
    local cmd=$2
    local sleep_time=$3
    
    sleep $sleep_time
    date +%s.%N > "$OUTPUT_DIR/${id}.start"
    echo "$cmd" | ./client -p $PORT > "$OUTPUT_DIR/${id}.log" 2>&1
    date +%s.%N > "$OUTPUT_DIR/${id}.end"
}

verify_response() {
    local id=$1
    local expected=$2
    local file="$OUTPUT_DIR/${id}.log"
    
    if grep -q "$expected" "$file"; then
        echo -e "  [Pass] Req $id Content: '$expected' found."
    else
        echo -e "  ${RED}[FAIL] Req $id Content: Expected '$expected', but got:${NC}"
        cat $file
        return 1
    fi
}

verify_duration() {
    local id=$1
    local min=$2
    local max=$3
    
    local start=$(cat "$OUTPUT_DIR/${id}.start")
    local end=$(cat "$OUTPUT_DIR/${id}.end")
    
    local duration=$(awk "BEGIN {print $end - $start}")
    local pass=$(awk "BEGIN {print ($duration >= $min && $duration <= $max) ? 1 : 0}")
    
    if [ "$pass" -eq 1 ]; then
        echo -e "  [Pass] Req $id Duration: ${duration}s (Expected: $min ~ $max)"
    else
        echo -e "  ${RED}[FAIL] Req $id Duration: ${duration}s (Expected: $min ~ $max)${NC}"
        return 1
    fi
}

verify_order() {
    local id1=$1
    local id2=$2
    
    local end1=$(cat "$OUTPUT_DIR/${id1}.end")
    local end2=$(cat "$OUTPUT_DIR/${id2}.end")
    
    local pass=$(awk "BEGIN {print ($end1 < $end2) ? 1 : 0}")
    
    if [ "$pass" -eq 1 ]; then
         echo -e "  [Pass] Order: Req $id1 finished before Req $id2"
    else
         echo -e "  ${RED}[FAIL] Order: Req $id1 ($end1) finished AFTER Req $id2 ($end2)${NC}"
         return 1
    fi
}

reset_key() {
    local key=$1
    echo "DELETE $key" | ./client -p $PORT > /dev/null 2>&1
}

run_test() {
    local test_name=$1
    echo -e "\n${YELLOW}=== Test: $test_name ===${NC}"
}

run_test "1. Single Create & Collision"
reset_key "k1"
send_req "1-1" "CREATE k1 v1" 0 &
wait
verify_response "1-1" "CREATE OK"

send_req "1-2" "CREATE k1 v2" 0 &
wait
verify_response "1-2" "COLLISION"

run_test "2. Single Read & Not Found"
send_req "2-1" "READ k1" 0 &
wait
verify_response "2-1" "v1"

reset_key "k_none"
send_req "2-2" "READ k_none" 0 &
wait
verify_response "2-2" "NOT FOUND"

run_test "3. Single Update"
send_req "3-1" "UPDATE k1 v_new" 0 &
wait
verify_response "3-1" "UPDATE OK"

send_req "3-2" "READ k1" 0 &
wait
verify_response "3-2" "v_new"

send_req "3-3" "UPDATE k_none vvv" 0 &
wait
verify_response "3-3" "NOT FOUND"

run_test "4. Single Delete"
send_req "4-1" "DELETE k1" 0 &
wait
verify_response "4-1" "DELETE OK"

send_req "4-2" "READ k1" 0 &
wait
verify_response "4-2" "NOT FOUND"

send_req "4-3" "DELETE k1" 0 &
wait
verify_response "4-3" "NOT FOUND"

run_test "5. Concurrency Test (Different Keys)"
reset_key "c1"
reset_key "c2"
reset_key "c3"

echo "Creating keys concurrently..."

start_time=$(date +%s.%N)
send_req "5-1" "CREATE c1 v1" 0 &
send_req "5-2" "CREATE c2 v2" 0.1 &
send_req "5-3" "CREATE c3 v3" 0.2 &
wait
end_time=$(date +%s.%N)

total_dur=$(awk "BEGIN {print $end_time - $start_time}")
limit=$(awk "BEGIN {print $DELAY * 2}")
pass=$(awk "BEGIN {print ($total_dur < $limit) ? 1 : 0}")

echo "Total duration for 3 concurrent creates: ${total_dur}s"
if [ "$pass" -eq 1 ]; then
    echo -e "  [Pass] Concurrency verified (Total time < Serial time)"
else
    echo -e "  ${RED}[FAIL] Concurrency issue? Too slow.${NC}"
fi

run_test "6. Write Lock Test (Reader waits for Writer)"
reset_key "w1"
echo "CREATE w1 init" | ./client -p $PORT > /dev/null

send_req "6-1" "UPDATE w1 v_updated" 0 &
send_req "6-2" "READ w1" 0.5 &
wait

verify_response "6-1" "UPDATE OK"
verify_response "6-2" "v_updated"
verify_order "6-1" "6-2"

run_test "7. Read Lock Test (Readers share lock)"
send_req "7-1" "READ w1" 0 &
send_req "7-2" "READ w1" 0.2 &
wait

min=$(awk "BEGIN {print $DELAY - 0.5}")
max=$(awk "BEGIN {print $DELAY + 1.5}")
verify_duration "7-2" "$min" "$max"
echo -e "  [Check] If 7-2 duration is near ${DELAY}s, Shared Lock works."

run_test "8. Fine-grained Lock Test"
reset_key "key_A"
reset_key "key_B"
echo "CREATE key_A valA" | ./client -p $PORT > /dev/null
echo "CREATE key_B valB" | ./client -p $PORT > /dev/null

send_req "8-1" "UPDATE key_A valA_new" 0 &
send_req "8-2" "UPDATE key_B valB_new" 0.2 &
wait

min=$(awk "BEGIN {print $DELAY - 0.5}")
max=$(awk "BEGIN {print $DELAY + 1.5}")
verify_duration "8-2" "$min" "$max"

run_test "9. Complex Scenario (Writer Preference)"
reset_key "wp"
echo "CREATE wp val" | ./client -p $PORT > /dev/null

send_req "9-1" "READ wp" 0 &
send_req "9-2" "UPDATE wp val_new" 0.2 &
send_req "9-3" "READ wp" 0.4 &
wait

verify_order "9-1" "9-2"
verify_order "9-2" "9-3"
verify_response "9-3" "val_new"

run_test "10. QREAD Test (Bypass Writer)"
reset_key "qr"
echo "CREATE qr val" | ./client -p $PORT > /dev/null

send_req "10-1" "READ qr" 0 &
send_req "10-2" "UPDATE qr val_new" 0.2 &
send_req "10-3" "QREAD qr" 0.4 &
wait

verify_order "10-3" "10-2"
verify_response "10-3" "val"

echo -e "\n${GREEN}=== All Tests Completed ===${NC}"