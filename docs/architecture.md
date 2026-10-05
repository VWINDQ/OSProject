# สถาปัตยกรรมของระบบ (Architecture)

เอกสารนี้อธิบายโครงสร้างภายในของ Server (Receiver, Work Queue, Worker, Shared Data, Mutex) และส่วน Client วิธีรันและคำสั่งต่างๆ อยู่ใน [README](../README.md) ส่วนวิธีใช้ Docker อยู่ใน [docker-runtime-flow.md](docker-runtime-flow.md) และผลการทดลองอยู่ใน [experiment-report.md](experiment-report.md)

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
mq_timedsend(request queue)                        |
  | timeout -> "Request was not sent" -> Exit      |
  v                                                |
mq_timedreceive(own response queue)                |
  | timeout -> "No reply within N s" -> Exit       |
  v                                                |
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

### หน้าที่ของแต่ละ Function ใน `src/client/client.c`

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
| `read_timeout_seconds` | ไม่มี | อ่าน `CINEMA_TIMEOUT_SECONDS` (1-3600) ถ้าไม่ตั้งหรือผิดใช้ 10 วินาที | ตอนเริ่มโปรแกรม |
| `deadline_in` | จำนวนวินาที | คืนเวลาสิ้นสุดแบบ absolute สำหรับ `mq_timed*` | ก่อนส่งและก่อนรับ |
| `send_request` | request queue, `Request`, timeout | เรียก `mq_timedsend`; หมดเวลาแล้วแจ้ง `Request was not sent`; คืน true/false | หลัง input ผ่าน validation |
| `receive_response` | response queue, `Response`, timeout | เรียก `mq_timedreceive`, ตรวจขนาดข้อความ; หมดเวลาแล้วแจ้งว่าไม่ทราบผล; คืน true/false | หลังส่งสำเร็จ |
| `cleanup` | `ClientContext` | ปิด descriptor ทั้งสองและ unlink คิวตอบกลับ | ทุกทางออกหลังเปิดคิว |
| `main` | `argc/argv` | ควบคุม workflow โดยไม่ทำรายละเอียดเอง | จุดเริ่มโปรแกรม |
