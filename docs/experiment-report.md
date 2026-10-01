# รายงานการทดลอง (Experiment Report)

วิธีรันการทดลองอยู่ใน [README](../README.md) หัวข้อ "วิธีทดลอง Race Condition" เอกสารนี้สรุปผลที่ได้และอธิบายวิธีอ่าน Server log

## ผลการทดลองที่เก็บได้

ทดลองใน Docker (Docker Desktop 29.7.2, kernel 6.18.33.2-microsoft-standard-WSL2, 12 CPU, gcc 14.3.0) กรณีละ 10 รอบ
แต่ละรอบเริ่ม Server ใหม่ แล้วให้ Client 5 ตัวส่ง `RESERVE 10` พร้อมกัน

| การทดลอง | Server | ผลที่ได้ (10 รอบ) |
|---|---|---|
| Experiment 1: Sequential | `sync 1` | มี `SUCCESS` เพียง 1 ราย ทุกรอบ และไม่มี `RACE DETECTED` |
| Experiment 2: Concurrent ไม่มี Synchronization | `nosync 3` | มี `SUCCESS` 3 ราย ทุกรอบ มี `RACE DETECTED` 2 บรรทัดต่อรอบ และตารางที่นั่งเหลือเจ้าของคนเดียว (lost update) |
| Experiment 3: Concurrent มี Mutex | `sync 3` (delay เท่าเดิม) | มี `SUCCESS` เพียง 1 ราย ทุกรอบ ไม่มี `RACE DETECTED` และมี `waiting for mutex` ทุกรอบ (Worker แย่ง mutex กันจริง) |
| Demo 1: คำสั่งต่างกันพร้อมกัน | `sync 3` | `RESERVE 3`, `RESERVE 7`, `RESERVE 12`, `STATUS 10`, `LIST` สำเร็จครบ เสร็จภายในราว 0.5 วินาที |

โฟลเดอร์ `results/` ที่สคริปต์สร้างตอนรันไม่ถูกเก็บใน repo (อยู่ใน `.gitignore`) ถ้าต้องการหลักฐานชุดใหม่ ให้รัน:

```bash
docker exec -e ROUNDS=10 cinema bash scripts/experiments.sh all
```

แล้วดู `results/<ชื่อการทดลอง>/summary.txt` ใน container (คัดลอกออกมาด้วย `docker cp cinema:/app/results ./my-results`)

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

**`sync` (Experiment 3 รอบที่ 1 จากการรันจริง):** Worker-2 ได้ล็อกที่นั่ง 10 ก่อน Worker-1 และ Worker-3 ต้อง
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

**`nosync` (Experiment 2 รอบที่ 1 จากการรันจริง):** Worker ทั้งสามตัวเข้า Critical Section พร้อมกัน (ไม่มีล็อก)
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
