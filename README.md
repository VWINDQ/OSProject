# Cinema Reservation System

ระบบจองที่นั่งโรงภาพยนตร์แบบ Concurrent เขียนด้วยภาษา C ใช้ **POSIX Message Queue** เป็นช่องทางสื่อสารระหว่าง Client กับ Server
Server เป็น process เดียวที่มี **Worker หลายตัว (thread)** ประมวลผลคำขอพร้อมกัน และใช้ **Mutex** (`pthread_mutex_t`) ป้องกัน Race Condition
ที่เกิดจาก Worker หลายตัวแก้ตารางที่นั่งร่วมกัน รันและทดลองผ่านหลาย Terminal บน Linux/Docker

| # | ข้อกำหนดของโจทย์ (หัวข้อ 6) | ทำไว้ที่ |
|---|---|---|
| 1 | เขียนด้วย C หรือ C++ | `server.c`, `client.c`, `reservation.c`, `logger.c` (C11) |
| 2 | System V หรือ POSIX Message Queue | POSIX: `/cinema_request` และ `/cinema_client_<client_id>_<pid>` |
| 3 | เลือกสถานการณ์หนึ่งแบบ | โรงภาพยนตร์ (จองที่นั่ง) |
| 4 | ทรัพยากรอย่างน้อย 20 รายการ | ที่นั่ง 1-20 |
| 5 | Client อย่างน้อย 5 ตัว | เปิดกี่ตัวก็ได้ (`./client <id>`), ทดลองด้วย 5 ตัว |
| 6 | Worker อย่างน้อย 3 ตัว | ค่าเริ่มต้น 3 ตัว (`./server sync 3`) ตั้งได้ 1-32 |
| 7 | Shared Reservation Data | `owners[1..20]` ใน `reservation.c` |
| 8 | คำสั่ง LIST, STATUS, RESERVE, CANCEL | ดู "คำสั่งที่ Client รองรับ" |
| 9 | สร้าง Race Condition โดยตั้งใจได้ | `./server nosync 3` |
| 10 | random delay ระหว่าง check และ update | 50-500 ms ใน `RESERVE` (เปิดทุกโหมด) |
| 11 | Mutex หรือ Semaphore แก้ Race Condition | `seat_mutex[1..20]` (`./server sync 3`) |
| 12 | Log ของ Worker และ Critical Section | ดู "อ่าน Server Log" |
| 13 | Compile และ Run ผ่าน Docker | ดู "Build Docker Image" และ "Run Container" |
| 14 | ทดลองจากหลาย Terminal | ดู "เปิด Client หลายตัว" |

## โครงสร้างไฟล์

```text
cinema-reservation/
├── server.c            # main, Receiver, Work Queue, Worker, ตรวจ request, shutdown
├── reservation.c/.h    # ตารางที่นั่ง, seat_mutex, check/delay/update, โหมด sync/nosync
├── logger.c/.h         # log ที่มี sequence number และเวลา (thread-safe)
├── client.c            # Client (ส่งคำสั่ง, รับผลจาก Server)
├── common.h            # protocol ที่ Client/Server ใช้ร่วมกัน
├── scripts/
│   ├── experiments.sh  # Demo 1 และ Experiment 1-3 (เก็บผลไว้ใน results/)
│   ├── smoke_test.sh   # ทดสอบ Server กับ Client แบบ end-to-end
│   ├── check_readme.sh # ตรวจว่า README ครบตามโจทย์ข้อ 7
│   └── dk.sh           # รันคำสั่งใน container gcc (สำหรับ Git Bash บน Windows)
├── tests/              # unit test (logger, reservation) และ raw_request.c
├── results/            # หลักฐานการทดลองจริงจาก Docker
├── Dockerfile
├── Makefile
└── README.md
```

## Build Docker Image

```bash
docker build -t cinema-reservation .
```

image ใช้ `gcc:14-bookworm` คัดลอกซอร์สแล้วรัน `make` (ได้ `./server` และ `./client` ใน `/app`)

## Run Container

```bash
docker run -dit --name cinema -v "${PWD}/results:/app/results" cinema-reservation
```

