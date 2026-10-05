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

หลักฐานของรอบนี้เก็บไว้ในโฟลเดอร์ `results/` (รันเมื่อ 2026-10-05 ด้วยโค้ดที่ commit `59552ad` ดู `results/README.md`) มี `summary.txt`, `server-round<N>.log` และ `clients-round<N>.txt` ของแต่ละการทดลอง รวมถึง `load-test.txt` และ `environment.txt`

ถ้ารันซ้ำโดย mount โฟลเดอร์นี้ ไฟล์เหล่านั้นจะถูกเขียนทับ (กู้คืนด้วย `git checkout -- results`) ถ้าต้องการหลักฐานชุดใหม่ ให้รัน:

```bash
docker exec -e ROUNDS=10 cinema bash scripts/experiments.sh all
```

ถ้าไม่ได้ mount ผลจะอยู่ที่ `/app/results` ใน container (คัดลอกออกมาด้วย `docker cp cinema:/app/results ./my-results`)

## Load test

`scripts/load_test.sh` ให้ Client จำลอง 16 ตัวส่ง `STATUS` ต่อเนื่องรอบละ 5 วินาที (รอคำตอบทุกครั้งก่อนส่งใหม่) กับ Server โหมด `sync` ที่ Worker 1, 3 และ 8
ใช้ `STATUS` เพราะ `RESERVE` มี random delay 50-500 ms ที่จะบดบังตัวเลข Log ของ Server ถูกทิ้ง (`/dev/null`) แต่ Worker ยังต้องเขียน log ทุกบรรทัดอยู่

ผลที่วัดได้ในสภาพแวดล้อมเดียวกับการทดลองข้างต้น (รัน 3 รอบ ตัวเลขแต่ละรอบต่างกัน จึงแสดงเป็นช่วง ข้อมูลดิบอยู่ที่ `results/load-test.txt`):

| Worker | req/s | เวลาตอบเฉลี่ย (ms) | p95 (ms) | p99 (ms) | errors |
|---|---|---|---|---|---|
| 1 | 48,800 - 64,600 | 0.25 - 0.33 | 0.42 - 0.51 | 0.73 - 0.85 | 0 |
| 3 | 45,700 - 67,800 | 0.24 - 0.35 | 0.43 - 0.66 | 0.63 - 1.05 | 0 |
| 8 | 27,900 - 49,300 | 0.32 - 0.57 | 0.65 - 1.20 | 1.07 - 1.94 | 0 |

วิธีอ่าน:

- **Worker มากขึ้นไม่ได้ทำให้ `STATUS` เร็วขึ้น** งานของ `STATUS` สั้นมาก (ล็อกที่นั่ง อ่าน ตอบ) เวลาส่วนใหญ่หมดไปกับการส่งต่อผ่าน Work Queue, การเขียน log และการสลับ thread Worker 1 กับ 3 ตัวได้ใกล้เคียงกัน (ตัวที่เร็วกว่าสลับกันระหว่างรอบ) ส่วน 8 ตัวช้ากว่าทั้งสองในทุกรอบ ซึ่งต่างจากกรณี `RESERVE` ที่แต่ละคำขอรอ delay นาน Worker หลายตัวช่วยให้คำขอของที่นั่งต่างกันทำงานคู่ขนานได้จริง
- ตัวเลขขึ้นกับเครื่องและแกว่งมาก (รันบน Docker Desktop/WSL2 ที่ใช้ร่วมกับโปรแกรมอื่น) การวัดก่อนหน้านี้ในวันเดียวกันตอนที่เครื่องมีงานอื่นรันอยู่ ได้เพียง 22,000 - 31,000 req/s ที่ 1 Worker ใช้เปรียบเทียบจำนวน Worker ในรอบเดียวกันได้ ไม่ควรใช้เป็นค่าสัมบูรณ์
- `errors` คือจำนวนคำขอที่ส่งไม่ได้หรือไม่ได้คำตอบที่ถูกต้องภายใน 5 วินาที เป็น 0 ทุกรอบ คำสั่ง `make test` รันแบบสั้น (2 Client, 1 วินาที) เพื่อตรวจว่าเครื่องมือยังทำงานและไม่มี error

