# Cinema Reservation System - Client

โปรเจกต์ส่วน Client สำหรับระบบจองที่นั่งโรงภาพยนตร์ ใช้ภาษา C และ POSIX Message Queue บน Linux/Docker

> `mock_server.c` มีไว้ทดสอบ Client ก่อนเชื่อมกับ Server จริงเท่านั้น ไม่ใช่ Server ที่ส่งงาน เพราะไม่มี Worker 3 ตัว, race-condition mode, random delay, mutex/semaphore หรือ log ตามโจทย์ฉบับเต็ม

## 1. Requirement ที่เกี่ยวข้องกับ Client

ข้อกำหนดจากเอกสารอาจารย์:

- เขียนด้วย C หรือ C++ และ compile/run บน Linux/Docker
- ใช้ System V หรือ POSIX Message Queue; งานนี้เลือก POSIX Message Queue
- ระบบมีทรัพยากรอย่างน้อย 20 รายการ; งานนี้ใช้ที่นั่ง 1-20
- รองรับ `LIST`, `STATUS <resource_id>`, `RESERVE <resource_id>`, `CANCEL <resource_id>` และ `QUIT`
- รองรับ Client อย่างน้อย 5 ตัวและทดลองผ่านหลาย Terminal
- Client ส่งคำขอเข้า Request Message Queue และรับผลจาก Server
- source ที่ส่งต้องมีอย่างน้อย Client, Server, Dockerfile และ README

ข้อกำหนดในเอกสารที่เป็นหน้าที่ Server ไม่ใช่ Client:

- Worker อย่างน้อย 3 ตัวและ concurrent processing
- Shared Reservation Data และ seat ownership
- การสร้าง race condition ด้วย random delay ระหว่าง check/update
- การป้องกัน critical section ด้วย mutex หรือ semaphore
- Worker/critical-section log และการทดลอง 3 กรณี

ดังนั้น Client นี้ไม่เก็บตารางที่นั่งและไม่ตัดสินเองว่าที่นั่งว่างหรือไม่

## 2. Client Workflow

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

## 3. Message Queue Design

### Request Queue

- ชื่อ `/cinema_request`
- Server เป็นผู้สร้าง; Client เปิดด้วย `O_WRONLY` เพราะ Client ส่งอย่างเดียว
- Client ไม่ใช้ `O_CREAT` เพื่อให้ตรวจพบได้ทันทีว่า Server ยังไม่เริ่ม
- ทุก Client ใช้คิวนี้ร่วมกัน ขนาดข้อความต้องเป็น `sizeof(Request)`

### Response Queue

- ชื่อ `/cinema_client_<client_id>_<pid>` เช่น `/cinema_client_3_1250`
- Client สร้างด้วย `O_RDONLY | O_CREAT | O_EXCL`, permission `0600`
- `O_RDONLY`: Client รับอย่างเดียว
- `O_CREAT`: สร้างคิวถ้ายังไม่มี
- `O_EXCL`: ป้องกันการเปิดคิวชื่อซ้ำโดยไม่รู้ตัว
- PID ทำให้แม้เปิด Client ID เดิมพร้อมกัน ชื่อคิวก็ยังไม่ชนกัน
- Client ใส่ชื่อคิวของตนในทุก `Request`; Server ต้องเปิดชื่อนั้นและตอบกลับไปที่คิวนั้นเท่านั้น จึงไม่เกิดการแย่งอ่านข้อความตอบกลับของ Client อื่น

### Message Structure

```c
typedef struct {
    int client_id;
    char command[20];
    int resource_id;
    char response_queue[64];
} Request;

typedef struct {
    int success;
    char message[512];
} Response;
```

คง struct ตามข้อเสนอเดิมเพื่อให้ integration ง่าย ไม่เพิ่ม field ที่ Server ของเพื่อนต้องรองรับโดยไม่จำเป็น สัญญาร่วมกันคือ `success` เป็น `1` เมื่อสำเร็จและ `0` เมื่อล้มเหลว ส่วน `message` เป็นข้อความเนื้อหาโดยไม่ต้องใส่ `SUCCESS:`/`FAILED:` เพราะ Client เติม prefix ให้