- `-dit` ให้ container ค้างอยู่เบื้องหลัง เพื่อเปิดหลาย Terminal ด้วย `docker exec`
- ทุก Terminal ต้องเข้า **container เดียวกัน** เพราะ POSIX Message Queue อยู่ใน IPC namespace ของ container
- `-v ...results` ทำให้หลักฐานการทดลองที่สคริปต์เขียนลง `/app/results` ปรากฏในโฟลเดอร์ `results/` บนเครื่อง
- `${PWD}` ใช้ได้ใน PowerShell และ bash; ใน cmd.exe ใช้ `%cd%`; ใน Git Bash บน Windows ให้ใช้ `$(pwd -W)`

## เปิด Server

```text
./server [sync|nosync] [workers] [delay_min_ms delay_max_ms]
```

| อาร์กิวเมนต์ | ความหมาย | ค่าเริ่มต้น |
|---|---|---|
| `sync` / `nosync` | เปิด/ปิด Mutex ที่ป้องกันตารางที่นั่ง | `sync` |
| `workers` | จำนวน Worker thread (1-32) | `3` |
| `delay_min_ms delay_max_ms` | ช่วง random delay ระหว่าง check กับ update ของ `RESERVE` (0 ≤ min ≤ max ≤ 10000) | `50 500` |

Terminal 1:

```bash
docker exec -it cinema ./server sync 3
```

กด `Ctrl+C` เพื่อหยุด Server: Worker จะทำคำขอที่รับไว้แล้วให้เสร็จ, ลบ Message Queue, แล้วพิมพ์ตาราง Resource/Status/Owner สุดท้ายกับจำนวนคำขอที่แต่ละ Worker ทำ

## เปิด Client หลายตัว

| Terminal | หน้าที่ | คำสั่ง |
|---|---|---|
| 1 | Server | `docker exec -it cinema ./server sync 3` |
| 2 | Client 1 | `docker exec -it cinema ./client 1` |
| 3 | Client 2 | `docker exec -it cinema ./client 2` |
| 4 | Client 3 | `docker exec -it cinema ./client 3` |
| 5 | Client 4 | `docker exec -it cinema ./client 4` |
| 6 | Client 5 | `docker exec -it cinema ./client 5` |

```bash
docker exec -it cinema ./client 1
docker exec -it cinema ./client 2
docker exec -it cinema ./client 3
docker exec -it cinema ./client 4
docker exec -it cinema ./client 5
```

Client ID ต้องเป็นจำนวนเต็ม 1-999999 ถ้ายังไม่เปิด Server Client จะแจ้งว่า `/cinema_request` ไม่มีอยู่แล้วจบโปรแกรม

## คำสั่งที่ Client รองรับ

| คำสั่ง | ความหมาย | ตัวอย่างข้อความตอบ |
|---|---|---|
| `LIST` | แสดงที่นั่ง 1-20 และเจ้าของ | `SUCCESS: Seats (- = available, Cn = reserved by Client n): 1:- 2:C3 …` |
| `STATUS <seat_id>` | ดูสถานะที่นั่ง | `SUCCESS: Seat 10 is available.` / `SUCCESS: Seat 10 is reserved by Client 1.` |
| `RESERVE <seat_id>` | จองที่นั่ง | `SUCCESS: Seat 10 reserved successfully.` / `FAILED: Seat 10 is already reserved.` |
| `CANCEL <seat_id>` | ยกเลิกการจอง (เฉพาะเจ้าของ) | `SUCCESS: Seat 10 reservation cancelled.` / `FAILED: Seat 10 belongs to another client.` |
| `QUIT` | ปิด Client | `SUCCESS: Client session closed.` |

## รูปแบบ Message Queue

ใช้ **POSIX Message Queue** (`mq_open`, `mq_send`, `mq_timedreceive`, `mq_receive`) สองชนิด:

| Queue | ชื่อ | ใครสร้าง/ใครเขียน/ใครอ่าน |
|---|---|---|
| Request Queue | `/cinema_request` | Server สร้างและอ่าน, ทุก Client เขียน |
| Response Queue | `/cinema_client_<client_id>_<pid>` | Client แต่ละตัวสร้างและอ่านของตัวเอง, Server เขียนตอบ |

- Client ใส่ชื่อ Response Queue ของตนลงในทุก `Request` Server ตอบไปที่ชื่อนั้นเท่านั้น จึงไม่มี Client แย่งอ่านคำตอบของกัน
- PID ในชื่อคิวทำให้เปิด Client ID เดิมหลายตัวพร้อมกันได้โดยชื่อไม่ชน
- Request Queue ลึก 10 ข้อความ ถ้ามี Client ส่งมากกว่านั้นพร้อมกัน `mq_send` ของ Client จะรอจนมีที่ว่าง (ไม่สูญหาย)

