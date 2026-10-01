# การใช้งานบน Docker (Docker Runtime Flow)

คำสั่งหลักและเหตุผลแต่ละขั้น คำสั่งเหล่านี้รันได้ทั้งใน PowerShell, Git Bash และ Linux/macOS (สรุปสั้นอยู่ใน [README](../README.md))

## ภาพรวม

ระบบทั้งหมดรันใน **container เดียว** ชื่อ `cinema` ทุก Terminal เข้ามาด้วย `docker exec` จึงอยู่ IPC namespace เดียวกัน
และเห็น POSIX Message Queue (`/cinema_request`, `/cinema_client_<id>_<pid>`) ชุดเดียวกัน

```text
Docker Desktop / Docker Engine
└── container "cinema"  (image: cinema-reservation, base gcc:14-bookworm)
    ├── Terminal 1: docker exec -it cinema ./server sync 3
    ├── Terminal 2: docker exec -it cinema ./client 1
    ├── ...
    └── Terminal 6: docker exec -it cinema ./client 5
```

ถ้าเปิด container คนละตัว Client จะมองไม่เห็นคิวของ Server (ขึ้นว่า `/cinema_request` ไม่มีอยู่)

## ขั้นตอน

| ขั้น | คำสั่ง | ทำอะไร |
|---|---|---|
| 1. Build image | `docker build -t cinema-reservation .` | คัดลอก `src/`, `scripts/`, `tests/`, `Makefile` แล้วรัน `make` ได้ `./server` และ `./client` ใน `/app` (ครั้งแรกจะดาวน์โหลด image `gcc:14-bookworm` ราว 1.4 GB) |
| 2. เปิด container | `docker run -dit --name cinema cinema-reservation` | `-d` ให้ทำงานเบื้องหลัง `-it` ให้ shell ค้างอยู่ จึงใช้ `docker exec` เข้ามาได้หลายครั้ง |
| 3. เปิด Server | `docker exec -it cinema ./server sync 3` | โหมด (`sync`/`nosync`) จำนวน Worker และช่วง delay ดูใน [README](../README.md) |
| 4. เปิด Client | `docker exec -it cinema ./client 1` | ทำซ้ำใน Terminal อื่นด้วย Client ID 2 ถึง 5 |
| 5. รันชุดทดสอบ | `docker exec cinema make test` | unit test, smoke test และ Experiment ทั้งหมด (ใช้เวลาประมาณ 1-2 นาที) |
| 6. รันการทดลอง | `docker exec cinema bash scripts/experiments.sh all` | Demo 1 และ Experiment 1-3 พิมพ์ผล PASS/FAIL |
| 7. เก็บกวาด | `docker rm -f cinema` | หยุดและลบ container |

ถ้าเคยสร้าง container ชื่อ `cinema` ไว้แล้ว ให้ `docker rm -f cinema` ก่อนรันขั้นที่ 2

## เก็บผลการทดลองออกมาจาก container

สคริปต์ทดลองเขียนผลที่ `/app/results` ใน container มีสองวิธีเอาออกมา

- `docker cp cinema:/app/results ./my-results` หลังรันเสร็จ
- ใส่ `-v "${PWD}/results:/app/results"` ตอน `docker run` (PowerShell/bash; cmd.exe ใช้ `%cd%`, Git Bash ใช้ `$(pwd -W)`) ผลจะเขียนลงโฟลเดอร์ `results/` บนเครื่องโดยตรงและเขียนทับของเดิมทุกครั้ง

`make test` เขียนผลที่ `/tmp/cinema_results` ใน container จึงไม่ทับ `results/`

## รันโดยไม่ต้องเปิด container ค้าง (Git Bash บน Windows)

```bash
bash scripts/dk.sh make test
```

`scripts/dk.sh` สร้าง container ชั่วคราวจาก `gcc:14-bookworm` ที่ mount โฟลเดอร์โปรเจกต์ไว้ที่ `/work` รันคำสั่งแล้วลบทิ้ง ใช้ได้โดยไม่ต้อง `docker build`

## ข้อควรระวัง

- **อย่ารัน `make test` ใน container ที่มี Server เปิดมือค้างอยู่** เทสจะลบและสร้าง `/cinema_request` ใหม่ ทำให้ Server ตัวที่เปิดไว้ใช้งานไม่ได้ ให้ปิด Server ด้วย `Ctrl+C` ก่อน
- **เปิด Server ได้ทีละตัว** ตัวที่สองจะลบคิวของตัวแรก
- **ปิด Server ด้วย `Ctrl+C`** (หรือ `docker stop`) ไม่ใช่ปิดหน้าต่างทิ้ง เพื่อให้ลบคิวและพิมพ์ตารางที่นั่งสุดท้าย ถ้า Client ถูกปิดทิ้งกลางคัน คิวตอบกลับของมันจะค้างจน container หยุด

## แก้ปัญหา

| อาการ | สาเหตุและวิธีแก้ |
|---|---|
| `failed to connect to the docker API` | Docker Desktop ยังไม่ทำงาน เปิดโปรแกรมและรอให้เป็นสถานะ Running |
| `name "/cinema" is already in use` | มี container เก่า รัน `docker rm -f cinema` |
| Client ขึ้นว่า `/cinema_request` ไม่มีอยู่ | ยังไม่ได้เปิด Server หรือ `docker exec` เข้าคนละ container |
| Server ขึ้น `Cannot create request queue` | ปิด Server ตัวเก่า (`Ctrl+C`) แล้วเปิดใหม่ |
| สคริปต์ `.sh` ขึ้น `bash\r: No such file` | ไฟล์ถูกแปลงเป็น CRLF ตอน clone บน Windows `.gitattributes` กำหนดให้เป็น LF อยู่แล้ว ถ้ายังเกิด ให้ `git config core.autocrlf false` แล้ว clone ใหม่ |