การไหลของข้อความ:

```text
Client N -- Request{..., response_queue="/cinema_client_N_PID"}
         --> /cinema_request --> Server/Worker

Server/Worker -- Response{success, message}
              --> /cinema_client_N_PID --> Client N
```

Client ส่งทีละคำขอแล้วรอคำตอบก่อนรับคำสั่งถัดไป จึงไม่ต้องมี request sequence number ใน protocol รุ่นนี้

## 4. Project Structure

```text
cinema-reservation/
├── client.c            # Client ตัวจริง
├── common.h            # protocol ที่ Client/Server ต้องใช้ร่วมกัน
├── server.c            # Server จริงจากสมาชิกทีม (ยังไม่อยู่ในส่วนนี้)
├── mock_server.c       # Server แบบง่ายสำหรับทดสอบ Client เท่านั้น
├── scripts/
│   └── smoke_test.sh
├── Dockerfile
├── Makefile
└── README.md
```

## 5. หน้าที่ของแต่ละ Function ใน `client.c`

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

`mq_open()` คือการเปิด/สร้างช่อง IPC ที่ kernel ดูแล, `mq_send()` คัดลอก Request เข้า queue ของ Server และ `mq_receive()` รออ่านข้อความตอบกลับจาก queue ของ Client เอง Message Queue ช่วยให้ process ที่แยกกันสื่อสารโดยไม่ต้องแชร์ address space แต่ไม่ได้ป้องกัน Worker หลายตัวแก้ shared reservation table พร้อมกัน; ส่วนหลังจึงยังต้องใช้ mutex/semaphore ที่ Server

## 6. Build และ Run บน Docker

```bash
docker build -t cinema-reservation .
docker run -it --name cinema-lab cinema-reservation
```

ใน container แรก:

```bash
./mock_server
```

เปิด Terminal เพิ่มแล้วเข้า container เดิม:

```bash
docker exec -it cinema-lab bash
./client 1
```

บน Linux ที่ติดตั้ง GCC แล้ว สามารถ build โดยตรง:

```bash
make
# เทียบเท่าหลัก ๆ กับ:
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 client.c -o client -lrt
```

`-lrt` ใช้ link POSIX realtime library บน Linux รุ่นเก่า; glibc รุ่นใหม่อาจไม่จำเป็นแต่ใส่ไว้เพื่อ compatibility

## 7. ทดสอบ Client ก่อนมี Server จริง

ทดสอบอัตโนมัติใน Linux/Docker:

```bash
make test
```

ทดสอบด้วยมือ:

```bash
# Terminal A
./mock_server

# Terminal B
./client 1
```

ลองคำสั่งถูกต้อง:

```text
LIST
STATUS 10
RESERVE 10
STATUS 10
CANCEL 10
QUIT
```

ลอง input ผิดเพื่อยืนยันว่าไม่ crash:

```text
STATUS abc
RESERVE
RESERVE 100
CANCEL -1
HELLO
LIST extra
```

ทดสอบ Server ยังไม่เปิดโดยหยุด `mock_server` แล้วรัน `./client 1`; Client ต้องแจ้งว่า `/cinema_request` ไม่มีอยู่และจบโปรแกรม

## 8. Multi-Client Test (5 Clients)

หลัง build และเปิด container ชื่อ `cinema-lab` แล้ว:

```text
Terminal 1: docker exec -it cinema-lab bash  -> ./mock_server
Terminal 2: docker exec -it cinema-lab bash  -> ./client 1
Terminal 3: docker exec -it cinema-lab bash  -> ./client 2
Terminal 4: docker exec -it cinema-lab bash  -> ./client 3
Terminal 5: docker exec -it cinema-lab bash  -> ./client 4
Terminal 6: docker exec -it cinema-lab bash  -> ./client 5
```