```c
typedef struct {
    int client_id;
    char command[20];
    int resource_id;
    char response_queue[64];
} Request;

typedef struct {
    int success;        /* 1 = SUCCESS, 0 = FAILED */
    char message[512];  /* เฉพาะเนื้อหา Client เติม SUCCESS:/FAILED: ให้ */
} Response;
```

Server ตรวจทุกคำขอก่อนประมวลผล: ขนาดข้อความต้องเท่ากับ `sizeof(Request)`, command ต้องเป็นหนึ่งใน 5 คำสั่ง, `client_id` อยู่ใน 1-999999,
ที่นั่งอยู่ใน 1-20 และชื่อ Response Queue ต้องขึ้นต้น `/cinema_client_` ตามด้วยตัวเลข/`_` เท่านั้น (กัน request ปลอมที่ให้ Server เขียนลงคิวอื่น)
คำขอที่ไม่ผ่านจะถูกตอบ `FAILED` หรือถูกทิ้งพร้อมบันทึก log

## สถาปัตยกรรม Server

```text
Client 1..5 ──mq_send──► /cinema_request  (POSIX message queue)
                               │
                  Receiver (main thread): mq_timedreceive → ตรวจ → push
                               │
                  Work Queue ในหน่วยความจำ (queue_mutex + condvar, ความจุ 64)
                               │
                  Worker-1 … Worker-N (pthread): pop → execute
                     │                              │
                     ▼                              ▼
        Shared Reservation Table          mq_send (non-blocking) ตอบไปที่
        owners[1..20] + seat_mutex[1..20] Request.response_queue
```

**Shared Data** คือ `int owners[1..20]` (0 = AVAILABLE ไม่เช่นนั้นคือ Client ID ของเจ้าของ)

**Critical Section** ของแต่ละคำสั่ง:

| คำสั่ง | Critical Section |
|---|---|
| `RESERVE` | check (`owners[id] == 0`) → random delay → update (`owners[id] = client_id`) ทั้งก้อน |
| `CANCEL` | check เจ้าของ → update |
| `STATUS` | อ่านที่นั่งเดียว |
| `LIST` | อ่านทั้ง 20 ที่ (lock 1→20 ตามลำดับ คลาย 20→1 ไม่ให้เกิด deadlock) |

**Mutex สามกลุ่ม** (แยกกันชัดเจน):

| Mutex | ปกป้องอะไร | เปิด/ปิดได้ไหม |
|---|---|---|
| `seat_mutex[1..20]` | ตารางที่นั่ง `owners[]` (ตัวที่ใช้ทดลอง) | ตามโหมด `sync`/`nosync` |
| `queue_mutex` (+ condvar) | Work Queue ภายในระหว่าง Receiver กับ Worker | เปิดตลอด (ท่อภายใน ไม่เกี่ยวกับการทดลอง) |
| `log_mutex` | ไม่ให้บรรทัด log ปนกัน และให้ sequence number ตรงกับลำดับบรรทัด | เปิดตลอด |

**ทำไม Message Queue เพียงอย่างเดียวจึงไม่พอป้องกัน Race Condition:** Message Queue ส่งมอบแต่ละคำขอให้ Worker เพียงหนึ่งตัวแบบอะตอมมิก
แต่พอมี Worker หลายตัวหยิบคำขอคนละอัน (เช่น `RESERVE 10` จาก Client 1, 2, 3) ไปประมวลผลพร้อมกัน ทั้งหมดก็ไปอ่านและเขียน `owners[10]` ตัวเดียวกัน
คิวไม่ได้ควบคุมลำดับการอ่าน-เขียนข้อมูลที่ Worker แชร์กัน จึงต้องมี Mutex ครอบ Critical Section

**เลือก mutex ต่อที่นั่ง:** ผู้จองที่นั่งต่างกันไม่ต้องรอกัน (Demo 1 ทำงานขนานจริง) ส่วนผู้แย่งที่นั่งเดียวกันจะชนกันที่ mutex ของที่นั่งนั้นเท่านั้น

