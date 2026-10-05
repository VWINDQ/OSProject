# Cinema Reservation System

ระบบจองที่นั่งโรงภาพยนตร์แบบ Concurrent เขียนด้วยภาษา C ใช้ **POSIX Message Queue** เป็นช่องทางสื่อสารระหว่าง Client กับ Server
Server เป็น process เดียวที่มี **Worker หลายตัว (thread)** ประมวลผลคำขอพร้อมกัน และใช้ **Mutex** (`pthread_mutex_t`) ป้องกัน Race Condition
ที่เกิดจาก Worker หลายตัวแก้ตารางที่นั่งร่วมกัน รันและทดลองผ่านหลาย Terminal บน Linux/Docker

| # | ข้อกำหนดของโจทย์ (หัวข้อ 6) | ทำไว้ที่ |
|---|---|---|
| 1 | เขียนด้วย C หรือ C++ | `src/server/server.c`, `src/client/client.c`, `src/reservation/reservation.c`, `src/utils/logger.c` (C11) |
| 2 | System V หรือ POSIX Message Queue | POSIX: `/cinema_request` และ `/cinema_client_<client_id>_<pid>` |
| 3 | เลือกสถานการณ์หนึ่งแบบ | โรงภาพยนตร์ (จองที่นั่ง) |
| 4 | ทรัพยากรอย่างน้อย 20 รายการ | ที่นั่ง 1-20 |
| 5 | Client อย่างน้อย 5 ตัว | เปิดกี่ตัวก็ได้ (`./client <id>`), ทดลองด้วย 5 ตัว |
| 6 | Worker อย่างน้อย 3 ตัว | ค่าเริ่มต้น 3 ตัว (`./server sync 3`) ตั้งได้ 1-32 |
| 7 | Shared Reservation Data | `owners[1..20]` ใน `src/reservation/reservation.c` |
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
├── .github/workflows/ci.yml   # CI (รันด้วยมือจากแท็บ Actions)
├── src/
│   ├── client/client.c        # Client (ส่งคำสั่ง รับผลจาก Server, มี timeout)
│   ├── server/server.c        # main, Receiver, Work Queue, Worker, shutdown
│   ├── reservation/           # ตารางที่นั่ง, seat_mutex, check/delay/update, โหมด sync/nosync
│   │   ├── reservation.c
│   │   └── reservation.h
│   ├── utils/
│   │   ├── logger.c/.h        # log ที่มี sequence number และเวลา (thread-safe)
│   │   └── server_lock.c/.h   # กันเปิด Server ซ้ำ (ล็อกไฟล์)
│   ├── benchmark/             # load test: load_test.c และสถิติ latency_stats.c/.h
│   ├── constants/constants.h  # ชื่อคิว, ขนาด, permission, timeout (ใช้ร่วม Client/Server)
│   └── models/message.h       # struct Request / Response
├── docs/
│   ├── architecture.md        # สถาปัตยกรรม Server, Critical Section, Mutex, ส่วน Client
│   ├── docker-runtime-flow.md # ขั้นตอนใช้ Docker และแก้ปัญหา
│   └── experiment-report.md   # ผลการทดลอง 3 กรณี และวิธีอ่าน Server log
├── scripts/
│   ├── experiments.sh         # Demo 1 และ Experiment 1-3
│   ├── load_test.sh           # load test: req/s และ latency ที่ Worker 1, 3, 8
│   ├── smoke_test.sh          # ทดสอบ Server กับ Client แบบ end-to-end
│   ├── check_readme.sh        # ตรวจว่า README/docs ครบตามโจทย์ข้อ 7
│   └── dk.sh                  # รันคำสั่งใน container gcc (สำหรับ Git Bash บน Windows)
├── tests/                     # unit test (logger, reservation, server_lock, latency_stats) และ raw_request.c
├── Dockerfile
├── Makefile
└── README.md
```

เอกสารเพิ่มเติม: [สถาปัตยกรรม](docs/architecture.md) · [การใช้ Docker](docs/docker-runtime-flow.md) · [ผลการทดลอง](docs/experiment-report.md)

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

ใช้ **POSIX Message Queue** (`mq_open`, `mq_timedsend`, `mq_timedreceive`) สองชนิด:

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

Server เป็น process เดียว: Receiver (main thread) อ่านคำขอจาก `/cinema_request` แล้วส่งเข้า Work Queue ให้ Worker thread 1..N ประมวลผล ตารางที่นั่ง `owners[1..20]` คือ Shared Data ที่ป้องกันด้วย `seat_mutex[1..20]` (เปิด/ปิดด้วยโหมด `sync`/`nosync`) รายละเอียด Critical Section, Mutex ทั้งสามกลุ่ม และเหตุผลที่ Message Queue เพียงอย่างเดียวไม่พอป้องกัน Race Condition อยู่ที่ [docs/architecture.md](docs/architecture.md)

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

Log มีรูปแบบ `[#<ลำดับ> +<ms>ms][<ผู้ทำ>] <ข้อความ>` เช่น `waiting for mutex of Resource 10` และ `RACE DETECTED` ความหมายของแต่ละข้อความและตัวอย่าง log จริงของ `sync` กับ `nosync` อยู่ที่ [docs/experiment-report.md](docs/experiment-report.md)

## การทดสอบ

```bash
docker exec cinema make test                 # ใน container
bash scripts/dk.sh make test                 # จาก Git Bash บน Windows (ใช้ container ชั่วคราว)
```

`make test` รัน unit test (logger, reservation: `sync` ได้ผู้ชนะเดียว, `nosync` เกิด race, mutex ต่อที่นั่งทำงานขนาน, `LIST` ไม่ deadlock; server_lock; latency_stats),
smoke test แบบ end-to-end (รวม request ที่ผิดรูปแบบ, Client ที่หายไป, SIGTERM ระหว่างประมวลผล, คิวค้างหลัง `kill -9`, Client 30 ตัวขณะที่ Server ถูกหยุดจนคิวคำขอเต็มจริง, อาร์กิวเมนต์ผิด,
Server ตัวที่สองถูกปฏิเสธ, Client หมดเวลารอ), Experiment ทั้งหมด (3 รอบ เขียนผลที่ `/tmp/cinema_results` ไม่ทับ `results/`) และ load test แบบสั้น

## Load test

วัดความเร็วของ Server ด้วยคำสั่ง `STATUS` จาก Client จำลองหลายตัวพร้อมกัน (ใช้ `STATUS` เพราะ `RESERVE` มี random delay 50-500 ms ที่จะบดบังตัวเลข) ที่ Worker 1, 3 และ 8:

```bash
docker exec cinema bash scripts/load_test.sh 16 5   # Client 16 ตัว วัดรอบละ 5 วินาที
```

พิมพ์ req/s, เวลาตอบเฉลี่ย, p50/p95/p99/max และจำนวนที่ผิดพลาดของแต่ละจำนวน Worker ตัวเลขที่วัดได้และวิธีอ่านอยู่ที่ [docs/experiment-report.md](docs/experiment-report.md)

## Timeout และการกันเปิด Server ซ้ำ

- **Client หมดเวลารอ:** Client รอส่งและรอคำตอบได้ไม่เกิน 10 วินาที (ตั้งใหม่ด้วย `CINEMA_TIMEOUT_SECONDS=1-3600`) ถ้าส่งไม่ได้จะขึ้น `Request was not sent` ถ้าส่งแล้วไม่มีคำตอบจะขึ้น
  `No reply within N s; the outcome is unknown, check STATUS before retrying.` แล้ว Client จบการทำงาน (คำขอที่ส่งไปแล้วอาจถูกประมวลผลภายหลัง จึงให้ตรวจ `STATUS` ก่อนสั่งซ้ำ)
- **Server ตัวเดียว:** Server ล็อกไฟล์ `/tmp/cinema_server.lock` ตอนเริ่ม ตัวที่สองจะขึ้น `Another server is already running` แล้วออกด้วย code 1 โดยไม่แตะคิวของตัวแรก
  ล็อกหายเองเมื่อ Server ตาย (รวม `kill -9`)

## ส่วน Client

Client (`src/client/client.c`) อ่านคำสั่งจากผู้ใช้ ตรวจรูปแบบ ส่ง `Request` เข้า `/cinema_request` แล้วรอคำตอบจากคิวของตนเอง flow และหน้าที่ของแต่ละ function อยู่ที่ [docs/architecture.md](docs/architecture.md)

## ข้อจำกัด

- **Server ตัวเดียว (single point of failure):** ถ้า Server ล่ม คำขอที่ค้างหาย (เปิดซ้ำไม่ได้ตั้งใจจะถูกกันด้วยล็อกไฟล์ แต่ไม่มี Server สำรอง)
- **Client หมดเวลาแล้วจบการทำงาน:** ไม่ลองส่งซ้ำอัตโนมัติ เพราะคำขอเดิมอาจถูกประมวลผลไปแล้ว ผู้ใช้ต้องตรวจ `STATUS` เอง
- **คิวของ Client ค้างเมื่อถูก kill:** Client ที่ถูก `kill -9` ไม่ได้ `mq_unlink` คิวของตนเอง คิวนั้นค้างจน container หยุด
- **Worker เป็น thread ใน process เดียว:** ถ้า Worker ตัวใดทำให้ process ล้ม ทุก Worker ล้มตามกัน
- **Experiment 2 เป็นเชิงความน่าจะเป็น:** เกิด race เกือบทุกรอบ (delay ≥ 50 ms และ Worker 3 ตัวหยิบคำขอแรกพร้อมกัน) แต่ไม่ได้รับประกัน 100%
- **ไม่มี persistence:** ข้อมูลการจองอยู่ในหน่วยความจำ หายเมื่อ Server ปิด
- **ตรวจด้วย ThreadSanitizer ไม่ได้บน Docker Desktop ของเครื่องที่ทดลอง:** `-fsanitize=thread` หยุดด้วย `unexpected memory mapping` (ปัญหา ASLR ของ kernel)
  และ `setarch -R` ถูก seccomp ของ Docker บล็อก หลักฐานว่า Race เป็นของจริงมาจาก unit test (`nosync` มี Worker เข้า Critical Section พร้อมกันและเกิด lost update)
  บรรทัด `RACE DETECTED` ใน log และผล Experiment 2/3
- **ไม่กรองอักขระควบคุมใน log:** ชื่อ command ที่ส่งมาเองถูกพิมพ์ลง log ตามที่ได้รับ (หลังผ่านการตรวจชนิดคำสั่งแล้วเท่านั้นที่จะถูกประมวลผล)