รันเอง:

```bash
docker exec cinema bash scripts/load_test.sh 16 5
```

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

**`sync` (Experiment 3 รอบที่ 1, `results/exp3-mutex/server-round1.log`):** Worker-1 ได้ล็อกที่นั่ง 10 ก่อน (Client-2) Worker-3 และ Worker-2 ต้อง `waiting for mutex` จน Worker-1 จองเสร็จและคลายล็อก แล้วแต่ละตัวที่เข้าต่อเห็น `RESERVED by Client-2` จึงตอบ `FAILED`

```text
[#0002 +0053ms][Worker-1] received RESERVE 10 from Client-2
[#0003 +0055ms][Worker-1] entering critical section (Resource 10)
[#0004 +0056ms][Worker-1] check Resource 10: AVAILABLE
[#0005 +0057ms][Worker-1] random delay 318 ms
[#0006 +0057ms][Worker-3] received RESERVE 10 from Client-3
[#0007 +0057ms][Worker-3] waiting for mutex of Resource 10
[#0008 +0058ms][Worker-2] received RESERVE 10 from Client-1
[#0009 +0058ms][Worker-2] waiting for mutex of Resource 10
[#0010 +0376ms][Worker-1] Resource 10 reserved by Client-2
[#0011 +0378ms][Worker-1] leaving critical section (Resource 10)
[#0012 +0378ms][Worker-1] replied SUCCESS to Client-2
...
[#0015 +0379ms][Worker-3] entering critical section (Resource 10)
[#0016 +0380ms][Worker-3] check Resource 10: RESERVED by Client-2
[#0017 +0380ms][Worker-3] Resource 10 already reserved
[#0018 +0380ms][Worker-3] leaving critical section (Resource 10)
```

**`nosync` (Experiment 2 รอบที่ 1, `results/exp2-race/server-round1.log`):** Worker ทั้งสามตัวเข้า Critical Section พร้อมกัน (ไม่มีล็อก) และเห็น `AVAILABLE` ทั้งหมดก่อนที่ใครจะเขียน Worker-1 จองให้ Client-2 ก่อน แต่ Worker-3 ที่ตื่นทีหลังเขียนทับเป็น Client-3 (`RACE DETECTED`) แล้ว Worker-2 เขียนทับอีกครั้งเป็น Client-1 ทั้งสามได้ `SUCCESS` แต่ตารางเหลือเจ้าของคือ Client-1 คนเดียว (lost update)

```text
[#0002 +0054ms][Worker-1] received RESERVE 10 from Client-2
[#0003 +0055ms][Worker-1] entering critical section (Resource 10) (NO LOCK)
[#0004 +0056ms][Worker-1] check Resource 10: AVAILABLE
[#0005 +0056ms][Worker-1] random delay 382 ms
[#0006 +0056ms][Worker-3] received RESERVE 10 from Client-3
[#0007 +0057ms][Worker-3] entering critical section (Resource 10) (NO LOCK)
[#0008 +0057ms][Worker-3] check Resource 10: AVAILABLE
[#0009 +0059ms][Worker-3] random delay 384 ms
[#0010 +0059ms][Worker-2] received RESERVE 10 from Client-1
[#0011 +0060ms][Worker-2] entering critical section (Resource 10) (NO LOCK)
[#0012 +0060ms][Worker-2] check Resource 10: AVAILABLE
[#0013 +0061ms][Worker-2] random delay 447 ms
[#0014 +0439ms][Worker-1] Resource 10 reserved by Client-2
...
[#0028 +0446ms][Worker-3] RACE DETECTED: Resource 10 is now owned by Client-2, overwriting
[#0029 +0446ms][Worker-3] Resource 10 reserved by Client-3
...
[#0041 +0508ms][Worker-2] RACE DETECTED: Resource 10 is now owned by Client-3, overwriting
[#0042 +0509ms][Worker-2] Resource 10 reserved by Client-1
```