**ข้อยกเว้น `LIST`:** ต้องล็อกครบทั้ง 20 ที่นั่ง (เรียง 1→20) เพื่อให้ได้ snapshot ที่สอดคล้องกัน ถ้ามี `RESERVE` ที่นั่งใดกำลังหน่วงอยู่ `LIST` จะรอที่ที่นั่งนั้น (นานสุดเท่า `delay_max`) และระหว่างรอ `LIST` ถือล็อกที่นั่งเลขน้อยกว่าไว้แล้ว คำสั่งที่ใช้ที่นั่งเหล่านั้นจึงต้องรอด้วย ทั้งหมดนี้เห็นได้ใน log: `locking all seats in order 1..20` ตามด้วย `waiting for mutex of Resource n` ของ `LIST` เอง

## วิธีเปิด/ปิด Synchronization

เลือกตอนเปิด Server ไม่ต้อง compile ใหม่:

```bash
docker exec -it cinema ./server nosync 3   # ปิด Mutex: เกิด Race Condition ได้
docker exec -it cinema ./server sync 3     # เปิด Mutex: เหลือผู้จองสำเร็จคนเดียว
```

random delay (50-500 ms) **เปิดอยู่ทั้งสองโหมด** เพื่อพิสูจน์ว่า Mutex เป็นตัวแก้ปัญหา ไม่ใช่โชค โหมด `sync` ล็อกเฉพาะ `seat_mutex[]`

## วิธีทดลอง Race Condition

### ทดลองด้วยมือ (หลาย Terminal)

1. Terminal 1: `docker exec -it cinema ./server nosync 3`
2. Terminal 2-6: เปิด `./client 1` ถึง `./client 5` แล้วพิมพ์ `RESERVE 10` ให้ใกล้เคียงกันที่สุด (ภายในครึ่งวินาที)
3. สังเกตผล: หลาย Client ได้ `SUCCESS` พร้อมกัน, Server log มีบรรทัด `RACE DETECTED`, และ `STATUS 10` จะเห็นเจ้าของเพียงคนเดียว (คนเขียนทับคนสุดท้าย)
4. ปิด Server (`Ctrl+C`) แล้วทำซ้ำโดยเปิด `./server sync 3` ผลที่ถูกต้องคือมี Client เดียวได้ `SUCCESS` ที่เหลือ `FAILED` และไม่มี `RACE DETECTED`

### ทดลองด้วยสคริปต์ (เก็บหลักฐานใน `results/`)

```bash
docker exec cinema bash scripts/experiments.sh mixed   # Demo 1
docker exec cinema bash scripts/experiments.sh 1       # Experiment 1
docker exec cinema bash scripts/experiments.sh 2       # Experiment 2
docker exec cinema bash scripts/experiments.sh 3       # Experiment 3
docker exec cinema bash scripts/experiments.sh all     # ทั้งหมด
docker exec -e ROUNDS=10 cinema bash scripts/experiments.sh all   # 10 รอบต่อการทดลอง
```

| การทดลอง | Server | Client | ผลที่คาด |
|---|---|---|---|
| Demo 1 (`mixed`) | `sync 3` | 5 ตัว ส่งคำสั่งต่างกันพร้อมกัน (RESERVE 3, 7, 12, STATUS 10, LIST) | ทุกคำสั่งสำเร็จและที่ 3, 7, 12 เป็นของ Client ที่จอง |
| **Experiment 1** Sequential | `sync 1` (Worker 1 ตัว) | 5 ตัว `RESERVE 10` | SUCCESS เพียง 1 ราย |
| **Experiment 2** Race | `nosync 3` | 5 ตัว `RESERVE 10` | SUCCESS หลายราย + `RACE DETECTED` |
| **Experiment 3** Mutex | `sync 3` (delay เท่าเดิม) | 5 ตัว `RESERVE 10` | SUCCESS เพียง 1 ราย ไม่มี `RACE DETECTED` |