ให้ Client ทั้ง 5 พิมพ์ `RESERVE 10` ในช่วงเวลาใกล้กันได้เพื่อเช็ก routing ของ response แต่ `mock_server` เป็นแบบ sequential จึงใช้พิสูจน์ race condition ไม่ได้ การทดลอง race condition 3 กรณีต้องใช้ Server จริงของทีม

## 9. Integration Checklist กับผู้เขียน Server

- [ ] ใช้ POSIX Message Queue เหมือนกัน
- [ ] Request Queue ชื่อ `/cinema_request`
- [ ] include ไฟล์ `common.h` เดียวกัน ห้าม copy struct แล้วแก้แยกกัน
- [ ] Request message size เท่ากับ `sizeof(Request)`
- [ ] Response message size เท่ากับ `sizeof(Response)`
- [ ] Server อ่าน `request.response_queue` และตอบไปยังชื่อนั้น ไม่ประกอบชื่อใหม่เอง
- [ ] `response.success`: `1` = success, `0` = failed
- [ ] `response.message` ต้องจบด้วย `\0` และมีเฉพาะเนื้อหา ไม่ใส่ prefix ซ้ำ
- [ ] Seat ID ที่ valid คือ 1-20
- [ ] `LIST` ใช้ `resource_id = 0` และตกลงรูปแบบข้อความที่อ่านง่าย/ไม่เกิน 511 ตัวอักษร
- [ ] `STATUS` ส่งสถานะและ owner เมื่อถูกจอง
- [ ] `RESERVE` อนุญาตเพียงหนึ่ง owner เมื่อเปิด synchronization
- [ ] `CANCEL` ตกลง policy ว่าเฉพาะ owner ยกเลิกได้หรือไม่ (mock เลือก owner-only)
- [ ] `QUIT` ต้องส่ง Response ก่อน Client ปิด/unlink queue
- [ ] ตกลงข้อความ/error code สำหรับ invalid ID, already reserved, not owner, unsupported command และ internal error
- [ ] Server ตรวจความยาว message และบังคับ null terminator ก่อนใช้ string
- [ ] Server จริงมี Worker >= 3, โหมดเปิด/ปิด synchronization และ random delay ตามโจทย์
- [ ] ทดสอบ binary ทั้งคู่ใน Docker image/architecture เดียวกัน เพราะส่ง C struct แบบ binary

## 10. Requirement vs Design Decision

| รายการ | ที่มา |
|---|---|
| C/C++, Message Queue, Linux/Docker | Requirement จากโจทย์ |
| ที่นั่งอย่างน้อย 20 และ Client อย่างน้อย 5 | Requirement จากโจทย์ |
| คำสั่งขั้นต่ำและ multi-terminal demo | Requirement จากโจทย์ |
| POSIX แทน System V | Design decision ที่ผู้ทำเลือก |
| Queue กลางชื่อ `/cinema_request` | Design decision เพื่อ integration |
| Response queue ต่อ Client | Design decision เพื่อ route คำตอบไม่ให้ Client แย่งกัน |
| เติม PID ในชื่อ response queue | Design decision เพื่อป้องกันชื่อชนและคิวค้าง |
| Permission request `0660`, response `0600` | Design decision สำหรับ same-container demo |
| Response ใช้ `success` 0/1 และข้อความ body | Design decision/สัญญาที่ต้องตกลงกับ Server |
| Client รับทีละคำขอแบบ synchronous | Design decision ที่เรียบง่ายและตรงขอบเขตงาน |
| Client ID 1-999999 | Design decision เพราะโจทย์ไม่ระบุช่วง |
| CANCEL แบบ owner-only ใน mock | Design decision ของ mock; ต้องตกลงกับ Server จริง |

ข้อจำกัดที่ต้องรู้: `mq_receive()` รอแบบ blocking จน Server ตอบตาม workflow ที่โจทย์กำหนด หาก Server ล่มหลังรับ Request ให้กด Ctrl+C เพื่อให้ Client ออกจากการรอและ cleanup คิวของตน
