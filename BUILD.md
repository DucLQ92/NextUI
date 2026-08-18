# Hướng Dẫn Biên Dịch NextUI (NextUI Build Guide)

Tài liệu này hướng dẫn chi tiết cách thiết lập môi trường, biên dịch mã nguồn hệ thống, biên dịch các core giả lập và đóng gói bản cài đặt NextUI cho các thiết bị chơi game retro (đặc biệt là **Trimui Brick Pro - TG4040**, **Trimui Smart Pro - TG5040**, **Trimui Brick - TG3040/TG5050**).

---

## 1. Yêu cầu môi trường (Prerequisites)

Dự án NextUI sử dụng **Docker Toolchain** chứa sẵn trình biên dịch chéo (GCC AArch64/ARM toolchain) cùng toàn bộ thư viện phụ thuộc (SDL2, OpenGL ES, ALSA, libsamplerate, SQLite, OpenSSL...).

* **Docker:** Cài đặt và khởi chạy [Docker Desktop](https://www.docker.com/products/docker-desktop/) (hỗ trợ macOS, Linux, Windows WSL2).
* **Công cụ cơ bản:** `git`, `make`, `curl`.

Kiểm tra Docker đã hoạt động bằng lệnh:
```bash
docker info
```

---

## 2. Thiết bị & Định danh Platform

| Thiết bị | Mã Model | Platform Target | Ghi chú |
|---|---|---|---|
| **Trimui Brick Pro** | **TG4040** | `tg5040` | Màn hình 1024x768 (4:3), Allwinner A133P, Dual Analog |
| **Trimui Smart Pro** | **TG5040** | `tg5040` | Màn hình 1280x720 (16:9), Allwinner A133P |
| **Trimui Brick** | **TG3040** | `tg5040` | Màn hình 1024x768 (4:3), Allwinner A133P |
| **Trimui Smart Pro S** | **TG5050** | `tg5050` | Màn hình 1280x720 (16:9), Allwinner A523 |

> **Lưu ý:** Đối với **Trimui Brick Pro (TG4040)**, hệ thống tự động gán `PLATFORM=tg5040` và nhận diện `DEVICE=brickpro` khi khởi động để áp dụng đúng độ phân giải màn hình và cấu hình nút bấm/LED.

---

## 3. Biên dịch nhanh bằng `build.sh` (Khuyến nghị)

Script `build.sh` được đặt tại thư mục gốc giúp đơn giản hóa toàn bộ quá trình biên dịch.

### 3.1. Biên dịch hệ thống (System Binaries)
Biên dịch launcher `nextui.elf`, engine giả lập `minarch.elf`, trình quản lý phím `keymon`, cài đặt `settings`, điều khiển `ledcontrol`, quản lý pin `batmon`...:
```bash
./build.sh
# hoặc:
./build.sh system
```

### 3.2. Biên dịch Core giả lập (Libretro Cores)
* **Xem danh sách core được hỗ trợ:**
  ```bash
  ./build.sh list-cores
  ```
* **Biên dịch một core cụ thể:**
  ```bash
  ./build.sh core <tên_core>
  ```
  *Ví dụ:*
  ```bash
  ./build.sh core mgba          # Game Boy Advance
  ./build.sh core gambatte      # Game Boy / Game Boy Color
  ./build.sh core snes9x        # Super Nintendo (SNES)
  ./build.sh core fceumm        # NES / Famicom
  ./build.sh core pcsx_rearmed  # Sony PlayStation 1
  ./build.sh core picodrive     # Sega Genesis / Mega Drive
  ./build.sh core fbneo         # Arcade (FinalBurn Neo)
  ```
* **Biên dịch TẤT CẢ các core:**
  ```bash
  ./build.sh cores
  ```

### 3.3. Đóng gói bản cài đặt (Package Release)
Đóng gói thành các file `.zip` hoàn chỉnh để chép vào thẻ nhớ SD:
```bash
./build.sh package
```
Các file cài đặt sẽ được tạo trong thư mục `releases/` (ví dụ: `NextUI-*-all.zip`, `NextUI-*-base.zip`, `MinUI.zip`).

### 3.4. Vào Docker Shell để Debug & Code trực tiếp
```bash
./build.sh shell
```
*Thư mục `/root/workspace` trong container được mount trực tiếp từ `workspace/` của máy tính. Mọi thay đổi mã nguồn sẽ đồng bộ tức thì.*

### 3.5. Dọn dẹp bản build (Clean)
```bash
./build.sh clean
```

### 3.6. Biên dịch cho thiết bị khác
Sử dụng tham số `-p` hoặc `--platform`:
```bash
./build.sh -p tg5050 system       # Build hệ thống cho Smart Pro S (TG5050)
./build.sh -p tg5050 core mgba    # Build mGBA core cho TG5050
```

---

## 4. Các lệnh `make` truyền thống (Nâng cao)

Nếu không dùng `build.sh`, bạn có thể gọi trực tiếp lệnh `make`:

```bash
# Build hệ thống
make build PLATFORM=tg5040

# Build 1 core cụ thể
make build-core PLATFORM=tg5040 CORE=mgba

# Build toàn bộ cores
make build-cores PLATFORM=tg5040

# Đóng gói release
make tg5040

# Vào Docker shell
make shell PLATFORM=tg5040

# Dọn dẹp
make clean PLATFORM=tg5040
```

---

## 5. Cấu trúc thư mục đầu ra sau khi Build

Sau khi build thành công:
* **Các file thực thi hệ thống:** nằm tại `workspace/all/*/build/tg5040/` và `workspace/tg5040/*/build/tg5040/`.
* **Các file core giả lập (`*.so`):** nằm tại `workspace/tg5040/cores/output/`.
* **Gói cài đặt thẻ nhớ:** nằm tại `releases/` (chứa file zip cài đặt sạch hoặc update cho TrimUI).

---

## 6. Hướng dẫn cài đặt lên thiết bị

1. Chuẩn bị thẻ nhớ MicroSD đã format định dạng **FAT32** hoặc **exFAT**.
2. Giải nén toàn bộ nội dung của gói `releases/NextUI-*-all.zip` (hoặc `MinUI.zip` kèm các thư mục `Emus`, `Tools`, `Roms`, `Bios`) vào thư mục gốc của thẻ nhớ.
3. Đưa thẻ nhớ vào máy Trimui Brick Pro / Smart Pro và bật nguồn để thiết bị tự động cập nhật và khởi chạy NextUI.