สคริปต์เปิด Client 5 ตัวพร้อมกันในแต่ละรอบ พิมพ์ตาราง `Client N : SUCCESS|FAILED` และเจ้าของสุดท้ายจาก `STATUS 10` แล้วสรุปเป็น PASS/FAIL
รอบหนึ่งจะนับว่าใช้ได้ก็ต่อเมื่อทุก Client ได้คำตอบที่ถูกต้อง (`SUCCESS` หรือ `FAILED: Seat 10 is already reserved.` เท่านั้น) และเจ้าของสุดท้ายเป็นหนึ่งใน Client ที่ได้ `SUCCESS` นอกจากนี้ Experiment 3 ต้องมีบรรทัด `waiting for mutex` อย่างน้อยหนึ่งบรรทัดในทุกรอบ เพื่อพิสูจน์ว่า Worker แย่ง mutex กันจริง
Experiment 2 เป็นเชิงความน่าจะเป็น จึงรายงานเป็น "เกิด race กี่รอบจากทั้งหมด" ผลแต่ละการทดลองอยู่ที่ `results/<ชื่อ>/summary.txt`
พร้อม `server-round<N>.log` และ `clients-round<N>.txt`

## อ่าน Server Log

รูปแบบ `[#<ลำดับ> +<ms>ms][<ผู้ทำ>] <ข้อความ>` โดยผู้ทำคือ `Server`, `Receiver` หรือ `Worker-<n>` ลำดับเพิ่มทีละ 1 ตรงกับลำดับบรรทัดจริง

| ข้อความ | ความหมาย |
|---|---|
| `received RESERVE 10 from Client-3` | Worker รับคำขอ |
| `waiting for mutex of Resource 10` | (`sync`) ที่นั่งนี้ถูก Worker อื่นล็อกอยู่ ต้องรอ |
| `locking all seats in order 1..20` | (`sync`) `LIST` เริ่มล็อกที่นั่งทีละตัวตามลำดับ (ถ้าตัวใดถูกถืออยู่จะมี `waiting for mutex of Resource n` ของ `LIST` ตามมา) |
| `entering critical section (Resource 10)` | เข้า Critical Section (ได้ล็อกแล้ว) |
| `check Resource 10: AVAILABLE` / `RESERVED by Client-1` | ผลการตรวจ (check) |
| `random delay 312 ms` | หน่วงเพื่อขยาย race window |
| `Resource 10 reserved by Client-3` | เขียนผล (update) |
| `RACE DETECTED: Resource 10 is now owned by Client-1, overwriting` | เกิด lost update: มีคนจองตัดหน้าระหว่าง delay แต่ Worker นี้เขียนทับ (พบเฉพาะ `nosync`) |
| `leaving critical section (Resource 10)` | ออกจาก Critical Section (log ก่อนคลายล็อกเสมอ) |
| `... (NO LOCK)` ต่อท้ายบรรทัดเข้า/ออก | โหมด `nosync`: ไม่มีการล็อกจริง |
| `replied SUCCESS to Client-3` | ส่งคำตอบกลับ |

### ตัวอย่างจริงจากการทดลอง

**`sync` (Experiment 3 รอบที่ 1, `results/exp3-mutex/server-round1.log`):** Worker-2 ได้ล็อกที่นั่ง 10 ก่อน Worker-1 และ Worker-3 ต้อง
`waiting for mutex` จน Worker-2 จองเสร็จและคลายล็อก แล้วแต่ละตัวที่เข้าต่อเห็น `RESERVED by Client-1` จึงตอบ `FAILED`

```text
[#0002 +0058ms][Worker-2] received RESERVE 10 from Client-1
[#0003 +0059ms][Worker-2] entering critical section (Resource 10)
[#0004 +0059ms][Worker-2] check Resource 10: AVAILABLE
[#0005 +0060ms][Worker-1] received RESERVE 10 from Client-2
[#0006 +0061ms][Worker-1] waiting for mutex of Resource 10
[#0007 +0061ms][Worker-2] random delay 181 ms
[#0008 +0062ms][Worker-3] received RESERVE 10 from Client-3
[#0009 +0064ms][Worker-3] waiting for mutex of Resource 10
[#0010 +0243ms][Worker-2] Resource 10 reserved by Client-1
[#0011 +0245ms][Worker-2] leaving critical section (Resource 10)
[#0012 +0245ms][Worker-1] entering critical section (Resource 10)
[#0013 +0246ms][Worker-1] check Resource 10: RESERVED by Client-1
[#0014 +0247ms][Worker-1] Resource 10 already reserved
[#0015 +0248ms][Worker-1] leaving critical section (Resource 10)
```

