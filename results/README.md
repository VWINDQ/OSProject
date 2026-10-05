# ผลการทดลอง (results)

หลักฐานจากการรันจริงใน Docker เมื่อ 2026-10-05 ด้วยโค้ดที่ commit `59552ad` (รายละเอียดเครื่องอยู่ใน `environment.txt`)
สรุปและวิธีอ่านอยู่ที่ [docs/experiment-report.md](../docs/experiment-report.md)

| ที่ | คืออะไร | สร้างด้วย |
|---|---|---|
| `exp1-sequential/` | Experiment 1: `./server sync 1`, Client 5 ตัว `RESERVE 10`, 10 รอบ | `scripts/experiments.sh 1` |
| `exp2-race/` | Experiment 2: `./server nosync 3` (เกิด Race Condition), 10 รอบ | `scripts/experiments.sh 2` |
| `exp3-mutex/` | Experiment 3: `./server sync 3` (Mutex, delay เท่าเดิม), 10 รอบ | `scripts/experiments.sh 3` |
| `mixed-demo/` | Demo 1: Client 5 ตัวส่งคำสั่งต่างกันพร้อมกัน | `scripts/experiments.sh mixed` |
| `load-test.txt` | load test 3 รอบ (Client 16 ตัว, `STATUS`, รอบละ 5 วินาที, Worker 1/3/8) | `scripts/load_test.sh 16 5` |
| `manual-rehearsal.txt` | ซ้อมแบบเปิด Client 5 ตัวด้วย `docker exec` แยกกัน กับ `./server nosync 3` | ทำด้วยมือตามขั้นตอนใน README |
| `environment.txt` | วันที่ เวอร์ชัน Docker, kernel, CPU, compiler, ขีดจำกัดของ message queue | |

## ไฟล์ในแต่ละโฟลเดอร์ทดลอง

- `summary.txt` สรุปทุกรอบ แต่ละรอบเป็นบรรทัด
  `round N: SUCCESS=<จำนวน Client ที่ได้ SUCCESS> RACE_DETECTED=<บรรทัด RACE DETECTED ใน log> final_owner=<เจ้าของที่นั่งตอนจบ> other_failures=<Client ที่ได้คำตอบผิดปกติ> mutex_waits=<จำนวนครั้งที่ Worker ต้องรอ mutex>`
- `server-round<N>.log` log ของ Server ทั้งรอบ (รูปแบบ `[#ลำดับ +ms][ผู้ทำ] ข้อความ`)
- `clients-round<N>.txt` ผลของ Client แต่ละตัวในรอบนั้น (`SUCCESS`/`FAILED`)

บรรทัดสรุปท้าย `summary.txt` ที่ขึ้นว่า "rounds that were sound ... exactly one SUCCESS" นับรอบที่มีผู้ชนะเพียงคนเดียวและไม่มี race
ใน Experiment 2 ค่านี้เป็น 0 ซึ่งเป็นสิ่งที่ต้องการ (ต้องเกิด race) ให้ดูบรรทัด `RESULT:` ซึ่งบอกว่าผ่านเงื่อนไขของการทดลองนั้นหรือไม่

## รันซ้ำ

```bash
docker run -dit --name cinema -v "${PWD}/results:/app/results" cinema-reservation
docker exec -e ROUNDS=10 cinema bash scripts/experiments.sh all
docker exec cinema bash scripts/load_test.sh 16 5
```

การ mount โฟลเดอร์นี้ทำให้ไฟล์ข้างบนถูกเขียนทับ ถ้าต้องการกู้ของเดิมที่ commit ไว้ ใช้ `git checkout -- results`
(ตัวเลขใน load test ขึ้นกับเครื่องและแกว่งระหว่างรอบ รันใหม่จะไม่ได้ค่าเท่าเดิม)
