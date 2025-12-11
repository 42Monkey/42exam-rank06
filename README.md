# Rank06 - mini_serv

A lightweight TCP chat server that allows multiple clients to connect and communicate with each other in real-time.

## System Configuration

### File Descriptor Limits
The server uses fixed-size arrays for O(1) client lookups:

```c
int     fd_to_id[65536];        // Maps file descriptor → client ID
char    *fd_to_buffer[65536];   // Maps file descriptor → message buffer
```

**Why check `ulimit -n`?**
- These arrays are sized based on maximum possible file descriptors
- Your system's `ulimit -n` shows the actual fd limit per process
- If `ulimit -n` returns 1024, only indices 0-1023 will be used

```bash
# Check your system's file descriptor limit
ulimit -n

# Increase limit if needed for many concurrent connections
ulimit -n 4096
```

### IP Address Calculation
The server binds to `127.0.0.1` (localhost) using its decimal representation:

```bash
# Convert IP address 127.0.0.1 to decimal format
printf "%d\n" $((127 << 24 | 0 << 16 | 0 << 8 | 1))
# Output: 2130706433
```

**Why this calculation?**
- IPv4 addresses are stored as 32-bit integers in network code
- Each octet (127, 0, 0, 1) occupies 8 bits
- Bit shifting: `127 << 24` moves 127 to the most significant byte
- Result: `127.0.0.1` = `0x7F000001` = `2130706433` decimal

```c
// In code, this becomes:
addr.sin_addr.s_addr = htonl(2130706433);  // Bind to 127.0.0.1
```

## Testing with nc (netcat)

### Basic Usage

1. **Compile and run the server:**
```bash
gcc -Wall -Wextra -Werror mini_serv.c -o mini_serv
./mini_serv 5555
```

2. **Connect clients using nc:**
```bash
# Terminal 1 - First client (will get ID 0)
nc 127.0.0.1 5555

# Terminal 2 - Second client (will get ID 1)
nc 127.0.0.1 5555

# Terminal 3 - Third client (will get ID 2)
nc 127.0.0.1 5555
```

### Expected Behavior

**When first client connects:**
- Server sends to all existing clients: `server: client 0 just arrived\n`

**When second client connects:**
- Server sends to all existing clients: `server: client 1 just arrived\n`
- First client sees: `server: client 1 just arrived`

**When a client sends a message:**
```bash
# Client 0 types: "Hello everyone!"
# All other clients see: "client 0: Hello everyone!"
```

**Multi-line messages:**
```bash
# Client 1 types:
Line 1
Line 2
Line 3

# All other clients see:
client 1: Line 1
client 1: Line 2
client 1: Line 3
```

**When a client disconnects (Ctrl+C):**
- Server sends to all remaining clients: `server: client X just left\n`

### Advanced Testing

**Test system limits:**
```bash
# Test file descriptor limits
echo "Your system can handle $(ulimit -n) file descriptors"

# Test many connections (be careful!)
for i in $(seq 1 100); do
    nc 127.0.0.1 5555 &
done
```

**Test error cases:**
```bash
# Wrong number of arguments
./mini_serv
# Should output: "Wrong number of arguments" to stderr

# Invalid port
./mini_serv abc
# Should bind to port 0 (system assigned)

# Port already in use
./mini_serv 5555 &
./mini_serv 5555
# Second instance should output: "Fatal error" to stderr
```

**Verify IP address calculation:**
```bash
# Confirm the decimal representation
printf "127.0.0.1 in decimal: %d\n" $((127 << 24 | 0 << 16 | 0 << 8 | 1))

# Alternative calculation methods
python3 -c "print(127*256**3 + 0*256**2 + 0*256 + 1)"  # Same result
echo "127*256^3 + 0*256^2 + 0*256 + 1" | bc              # Same result
```