**`nosync` (Experiment 2 รอบที่ 1, `results/exp2-race/server-round1.log`):** Worker ทั้งสามตัวเข้า Critical Section พร้อมกัน (ไม่มีล็อก)
และเห็น `AVAILABLE` ทั้งหมดก่อนที่ใครจะเขียน Worker-2 (delay สั้นสุด) จองให้ Client-3 ก่อน แต่ Worker-3 ที่ตื่นทีหลัง
ยังเขียนทับเป็น Client-2 (`RACE DETECTED`) ทั้งที่ Client-3 ได้รับ `SUCCESS` ไปแล้ว

```text
[#0002 +0058ms][Worker-1] received RESERVE 10 from Client-1
[#0003 +0061ms][Worker-1] entering critical section (Resource 10) (NO LOCK)
[#0004 +0063ms][Worker-1] check Resource 10: AVAILABLE
[#0005 +0064ms][Worker-1] random delay 386 ms
[#0006 +0065ms][Worker-3] received RESERVE 10 from Client-2
[#0007 +0066ms][Worker-3] entering critical section (Resource 10) (NO LOCK)
[#0008 +0067ms][Worker-3] check Resource 10: AVAILABLE
[#0009 +0068ms][Worker-3] random delay 316 ms
[#0010 +0069ms][Worker-2] received RESERVE 10 from Client-3
[#0011 +0069ms][Worker-2] entering critical section (Resource 10) (NO LOCK)
[#0012 +0070ms][Worker-2] check Resource 10: AVAILABLE
[#0013 +0070ms][Worker-2] random delay 53 ms
[#0014 +0124ms][Worker-2] Resource 10 reserved by Client-3
[#0015 +0125ms][Worker-2] leaving critical section (Resource 10) (NO LOCK)
[#0016 +0125ms][Worker-2] replied SUCCESS to Client-3
...
[#0035 +0385ms][Worker-3] RACE DETECTED: Resource 10 is now owned by Client-3, overwriting
[#0036 +0386ms][Worker-3] Resource 10 reserved by Client-2
[#0037 +0387ms][Worker-3] leaving critical section (Resource 10) (NO LOCK)
[#0038 +0388ms][Worker-3] replied SUCCESS to Client-2
```

## การทดสอบ

```bash
docker exec cinema make test                 # ใน container
bash scripts/dk.sh make test                 # จาก Git Bash บน Windows (ใช้ container ชั่วคราว)
```

`make test` รัน unit test (logger, reservation: `sync` ได้ผู้ชนะเดียว, `nosync` เกิด race, mutex ต่อที่นั่งทำงานขนาน, `LIST` ไม่ deadlock),
smoke test แบบ end-to-end (รวม request ที่ผิดรูปแบบ, Client ที่หายไป, SIGTERM ระหว่างประมวลผล, คิวค้างหลัง `kill -9`, Client 30 ตัวขณะที่ Server ถูกหยุดจนคิวคำขอเต็มจริง, อาร์กิวเมนต์ผิด)
และ Experiment ทั้งหมด (3 รอบ เขียนผลที่ `/tmp/cinema_results` ไม่ทับ `results/`)

## ส่วน Client

### Client Workflow

```text
Start
  |
  v
Validate ./client <client_id>
  | invalid --------------------------> Show usage -> Exit
  v
Install SIGINT/SIGTERM handlers
  |
  v
Open /cinema_request (O_WRONLY)
  | server not ready -----------------> Explain error -> Exit
  v
Create /cinema_client_<id>_<pid>
  | error ----------------------------> Close/unlink -> Exit
  v
Show menu
  |
  v
Read -> Parse -> Validate input
  | invalid --------------------------> Show error --+
  |                                                |
  v                                                |
Build Request                                      |
  |                                                |
  v                                                |
mq_send(request queue)                             |
  |                                                |
  v                                                |
mq_receive(own response queue)                     |
  |                                                |
  v                                                |
Display SUCCESS/FAILED                             |
  |                                                |
  +-- command != QUIT -----------------------------+
  |
  v
mq_close() both queues -> mq_unlink() own response queue -> Exit
```

Client ไม่เก็บตารางที่นั่งและไม่ตัดสินเองว่าที่นั่งว่างหรือไม่ ส่งทีละคำขอแล้วรอคำตอบก่อนรับคำสั่งถัดไป
Response Queue สร้างด้วย `O_RDONLY | O_CREAT | O_EXCL` permission `0600`; Request Queue เปิดด้วย `O_WRONLY` โดยไม่ใช้ `O_CREAT` เพื่อให้ตรวจพบทันทีว่า Server ยังไม่เริ่ม

### หน้าที่ของแต่ละ Function ใน `client.c`

| Function | รับอะไร | ทำอะไร/คืนอะไร | เรียกตอนไหน |
|---|---|---|---|
| `show_menu` | Client ID | แสดงชื่อระบบและคำสั่ง | หลังเปิดคิวสำเร็จ |
| `read_input` | buffer และขนาด | อ่านหนึ่งบรรทัด; แยกผลปกติ, EOF, ยาวเกิน, error | ทุกรอบของ menu loop |
| `parse_command` | ข้อความ input | แยก command/seat, แปลง command เป็นตัวใหญ่, ตรวจรูปแบบ | หลังอ่าน input |
| `validate_command` | command ที่ parse แล้ว | ตรวจ seat ให้อยู่ใน 1-20 | ก่อนสร้าง Request |
| `parse_client_id` | argument จาก command line | แปลงและตรวจ Client ID | ตอนเริ่มโปรแกรม |
| `open_request_queue` | ไม่มี | เปิด `/cinema_request`; คืน queue descriptor หรือ error | ก่อนเข้า menu |
| `create_response_queue` | Client ID และ buffer ชื่อ | สร้างคิวเฉพาะ Client; คืน descriptor หรือ error | ก่อนเข้า menu |
| `build_request` | Client ID, command, queue name | ล้าง struct แล้วใส่ทุก field | ก่อนส่งแต่ละคำขอ |
| `send_request` | request queue และ `Request` | เรียก `mq_send`; คืน true/false | หลัง input ผ่าน validation |
| `receive_response` | response queue และ `Response` | เรียก `mq_receive`, ตรวจขนาดข้อความ; คืน true/false | หลังส่งสำเร็จ |
| `cleanup` | `ClientContext` | ปิด descriptor ทั้งสองและ unlink คิวตอบกลับ | ทุกทางออกหลังเปิดคิว |
| `main` | `argc/argv` | ควบคุม workflow โดยไม่ทำรายละเอียดเอง | จุดเริ่มโปรแกรม |

## ข้อจำกัด

- **Server ตัวเดียว (single point of failure):** ถ้า Server ล่ม คำขอที่ค้างหาย และไม่รองรับการเปิด Server สองตัวพร้อมกัน (ตัวหลังจะลบคิวของตัวแรก)
- **Client ไม่มี timeout:** `mq_receive` รอแบบ blocking ถ้า Server หยุดหลังรับคำขอ Client จะค้างจนกด `Ctrl+C`
- **คิวของ Client ค้างเมื่อถูก kill:** Client ที่ถูก `kill -9` ไม่ได้ `mq_unlink` คิวของตนเอง คิวนั้นค้างจน container หยุด
- **Worker เป็น thread ใน process เดียว:** ถ้า Worker ตัวใดทำให้ process ล้ม ทุก Worker ล้มตามกัน
- **Experiment 2 เป็นเชิงความน่าจะเป็น:** เกิด race เกือบทุกรอบ (delay ≥ 50 ms และ Worker 3 ตัวหยิบคำขอแรกพร้อมกัน) แต่ไม่ได้รับประกัน 100%
- **ไม่มี persistence:** ข้อมูลการจองอยู่ในหน่วยความจำ หายเมื่อ Server ปิด
- **ตรวจด้วย ThreadSanitizer ไม่ได้บน Docker Desktop ของเครื่องที่ทดลอง:** `-fsanitize=thread` หยุดด้วย `unexpected memory mapping` (ปัญหา ASLR ของ kernel)
  และ `setarch -R` ถูก seccomp ของ Docker บล็อก หลักฐานว่า Race เป็นของจริงมาจาก unit test (`nosync` มี Worker เข้า Critical Section พร้อมกันและเกิด lost update)
  บรรทัด `RACE DETECTED` ใน log และผล Experiment 2/3
- **ไม่กรองอักขระควบคุมใน log:** ชื่อ command ที่ส่งมาเองถูกพิมพ์ลง log ตามที่ได้รับ (หลังผ่านการตรวจชนิดคำสั่งแล้วเท่านั้นที่จะถูกประมวลผล)
